/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 * Copyright (c) 2017 Oticon A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief POSIX arch specific kernel interface header
 *
 * This header contains the POSIX arch specific kernel interface. It is
 * included by the kernel interface architecture-abstraction header
 * (include/zephyr/arch/cpu.h).
 */

#ifndef ZEPHYR_INCLUDE_ARCH_POSIX_ARCH_H_
#define ZEPHYR_INCLUDE_ARCH_POSIX_ARCH_H_

/* Add include for DTS generated information */
#include <zephyr/devicetree.h>

#include <zephyr/toolchain.h>
#include <zephyr/irq.h>
#include <zephyr/arch/exception.h>
#include <zephyr/arch/posix/asm_inline.h>
#include <zephyr/arch/posix/thread.h>
#include <zephyr/arch/posix/thread_stack.h>
#include <zephyr/arch/posix/posix_arch_error.h>
#include <board_irq.h> /* Each board must define this */
#include <zephyr/sw_isr_table.h>
#include <zephyr/arch/posix/posix_soc_if.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_64BIT
#define ARCH_STACK_PTR_ALIGN 8
#else
#define ARCH_STACK_PTR_ALIGN 4
#endif

#ifdef CONFIG_USERSPACE
/* Memory partition attributes. Only the user mode permissions are enforced,
 * kernel mode code can access the whole Zephyr image RAM.
 */
#define Z_POSIX_MEM_U_R BIT(0)
#define Z_POSIX_MEM_U_W BIT(1)
#define Z_POSIX_MEM_U_X BIT(2)
#define Z_POSIX_MEM_P_W BIT(3)

typedef struct {
	uint8_t attrs;
} k_mem_partition_attr_t;

#define K_MEM_PARTITION_P_RW_U_RW                                                                  \
	((k_mem_partition_attr_t){Z_POSIX_MEM_P_W | Z_POSIX_MEM_U_R | Z_POSIX_MEM_U_W})
#define K_MEM_PARTITION_P_RW_U_RO ((k_mem_partition_attr_t){Z_POSIX_MEM_P_W | Z_POSIX_MEM_U_R})
#define K_MEM_PARTITION_P_RW_U_NA ((k_mem_partition_attr_t){Z_POSIX_MEM_P_W})
#define K_MEM_PARTITION_P_RO_U_RO ((k_mem_partition_attr_t){Z_POSIX_MEM_U_R})
#define K_MEM_PARTITION_P_RO_U_NA ((k_mem_partition_attr_t){0})
#define K_MEM_PARTITION_P_NA_U_NA ((k_mem_partition_attr_t){0})
#define K_MEM_PARTITION_P_RX_U_RX ((k_mem_partition_attr_t){Z_POSIX_MEM_U_R | Z_POSIX_MEM_U_X})
#define K_MEM_PARTITION_P_RWX_U_RWX                                                                \
	((k_mem_partition_attr_t){Z_POSIX_MEM_P_W | Z_POSIX_MEM_U_R | Z_POSIX_MEM_U_W |            \
				  Z_POSIX_MEM_U_X})
#define K_MEM_PARTITION_P_RWX_U_RX                                                                 \
	((k_mem_partition_attr_t){Z_POSIX_MEM_P_W | Z_POSIX_MEM_U_R | Z_POSIX_MEM_U_X})

#define K_MEM_PARTITION_IS_WRITABLE(attr)   (((attr).attrs & Z_POSIX_MEM_U_W) != 0U)
#define K_MEM_PARTITION_IS_EXECUTABLE(attr) (((attr).attrs & Z_POSIX_MEM_U_X) != 0U)
#endif /* CONFIG_USERSPACE */

extern uint32_t sys_clock_cycle_get_32(void);

static inline uint32_t arch_k_cycle_get_32(void)
{
	return sys_clock_cycle_get_32();
}

extern uint64_t sys_clock_cycle_get_64(void);

static inline uint64_t arch_k_cycle_get_64(void)
{
	return sys_clock_cycle_get_64();
}

static ALWAYS_INLINE void arch_nop(void)
{
	__asm__ volatile("nop");
}

static ALWAYS_INLINE bool arch_irq_unlocked(unsigned int key)
{
	return key == false;
}

static ALWAYS_INLINE unsigned int arch_irq_lock(void)
{
	return posix_irq_lock();
}


static ALWAYS_INLINE void arch_irq_unlock(unsigned int key)
{
	posix_irq_unlock(key);
}

/** Implementation of @ref arch_cpu_irqs_are_enabled. */
static ALWAYS_INLINE bool arch_cpu_irqs_are_enabled(void)
{
	/* No direct non-modifying probe exported by the POSIX SoC
	 * interface; briefly lock and restore instead.
	 */
	unsigned int key = posix_irq_lock();
	bool enabled = (key == false);

	posix_irq_unlock(key);
	return enabled;
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ARCH_POSIX_ARCH_H_ */
