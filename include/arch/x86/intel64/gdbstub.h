/*
 * Copyright (c) 2020 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief INTEL64 specific gdbstub interface header
 */

#ifndef ZEPHYR_INCLUDE_ARCH_X86_INTEL64_GDBSTUB_SYS_H_
#define ZEPHYR_INCLUDE_ARCH_X86_INTEL64_GDBSTUB_SYS_H_

#ifndef _ASMLANGUAGE

#include <stdint.h>
#include <toolchain.h>

/**
 * @brief Number of register used by gdbstub in IA-64
 */
#define ARCH_GDB_NUM_REGISTERS 20

/**
 * @brief GDB interruption context
 *
 * The exception stack frame contents used by gdbstub. The contents
 * of this struct are used to display information about the current
 * cpu state.
 */

struct gdb_interrupt_ctx {
	uint64_t rbx;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;
	uint64_t rbp;
	uint64_t rax;
	uint64_t rcx;
	uint64_t rdx;
	uint64_t rsi;
	uint64_t rdi;
	uint64_t r8;
	uint64_t r9;
	uint64_t r10;
	uint64_t r11;
	uint64_t vector;
	uint64_t code;
	uint64_t rip;
	uint64_t cs;
	uint64_t rflags;
	uint64_t rsp;
	uint64_t ss;
} __packed;

/**
 * @brief IA-32 register used in gdbstub
 */

enum GDB_REGISTER {
	GDB_RAX,
	GDB_RCX,
	GDB_RDX,
	GDB_RBX,
	GDB_R8,
	GDB_R9,
	GDB_R10,
	GDB_R11,
	GDB_R12,
	GDB_R13,
	GDB_R14,
	GDB_R15,
	GDB_RSP,
	GDB_RBP,
	GDB_RSI,
	GDB_RDI,
	GDB_PC,
	GDB_RFLAGS,
	GDB_CS,
	GDB_SS,
};

struct gdb_ctx {
	unsigned int exception;
	unsigned int registers[ARCH_GDB_NUM_REGISTERS];
};

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_X86_INTEL64_GDBSTUB_SYS_H_ */
