/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief POSIX architecture error handling
 *
 * With user mode, an oops or panic raised from a user thread must enter the
 * kernel through a system call, as the kernel memory cannot be accessed from
 * user mode. The system call also prevents a user thread from raising a
 * kernel panic.
 */

#ifndef ZEPHYR_INCLUDE_ARCH_POSIX_POSIX_ARCH_ERROR_H_
#define ZEPHYR_INCLUDE_ARCH_POSIX_POSIX_ARCH_ERROR_H_

#if defined(CONFIG_USERSPACE) && !defined(_ASMLANGUAGE)

#include <zephyr/arch/posix/syscall.h>
#include <zephyr/arch/exception.h>
#include <zephyr/syscall_list.h>
#include <zephyr/toolchain.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_PRINTK) && !defined(CONFIG_ASSERT_NO_FILE_INFO)
#define Z_POSIX_EXCEPT_LOC() printk("@ %s:%d\n", __FILE__, __LINE__)
#else
#define Z_POSIX_EXCEPT_LOC()
#endif

/* As with the generic z_except_reason(), z_fatal_error() may return in kernel
 * mode: when the current thread is already being aborted, aborting it again
 * from the fatal error handler is a no-op.
 */
#define ARCH_EXCEPT(reason_p)                                                                      \
	do {                                                                                       \
		if (arch_is_user_context()) {                                                      \
			(void)arch_syscall_invoke1((uintptr_t)(reason_p),                          \
						   K_SYSCALL_POSIX_USER_FAULT);                    \
			CODE_UNREACHABLE; /* LCOV_EXCL_LINE */                                     \
		} else {                                                                           \
			Z_POSIX_EXCEPT_LOC();                                                      \
			z_fatal_error((reason_p), NULL);                                           \
		}                                                                                  \
	} while (false)

/**
 * @brief Raise a fatal error on behalf of a user mode thread
 *
 * @param reason Requested reason (K_ERR_*). A user thread can only raise
 *               K_ERR_KERNEL_OOPS or K_ERR_STACK_CHK_FAIL, other values are
 *               reported as K_ERR_KERNEL_OOPS.
 */
__syscall void posix_user_fault(unsigned int reason);

#include <zephyr/syscalls/posix_arch_error.h>

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_USERSPACE && !_ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_POSIX_POSIX_ARCH_ERROR_H_ */
