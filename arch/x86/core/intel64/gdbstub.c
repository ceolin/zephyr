/*
 * Copyright (c) 2020 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel.h>
#include <kernel_internal.h>
#include <ia32/exception.h>
#include <inttypes.h>
#include <debug/gdbstub.h>


static struct gdb_ctx ctx;
static bool start;

/**
 * Currently we just handle vectors 1 and 3 but lets keep it generic
 * to be able to notify other exceptions in the future
 *
 * TODO: Move it to a common file
 */
static unsigned int get_exception(unsigned int vector)
{
	unsigned int exception;

	switch (vector) {
	case IV_DIVIDE_ERROR:
		exception = GDB_EXCEPTION_DIVIDE_ERROR;
		break;
	case IV_DEBUG:
		exception = GDB_EXCEPTION_BREAKPOINT;
		break;
	case IV_BREAKPOINT:
		exception = GDB_EXCEPTION_BREAKPOINT;
		break;
	case IV_OVERFLOW:
		exception = GDB_EXCEPTION_OVERFLOW;
		break;
	case IV_BOUND_RANGE:
		exception = GDB_EXCEPTION_OVERFLOW;
		break;
	case IV_INVALID_OPCODE:
		exception = GDB_EXCEPTION_INVALID_INSTRUCTION;
		break;
	case IV_DEVICE_NOT_AVAILABLE:
		exception = GDB_EXCEPTION_DIVIDE_ERROR;
		break;
	case IV_DOUBLE_FAULT:
		exception = GDB_EXCEPTION_MEMORY_FAULT;
		break;
	case IV_COPROC_SEGMENT_OVERRUN:
		exception = GDB_EXCEPTION_INVALID_MEMORY;
		break;
	case IV_INVALID_TSS:
		exception = GDB_EXCEPTION_INVALID_MEMORY;
		break;
	case IV_SEGMENT_NOT_PRESENT:
		exception = GDB_EXCEPTION_INVALID_MEMORY;
		break;
	case IV_STACK_FAULT:
		exception = GDB_EXCEPTION_INVALID_MEMORY;
		break;
	case IV_GENERAL_PROTECTION:
		exception = GDB_EXCEPTION_INVALID_MEMORY;
		break;
	case IV_PAGE_FAULT:
		exception = GDB_EXCEPTION_INVALID_MEMORY;
		break;
	case IV_X87_FPU_FP_ERROR:
		exception = GDB_EXCEPTION_MEMORY_FAULT;
		break;
	default:
		exception = GDB_EXCEPTION_MEMORY_FAULT;
		break;
	}

	return exception;
}

/*
 * Debug exception handler.
 */
void z_gdb_interrupt(z_arch_esf_t *esf)
{
	/* TODO add remaining registers look intel64/arch.h */
	ctx.exception = get_exception(esf->vector);

	ctx.registers[GDB_RAX] = esf->rax;
	ctx.registers[GDB_RCX] = esf->rcx;
	ctx.registers[GDB_RDX] = esf->rdx;
	ctx.registers[GDB_RBX] = esf->rbx;
	ctx.registers[GDB_RSP] = esf->rsp;
	ctx.registers[GDB_RBP] = esf->rbp;
	ctx.registers[GDB_RSI] = esf->rsi;
	ctx.registers[GDB_RDI] = esf->rdi;
	ctx.registers[GDB_PC] = esf->rip;
	ctx.registers[GDB_CS] = esf->cs;
	ctx.registers[GDB_RFLAGS]  = esf->rflags;
	ctx.registers[GDB_SS] = esf->ss;

	z_gdb_main_loop(&ctx, start);
	start = false;

	esf->rax = ctx.registers[GDB_RAX];
	esf->rcx = ctx.registers[GDB_RCX];
	esf->rdx = ctx.registers[GDB_RDX];
	esf->rbx = ctx.registers[GDB_RBX];
	esf->rsp = ctx.registers[GDB_RSP];
	esf->rbp = ctx.registers[GDB_RBP];
	esf->rsi = ctx.registers[GDB_RSI];
	esf->rdi = ctx.registers[GDB_RDI];
	esf->rip = ctx.registers[GDB_PC];
	esf->cs = ctx.registers[GDB_CS];
	esf->rflags = ctx.registers[GDB_RFLAGS];
	esf->ss = ctx.registers[GDB_SS];
}

void arch_gdb_continue(void)
{
	/* Clear the TRAP FLAG bit */
	ctx.registers[GDB_RFLAGS] &= ~BIT(8);
}

void arch_gdb_step(void)
{
	/* Set the TRAP FLAG bit */
	ctx.registers[GDB_RFLAGS] |= BIT(8);
}

void arch_gdb_init(void)
{
	start = true;
	__asm__ volatile ("int3");
}

