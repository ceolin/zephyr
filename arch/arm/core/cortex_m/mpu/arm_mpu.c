/*
 * Copyright (c) 2017 Linaro Limited.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <device.h>
#include <init.h>
#include <kernel.h>
#include <soc.h>
#include <arch/arm/cortex_m/cmsis.h>
#include <arch/arm/cortex_m/mpu/arm_mpu.h>
#include <arch/arm/cortex_m/mpu/arm_core_mpu.h>
#include <logging/sys_log.h>
#include <linker/linker-defs.h>

#define ARM_MPU_DEV ((volatile struct arm_mpu *) ARM_MPU_BASE)

/**
 * The attributes referenced in this function are described at:
 * https://goo.gl/hMry3r
 * This function is private to the driver.
 */
static inline u32_t _get_region_attr(u32_t xn, u32_t ap, u32_t tex,
				     u32_t c, u32_t b, u32_t s,
				     u32_t srd, u32_t size)
{
	return ((xn << 28) | (ap) | (tex << 19) | (s << 18)
		| (c << 17) | (b << 16) | (srd << 8) | (size));
}

/**
 * This internal function converts the region size to
 * the SIZE field value of MPU_RASR.
 */
static inline u32_t _size_to_mpu_rasr_size(u32_t size)
{
	/* The minimal supported region size is 32 bytes */
	if (size <= 32) {
		return REGION_32B;
	}

	/*
	 * A size value greater than 2^31 could not be handled by
	 * round_up_to_next_power_of_two() properly. We handle
	 * it separately here.
	 */
	if (size > (1 << 31)) {
		return REGION_4G;
	}

	size = 1 << (32 - __builtin_clz(size - 1));
	return (32 - __builtin_clz(size) - 2) << 1;
}


/**
 * This internal function is utilized by the MPU driver to parse the intent
 * type (i.e. THREAD_STACK_REGION) and return the correct parameter set.
 */
static inline u32_t _get_region_attr_by_type(u32_t type, u32_t size)
{
	int region_size = _size_to_mpu_rasr_size(size);

	switch (type) {
	case THREAD_STACK_USER_REGION:
		return _get_region_attr(1, P_RW_U_RW, 0, 1, 0,
					1, 0, region_size);
	case THREAD_STACK_REGION:
		return _get_region_attr(1, P_RW_U_RW, 0, 1, 0,
					1, 0, region_size);
	case THREAD_STACK_GUARD_REGION:
		return _get_region_attr(1, P_RO_U_NA, 0, 1, 0,
					1, 0, region_size);
	case THREAD_APP_DATA_REGION:
		return _get_region_attr(1, P_RW_U_RW, 0, 1, 0,
					1, 0, region_size);
	default:
		/* Size 0 region */
		return 0;
	}
}

static inline u8_t _get_num_regions(void)
{
#if defined(CONFIG_CPU_CORTEX_M0PLUS) || \
	defined(CONFIG_CPU_CORTEX_M3) || \
	defined(CONFIG_CPU_CORTEX_M4)
	/* Cortex-M0+, Cortex-M3, and Cortex-M4 MCUs may
	 * have a fixed number of 8 MPU regions.
	 */
	return 8;
#else
	u32_t type = ARM_MPU_DEV->type;

	type = (type & 0xFF00) >> 8;

	return (u8_t)type;
#endif
}

/* This internal function performs MPU region initialization.
 *
 * Note:
 *   The caller must provide a valid region index.
 */
static void _region_init(u32_t index, u32_t region_addr,
			 u32_t region_attr)
{
	/* Select the region you want to access */
	ARM_MPU_DEV->rnr = index;
	/* Configure the region */
	ARM_MPU_DEV->rbar = (region_addr & REGION_BASE_ADDR_MASK)
				| REGION_VALID | index;
	ARM_MPU_DEV->rasr = region_attr | REGION_ENABLE;
	SYS_LOG_DBG("[%d] 0x%08x 0x%08x", index, region_addr, region_attr);
}

/**
 * This internal function is utilized by the MPU driver to parse the intent
 * type (i.e. THREAD_STACK_REGION) and return the correct region index.
 */
static inline u32_t _get_region_index_by_type(u32_t type)
{
	/*
	 * The new MPU regions are allocated per type after the statically
	 * configured regions. The type is one-indexed rather than
	 * zero-indexed, therefore we need to subtract by one to get the region
	 * index.
	 */
	switch (type) {
	case THREAD_STACK_USER_REGION:
		return mpu_config.num_regions + THREAD_STACK_REGION - 1;
	case THREAD_STACK_REGION:
	case THREAD_STACK_GUARD_REGION:
	case THREAD_APP_DATA_REGION:
		return mpu_config.num_regions + type - 1;
	case THREAD_DOMAIN_PARTITION_REGION:
#if defined(CONFIG_USERSPACE)
		return mpu_config.num_regions + type - 1;
#elif defined(CONFIG_MPU_STACK_GUARD)
		return mpu_config.num_regions + type - 2;
#else
		/*
		 * Start domain partition region from stack guard region
		 * since stack guard is not enabled.
		 */
		return mpu_config.num_regions + type - 3;
#endif
	default:
		__ASSERT(0, "Unsupported type");
		return 0;
	}
}

/**
 * This internal function disables a given MPU region.
 */
static inline void _disable_region(u32_t r_index)
{
	/* Attempting to configure MPU_RNR with an invalid
	 * region number has unpredictable behavior. Therefore
	 * we add a check before disabling the requested MPU
	 * region.
	 */
	__ASSERT(r_index < _get_num_regions(),
		"Index 0x%x out-of-bound (supported regions: 0x%x)\n",
		r_index,
		_get_num_regions());
	SYS_LOG_DBG("disable region 0x%x", r_index);
	/* Disable region */
	ARM_MPU_DEV->rnr = r_index;
	ARM_MPU_DEV->rbar = 0;
	ARM_MPU_DEV->rasr = 0;
}

/**
 * This internal function checks if region is enabled or not.
 *
 * Note:
 *   The caller must provide a valid region number.
 */
static inline int _is_enabled_region(u32_t r_index)
{
	ARM_MPU_DEV->rnr = r_index;

	return ARM_MPU_DEV->rasr & REGION_ENABLE_MASK;
}

/**
 * This internal function checks if the given buffer is in the region.
 *
 * Note:
 *   The caller must provide a valid region number.
 */
static inline int _is_in_region(u32_t r_index, u32_t start, u32_t size)
{
	u32_t r_addr_start;
	u32_t r_size_lshift;
	u32_t r_addr_end;

	ARM_MPU_DEV->rnr = r_index;
	r_addr_start = ARM_MPU_DEV->rbar & REGION_BASE_ADDR_MASK;
	r_size_lshift = ((ARM_MPU_DEV->rasr & REGION_SIZE_MASK) >>
			REGION_SIZE_OFFSET) + 1;
	r_addr_end = r_addr_start + (1 << r_size_lshift) - 1;

	if (start >= r_addr_start && (start + size - 1) <= r_addr_end) {
		return 1;
	}

	return 0;
}

/**
 * This internal function checks if the region is user accessible or not.
 *
 * Note:
 *   The caller must provide a valid region number.
 */
static inline int _is_user_accessible_region(u32_t r_index, int write)
{
	u32_t r_ap;

	ARM_MPU_DEV->rnr = r_index;
	r_ap = ARM_MPU_DEV->rasr & ACCESS_PERMS_MASK;

	/* always return true if this is the thread stack region */
	if (_get_region_index_by_type(THREAD_STACK_REGION) == r_index) {
		return 1;
	}

	if (write) {
		return r_ap == P_RW_U_RW;
	}

	/* For all user accessible permissions, their AP[1] bit is l */
	return r_ap & (0x2 << ACCESS_PERMS_OFFSET);
}

/* ARM Core MPU Driver API Implementation for ARM MPU */

/**
 * @brief enable the MPU
 */
void arm_core_mpu_enable(void)
{
	/* Enable MPU and use the default memory map as a
	 * background region for privileged software access.
	 */
	ARM_MPU_DEV->ctrl = ARM_MPU_ENABLE | ARM_MPU_PRIVDEFENA;
}

/**
 * @brief disable the MPU
 */
void arm_core_mpu_disable(void)
{
	/* Disable MPU */
	ARM_MPU_DEV->ctrl = 0;
}

/**
 * @brief configure the base address and size for an MPU region
 *
 * @param   type    MPU region type
 * @param   base    base address in RAM
 * @param   size    size of the region
 */
void arm_core_mpu_configure(u8_t type, u32_t base, u32_t size)
{
	SYS_LOG_DBG("Region info: 0x%x 0x%x", base, size);
	u32_t region_index = _get_region_index_by_type(type);
	u32_t region_attr = _get_region_attr_by_type(type, size);

	if (region_index >= _get_num_regions()) {
		return;
	}

	_region_init(region_index, base, region_attr);
}

#if defined(CONFIG_USERSPACE)
void arm_core_mpu_configure_user_context(struct k_thread *thread)
{
	u32_t base = (u32_t)thread->stack_obj;
	u32_t size = thread->stack_info.size;
	u32_t index = _get_region_index_by_type(THREAD_STACK_USER_REGION);
	u32_t region_attr = _get_region_attr_by_type(THREAD_STACK_USER_REGION,
						     size);

	if (!thread->arch.priv_stack_start) {
		_disable_region(index);
		return;
	}
	if (index >= _get_num_regions()) {
		return;
	}
	/* configure stack */
	_region_init(index, base, region_attr);

#if defined(CONFIG_APPLICATION_MEMORY)
	/* configure app data portion */
	index = _get_region_index_by_type(THREAD_APP_DATA_REGION);
	if (index < _get_num_regions()) {
		size = (u32_t)&__app_ram_end - (u32_t)&__app_ram_start;
		region_attr =
			_get_region_attr_by_type(THREAD_APP_DATA_REGION, size);
		if (size > 0) {
			_region_init(index, (u32_t)&__app_ram_start, region_attr);
		}
	}
#endif /* CONFIG_APPLICATION_MEMORY */
}

/**
 * @brief configure MPU regions for the memory partitions of the memory domain
 *
 * @param   mem_domain    memory domain that thread belongs to
 */
void arm_core_mpu_configure_mem_domain(struct k_mem_domain *mem_domain)
{
	u32_t region_index =
		_get_region_index_by_type(THREAD_DOMAIN_PARTITION_REGION);
	u32_t region_attr;
	u32_t num_partitions;
	struct k_mem_partition *pparts;

	if (mem_domain) {
		SYS_LOG_DBG("configure domain: %p", mem_domain);
		num_partitions = mem_domain->num_partitions;
		pparts = mem_domain->partitions;
	} else {
		SYS_LOG_DBG("disable domain partition regions");
		num_partitions = 0;
		pparts = NULL;
	}

	for (; region_index < _get_num_regions(); region_index++) {
		if (num_partitions && pparts->size) {
			SYS_LOG_DBG("set region 0x%x 0x%x 0x%x",
				    region_index, pparts->start, pparts->size);
			region_attr = pparts->attr |
				      _size_to_mpu_rasr_size(pparts->size);
			_region_init(region_index, pparts->start, region_attr);
			num_partitions--;
		} else {
			_disable_region(region_index);
		}
		pparts++;
	}
}

/**
 * @brief configure MPU region for a single memory partition
 *
 * @param   part_index  memory partition index
 * @param   part        memory partition info
 */
void arm_core_mpu_configure_mem_partition(u32_t part_index,
					  struct k_mem_partition *part)
{
	u32_t region_index =
		_get_region_index_by_type(THREAD_DOMAIN_PARTITION_REGION);
	u32_t region_attr;

	SYS_LOG_DBG("configure partition index: %u", part_index);

	if (part &&
		(region_index + part_index < _get_num_regions())) {
		SYS_LOG_DBG("set region 0x%x 0x%x 0x%x",
			    region_index + part_index, part->start, part->size);
		region_attr = part->attr | _size_to_mpu_rasr_size(part->size);
		_region_init(region_index + part_index, part->start,
			     region_attr);
	} else {
		_disable_region(region_index + part_index);
	}
}

/**
 * @brief Reset MPU region for a single memory partition
 *
 * @param   part_index  memory partition index
 */
void arm_core_mpu_mem_partition_remove(u32_t part_index)
{
	u32_t region_index =
		_get_region_index_by_type(THREAD_DOMAIN_PARTITION_REGION);

	_disable_region(region_index + part_index);
}

/**
 * @brief get the maximum number of free regions for memory domain partitions
 */
int arm_core_mpu_get_max_domain_partition_regions(void)
{
	/*
	 * Subtract the start of domain partition regions from total regions
	 * will get the maximum number of free regions for memory domain
	 * partitions.
	 */
	return _get_num_regions() -
		_get_region_index_by_type(THREAD_DOMAIN_PARTITION_REGION);
}

/**
 * @brief validate the given buffer is user accessible or not
 */
int arm_core_mpu_buffer_validate(void *addr, size_t size, int write)
{
	s32_t r_index;

	/* Iterate all mpu regions in reversed order */
	for (r_index = _get_num_regions() - 1; r_index >= 0;  r_index--) {
		if (!_is_enabled_region(r_index) ||
		    !_is_in_region(r_index, (u32_t)addr, size)) {
			continue;
		}

		/* For ARM MPU, higher region number takes priority.
		 * Since we iterate all mpu regions in reversed order, so
		 * we can stop the iteration immediately once we find the
		 * matched region that grants permission or denies access.
		 */
		if (_is_user_accessible_region(r_index, write)) {
			return 0;
		} else {
			return -EPERM;
		}
	}

	return -EPERM;
}
#endif /* CONFIG_USERSPACE */

/* ARM MPU Driver Initial Setup */

/*
 * @brief MPU default configuration
 *
 * This function provides the default configuration mechanism for the Memory
 * Protection Unit (MPU).
 */
static void _arm_mpu_config(void)
{
	u32_t r_index;

	if (mpu_config.num_regions > _get_num_regions()) {
		/* Attempt to configure more MPU regions than
		 * what is supported by hardware. As this operation
		 * is executed during system (pre-kernel) initialization,
		 * we want to ensure we can detect an attempt to
		 * perform invalid configuration.
		 */
		__ASSERT(0,
			"Request to configure: %u regions (supported: %u)\n",
			mpu_config.num_regions,
			_get_num_regions()
		);
		return;
	}

	/* Disable MPU */
	ARM_MPU_DEV->ctrl = 0;

	/* Configure regions */
	for (r_index = 0; r_index < mpu_config.num_regions; r_index++) {
		_region_init(r_index,
			     mpu_config.mpu_regions[r_index].base,
			     mpu_config.mpu_regions[r_index].attr);
	}

	/* Enable MPU and use the default memory map as a
	 * background region for privileged software access.
	 */
	ARM_MPU_DEV->ctrl = ARM_MPU_ENABLE | ARM_MPU_PRIVDEFENA;

	/* Make sure that all the registers are set before proceeding */
	__DSB();
	__ISB();
}

static int arm_mpu_init(struct device *arg)
{
	ARG_UNUSED(arg);

	_arm_mpu_config();

	/* Sanity check for number of regions in Cortex-M0+, M3, and M4. */
#if defined(CONFIG_CPU_CORTEX_M0PLUS) || \
	defined(CONFIG_CPU_CORTEX_M3) || \
	defined(CONFIG_CPU_CORTEX_M4)
	__ASSERT((ARM_MPU_DEV->type & 0xFF00) >> 8 == 8,
		"Invalid number of MPU regions\n");
#endif
	return 0;
}

SYS_INIT(arm_mpu_init, PRE_KERNEL_1,
	 CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
