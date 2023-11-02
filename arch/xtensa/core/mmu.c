/*
 * Copyright (c) 2023 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/arch/xtensa/xtensa_mmu.h>
#include <xtensa/corebits.h>
#include <xtensa_mmu_priv.h>

/* This ASID is shared between all domains and kernel. */
#define MMU_SHARED_ASID 255

/* Fixed data TLB way to map the page table */
#define MMU_PTE_WAY 7

/* Fixed data TLB way to map VECBASE */
#define MMU_VECBASE_WAY 8

void xtensa_init_paging(uint32_t *l1_page)
{
	volatile uint8_t entry;
	uint32_t ps, vecbase;

	/* Set the page table location in the virtual address */
	xtensa_ptevaddr_set((void *)Z_XTENSA_PTEVADDR);

	/* Set rasid */
	xtensa_rasid_asid_set(MMU_SHARED_ASID, Z_XTENSA_SHARED_RING);

	/* Next step is to invalidate the tlb entry that contains the top level
	 * page table. This way we don't cause a multi hit exception.
	 */
	xtensa_dtlb_entry_invalidate_sync(Z_XTENSA_TLB_ENTRY(Z_XTENSA_PAGE_TABLE_VADDR, 6));
	xtensa_itlb_entry_invalidate_sync(Z_XTENSA_TLB_ENTRY(Z_XTENSA_PAGE_TABLE_VADDR, 6));

	/* We are not using a flat table page, so we need to map
	 * only the top level page table (which maps the page table itself).
	 *
	 * Lets use one of the wired entry, so we never have tlb miss for
	 * the top level table.
	 */
	xtensa_dtlb_entry_write(Z_XTENSA_PTE((uint32_t)l1_page,
					     Z_XTENSA_KERNEL_RING, Z_XTENSA_MMU_CACHED_WT),
			Z_XTENSA_TLB_ENTRY(Z_XTENSA_PAGE_TABLE_VADDR, MMU_PTE_WAY));

	/* Before invalidate the text region in the TLB entry 6, we need to
	 * map the exception vector into one of the wired entries to avoid
	 * a page miss for the exception.
	 */
	__asm__ volatile("rsr.vecbase %0" : "=r"(vecbase));

	xtensa_itlb_entry_write_sync(
		Z_XTENSA_PTE(vecbase, Z_XTENSA_KERNEL_RING,
			Z_XTENSA_MMU_X | Z_XTENSA_MMU_CACHED_WT),
		Z_XTENSA_TLB_ENTRY(
			Z_XTENSA_PTEVADDR + MB(4), 3));

	xtensa_dtlb_entry_write_sync(
		Z_XTENSA_PTE(vecbase, Z_XTENSA_KERNEL_RING,
			Z_XTENSA_MMU_X | Z_XTENSA_MMU_CACHED_WT),
		Z_XTENSA_TLB_ENTRY(
			Z_XTENSA_PTEVADDR + MB(4), 3));

	/* Temporarily uses KernelExceptionVector for level 1 interrupts
	 * handling. This is due to UserExceptionVector needing to jump to
	 * _Level1Vector. The jump ('j') instruction offset is incorrect
	 * when we move VECBASE below.
	 */
	__asm__ volatile("rsr.ps %0" : "=r"(ps));
	ps &= ~PS_UM;
	__asm__ volatile("wsr.ps %0; rsync" :: "a"(ps));

	__asm__ volatile("wsr.vecbase %0; rsync\n\t"
			:: "a"(Z_XTENSA_PTEVADDR + MB(4)));


	/* Finally, lets invalidate all entries in way 6 as the page tables
	 * should have already mapped the regions we care about for boot.
	 */
	for (entry = 0; entry < BIT(XCHAL_ITLB_ARF_ENTRIES_LOG2); entry++) {
		__asm__ volatile("iitlb %[idx]\n\t"
				 "isync"
				 :: [idx] "a"((entry << 29) | 6));
	}

	for (entry = 0; entry < BIT(XCHAL_DTLB_ARF_ENTRIES_LOG2); entry++) {
		__asm__ volatile("idtlb %[idx]\n\t"
				 "dsync"
				 :: [idx] "a"((entry << 29) | 6));
	}

	/* Map VECBASE to a fixed data TLB */
	xtensa_dtlb_entry_write(
			Z_XTENSA_PTE((uint32_t)vecbase,
				     Z_XTENSA_KERNEL_RING, Z_XTENSA_MMU_CACHED_WB),
			Z_XTENSA_TLB_ENTRY((uint32_t)vecbase, MMU_VECBASE_WAY));

	/*
	 * Pre-load TLB for vecbase so exception handling won't result
	 * in TLB miss during boot, and that we can handle single
	 * TLB misses.
	 */
	xtensa_itlb_entry_write_sync(
		Z_XTENSA_PTE(vecbase, Z_XTENSA_KERNEL_RING,
			Z_XTENSA_MMU_X | Z_XTENSA_MMU_CACHED_WT),
		Z_XTENSA_AUTOFILL_TLB_ENTRY(vecbase));

	/* To finish, just restore vecbase and invalidate TLB entries
	 * used to map the relocated vecbase.
	 */
	__asm__ volatile("wsr.vecbase %0; rsync\n\t"
			:: "a"(vecbase));

	/* Restore PS_UM so that level 1 interrupt handling will go to
	 * UserExceptionVector.
	 */
	__asm__ volatile("rsr.ps %0" : "=r"(ps));
	ps |= PS_UM;
	__asm__ volatile("wsr.ps %0; rsync" :: "a"(ps));

	xtensa_dtlb_entry_invalidate_sync(Z_XTENSA_TLB_ENTRY(Z_XTENSA_PTEVADDR + MB(4), 3));
	xtensa_itlb_entry_invalidate_sync(Z_XTENSA_TLB_ENTRY(Z_XTENSA_PTEVADDR + MB(4), 3));

	/*
	 * Clear out THREADPTR as we use it to indicate
	 * whether we are in user mode or not.
	 */
	XTENSA_WUR("THREADPTR", 0);
}

void xtensa_set_paging(uint32_t asid, uint32_t *l1_page)
{
}

void xtensa_invalidate_refill_tlb(void)
{
}
