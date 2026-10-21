/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief POSIX architecture specific syscall header
 *
 * This header contains the POSIX architecture specific syscall interface.
 * It is included by the syscall interface architecture-abstraction header
 * (include/zephyr/arch/syscall.h)
 *
 * There is no trap instruction: a system call is a function call which
 * restores the access to the kernel memory and switches to the privileged
 * stack of the calling thread.
 */

#ifndef ZEPHYR_INCLUDE_ARCH_POSIX_SYSCALL_H_
#define ZEPHYR_INCLUDE_ARCH_POSIX_SYSCALL_H_

#if defined(CONFIG_USERSPACE) && !defined(_ASMLANGUAGE)

#include <zephyr/types.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

uintptr_t z_posix_syscall(uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t arg4,
			  uintptr_t arg5, uintptr_t arg6, uintptr_t call_id);

int posix_arch_is_user_mode(void);

static inline uintptr_t arch_syscall_invoke6(uintptr_t arg1, uintptr_t arg2, uintptr_t arg3,
					     uintptr_t arg4, uintptr_t arg5, uintptr_t arg6,
					     uintptr_t call_id)
{
	return z_posix_syscall(arg1, arg2, arg3, arg4, arg5, arg6, call_id);
}

static inline uintptr_t arch_syscall_invoke5(uintptr_t arg1, uintptr_t arg2, uintptr_t arg3,
					     uintptr_t arg4, uintptr_t arg5, uintptr_t call_id)
{
	return z_posix_syscall(arg1, arg2, arg3, arg4, arg5, 0, call_id);
}

static inline uintptr_t arch_syscall_invoke4(uintptr_t arg1, uintptr_t arg2, uintptr_t arg3,
					     uintptr_t arg4, uintptr_t call_id)
{
	return z_posix_syscall(arg1, arg2, arg3, arg4, 0, 0, call_id);
}

static inline uintptr_t arch_syscall_invoke3(uintptr_t arg1, uintptr_t arg2, uintptr_t arg3,
					     uintptr_t call_id)
{
	return z_posix_syscall(arg1, arg2, arg3, 0, 0, 0, call_id);
}

static inline uintptr_t arch_syscall_invoke2(uintptr_t arg1, uintptr_t arg2, uintptr_t call_id)
{
	return z_posix_syscall(arg1, arg2, 0, 0, 0, 0, call_id);
}

static inline uintptr_t arch_syscall_invoke1(uintptr_t arg1, uintptr_t call_id)
{
	return z_posix_syscall(arg1, 0, 0, 0, 0, 0, call_id);
}

static inline uintptr_t arch_syscall_invoke0(uintptr_t call_id)
{
	return z_posix_syscall(0, 0, 0, 0, 0, 0, call_id);
}

static inline bool arch_is_user_context(void)
{
	/* The user mode flag is kept on the host side, so it can be read in
	 * both modes. Interrupts are only handled in kernel mode.
	 */
	return posix_arch_is_user_mode() != 0;
}

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_USERSPACE && !_ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_POSIX_SYSCALL_H_ */
