/*
 * Copyright (c) 2020 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_ARCH_GDBSTUB_H_
#define ZEPHYR_INCLUDE_ARCH_GDBSTUB_H_

#include <inttypes.h>

#if defined(CONFIG_X86)
#include <arch/x86/ia32/gdbstub.h>
#endif

/**
 * @brief Architecture layer debug start
 *
 * This function is called by @c gdb_init()
 */
void arch_gdb_init(void);

/**
 * @brief Continue running program
 *
 * Continue software execution.
 */
void arch_gdb_continue(void);

/**
 * @brief Continue with one step
 *
 * Continue software execution until reaches the next statement.
 */
void arch_gdb_step(void);


#endif /* ZEPHYR_INCLUDE_ARCH_GDBSTUB_SYS_H_ */
