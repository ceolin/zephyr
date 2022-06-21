/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (c) 2022 Intel Corporation.
 *
 * Author: Marcin Szkudlinski <marcin.szkudlinski@linux.intel.com>
 *
 */

#define DT_DRV_COMPAT intel_adsp_imr

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/mem_blocks.h>
#include <zephyr/linker/linker-defs.h>
#include <zephyr/drivers/mm/mm_drv_intel_adsp_imr_memoryblock.h>

#include <soc.h>

/* get IMR parameters from the device tree */
#define IMR_REGION DT_DRV_INST(0)
#define IMR_BASE_ADDR LINKER_DT_RESERVED_MEM_GET_PTR(DT_DRV_INST(0))
#define IMR_MEMORY_SIZE LINKER_DT_RESERVED_MEM_GET_SIZE(DT_DRV_INST(0))
#define IMR_PAGE_SIZE DT_PROP(DT_DRV_INST(0), block_size)
#define IMR_NUM_OF_PAGES (IMR_MEMORY_SIZE / IMR_PAGE_SIZE)

/* declare an IMR memory block */
SYS_MEM_BLOCKS_DEFINE_WITH_EXT_BUF(
		IMR_REGION,
		IMR_PAGE_SIZE,
		IMR_NUM_OF_PAGES,
		(uint8_t *) IMR_BASE_ADDR);

/* Check if the number is aligned with alignment value */
#define IS_ALIGNED(size, alignment) ((size) % (alignment) == 0)

/* Check if the given address is in the IMR range */
#define IS_IMR_ADDRESS(addr)						\
	((uintptr_t)(addr) >= (uintptr_t)IMR_BASE_ADDR &&		\
	(uintptr_t)(addr) <  (uintptr_t)(IMR_BASE_ADDR + IMR_MEMORY_SIZE))

int intel_adsp_ddr_memory_get(void *address, size_t length)
{
	int ret;

	__ASSERT_NO_MSG(IS_ALIGNED((uintptr_t)address, IMR_PAGE_SIZE));
	__ASSERT_NO_MSG(IS_ALIGNED(length, IMR_PAGE_SIZE));
	__ASSERT_NO_MSG(IS_IMR_ADDRESS(address));

	size_t slot_count = length / IMR_PAGE_SIZE;

	ret = sys_mem_blocks_get(&IMR_REGION, address, slot_count);

	return ret;
}

int intel_adsp_ddr_memory_allocate(size_t length, void **address)
{
	int ret;

	__ASSERT_NO_MSG(IS_ALIGNED((uintptr_t)length, IMR_PAGE_SIZE));
	__ASSERT_NO_MSG(address != NULL);

	size_t slot_count = length / IMR_PAGE_SIZE;

	ret = sys_mem_blocks_alloc_contiguous(&IMR_REGION, slot_count, address);

	return ret;
}

int intel_adsp_ddr_memory_free(size_t length, void *address)
{
	int ret;

	__ASSERT_NO_MSG(IS_ALIGNED((uintptr_t)address, IMR_PAGE_SIZE));
	__ASSERT_NO_MSG(IS_ALIGNED(length, IMR_PAGE_SIZE));
	__ASSERT_NO_MSG(IS_IMR_ADDRESS(address));

	size_t slot_count = length / IMR_PAGE_SIZE;

	ret = sys_mem_blocks_free_contiguous(&IMR_REGION, address, slot_count);

	return ret;
}
