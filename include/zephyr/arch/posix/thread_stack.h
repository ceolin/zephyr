/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief POSIX architecture thread stack layout
 *
 * Kernel threads run on their host pthread stack, so their Zephyr stack is
 * not used as call stack and does not need any special layout.
 *
 * User mode capable stacks are used: a thread in user mode runs on the
 * stack buffer, and its system calls run on a privileged stack placed in a
 * header before the buffer:
 *
 * High memory addresses
 * +-----------------------------------------+
 * | Thread stack buffer (page multiple)     |
 * +-----------------------------------------+
 * | Privileged stack (page multiple)        |
 * |   (CONFIG_PRIVILEGED_STACK_SIZE)        |
 * +-----------------------------------------+
 * | Guard page                              |
 * +-----------------------------------------+
 * Low memory addresses
 *
 * The protection is done with the host page permissions, so all parts are
 * page aligned. While the thread runs in user mode, only the stack buffer is
 * accessible. While it runs a system call, the guard page is made
 * inaccessible to catch privileged stack overflows.
 */

#ifndef ZEPHYR_INCLUDE_ARCH_POSIX_THREAD_STACK_H_
#define ZEPHYR_INCLUDE_ARCH_POSIX_THREAD_STACK_H_

#if defined(CONFIG_USERSPACE) && !defined(_ASMLANGUAGE)

#include <zephyr/sys/util.h>

#define Z_POSIX_PAGE_SIZE CONFIG_ARCH_POSIX_PAGE_SIZE

struct z_posix_thread_stack_header {
	char guard_page[Z_POSIX_PAGE_SIZE];
	char privilege_stack[ROUND_UP(CONFIG_PRIVILEGED_STACK_SIZE, Z_POSIX_PAGE_SIZE)];
} __packed __aligned(Z_POSIX_PAGE_SIZE);

#define ARCH_THREAD_STACK_RESERVED sizeof(struct z_posix_thread_stack_header)

#define ARCH_THREAD_STACK_OBJ_ALIGN(size) Z_POSIX_PAGE_SIZE

/* size includes ARCH_THREAD_STACK_RESERVED */
#define ARCH_THREAD_STACK_SIZE_ADJUST(size)                                                        \
	ROUND_UP(MAX((size), CONFIG_ARCH_POSIX_USER_STACK_MIN_SIZE + ARCH_THREAD_STACK_RESERVED),  \
		 Z_POSIX_PAGE_SIZE)

#endif /* CONFIG_USERSPACE && !_ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_POSIX_THREAD_STACK_H_ */
