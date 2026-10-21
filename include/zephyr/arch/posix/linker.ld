/*
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2017 Oticon A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Linker command/script file
 *
 * Linker script for the POSIX (native) platform
 */

#include <zephyr/linker/sections.h>

#include <zephyr/linker/linker-defs.h>
#include <zephyr/linker/linker-tool.h>

#ifdef CONFIG_USERSPACE
/* User mode memory protection is done with the host page permissions */
#define APP_SHARED_ALIGN . = ALIGN(CONFIG_ARCH_POSIX_PAGE_SIZE)
#define SMEM_PARTITION_ALIGN(size) . = ALIGN(CONFIG_ARCH_POSIX_PAGE_SIZE)
/* At least the alignment of the generated kernel object table sections */
#define Z_POSIX_KOBJECT_ALIGN 64
#endif

SECTIONS
{
#include <zephyr/linker/llext-sections.ld>

SECTION_PROLOGUE(rom_start,,)
{
	/* Located in generated directory. This file is populated by the
	 * zephyr_linker_sources() Cmake function.
	 */
	#include <snippets-rom-start.ld>
} GROUP_LINK_IN(ROMABLE_REGION)

#include <zephyr/linker/common-rom.ld>
/* Located in generated directory. This file is populated by calling
 * zephyr_linker_sources(ROM_SECTIONS ...). Useful for grouping iterable RO structs.
 */
#include <snippets-rom-sections.ld>

#ifdef CONFIG_USERSPACE
/*
 * The kernel object lookup table is generated from a provisional executable
 * and linked in the final one: the sections reserving room for it must have
 * the same size, alignment, type and flags in both (see
 * scripts/build/gen_native_userspace_ld.py). The anchor
 * makes this section read-only data even when it only holds placeholders.
 */
SECTION_PROLOGUE(_RODATA_SECTION_NAME,,ALIGN(Z_POSIX_KOBJECT_ALIGN))
{
	KEEP(*(.posix_rodata_anchor))
#else
SECTION_PROLOGUE(_RODATA_SECTION_NAME,,)
{
#endif
	/* Located in generated directory. This file is populated by the
	 * zephyr_linker_sources() Cmake function.
	 */
	#include <snippets-rodata.ld>
#include <zephyr/linker/kobject-rom.ld>
} GROUP_LINK_IN(ROMABLE_REGION)

#ifdef CONFIG_USERSPACE
/*
 * With user mode, all the RAM of the image is gathered in the final link of the
 * native simulator executable, so it can be protected (see
 * scripts/build/gen_native_userspace_ld.py).
 * Claim here the image data and bss, which otherwise would be mixed with the
 * host ones in the final link.
 *
 * Data which is read-only after relocation (from -fPIC objects) keeps a
 * dotted name, so it goes with the host one to the read-only segment, which
 * is accessible in user mode.
 */
SECTION_DATA_PROLOGUE(.data.rel.ro.zephyr,,)
{
	*(.data.rel.ro.local*)
	*(.data.rel.ro .data.rel.ro.*)
} GROUP_DATA_LINK_IN(RAMABLE_REGION, ROMABLE_REGION)

/* The partitions offsets within the section must be page aligned */
. = ALIGN(CONFIG_ARCH_POSIX_PAGE_SIZE);
#include <app_smem.ld>
_app_smem_size = _app_smem_end - _app_smem_start;
#endif /* CONFIG_USERSPACE */

SECTION_DATA_PROLOGUE(_DATA_SECTION_NAME,,)
{
#ifdef CONFIG_USERSPACE
	*(.data)
	*(".data.*")
#endif
	/* Located in generated directory. This file is populated by the
	 * zephyr_linker_sources() Cmake function.
	 */
	#include <snippets-rwdata.ld>
} GROUP_DATA_LINK_IN(RAMABLE_REGION, ROMABLE_REGION)

.note.GNU-stack :
{
  KEEP(*(.note.GNU-stack))
}

#include <zephyr/linker/common-ram.ld>

/* Located in generated directory. This file is populated by the
 * zephyr_linker_sources() Cmake function.
 */
#include <snippets-ram-sections.ld>

#include <zephyr/arch/posix/native_tasks.ld>

#ifdef CONFIG_USERSPACE
. = ALIGN(Z_POSIX_KOBJECT_ALIGN);
#endif
#include <zephyr/linker/kobject-data.ld>

/* Located in generated directory. This file is populated by the
 * zephyr_linker_sources() Cmake function.
 */
#include <snippets-data-sections.ld>

__data_region_end = .;

#ifdef CONFIG_USERSPACE
SECTION_PROLOGUE(_BSS_SECTION_NAME,(NOLOAD),)
{
	*(.bss)
	*(".bss.*")
	*(COMMON)
	/* Located in generated directory. This file is populated by the
	 * zephyr_linker_sources() Cmake function.
	 */
	#include <snippets-bss.ld>
} GROUP_NOLOAD_LINK_IN(RAMABLE_REGION, ROMABLE_REGION)

/* Includes the user mode stacks */
#include <zephyr/linker/common-noinit.ld>
#else
SECTION_DATA_PROLOGUE(_NOINIT_SECTION_NAME,,)
{
	/* Located in generated directory. This file is populated by the
	 * zephyr_linker_sources() Cmake function.
	 */
	#include <snippets-noinit.ld>
} GROUP_LINK_IN(RAMABLE_REGION)
#endif /* CONFIG_USERSPACE */

/* Located in generated directory. This file is populated by the
 * zephyr_linker_sources() Cmake function.
 */
#include <snippets-sections.ld>

} INSERT AFTER .data;

#ifdef CONFIG_USERSPACE
SECTIONS
{
	/* Room for the generated kernel object lookup code, so the size of the
	 * text does not change in the final link.
	 */
	SECTION_PROLOGUE(kobject_text,,ALIGN(16))
	{
#include <zephyr/linker/kobject-text.ld>
	} GROUP_LINK_IN(ROMABLE_REGION)
} INSERT AFTER .text;
#endif /* CONFIG_USERSPACE */

#include <zephyr/arch/posix/native_sim_interface.ld>

/*
 * Note that the INSERT command actually changes the meaning of the -T command
 * line switch: The script will now augment the default SECTIONS instead of
 * replacing it.
 */
