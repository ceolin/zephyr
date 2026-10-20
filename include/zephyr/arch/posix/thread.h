/*
 * Copyright (c) 2017 Intel Corporation
 * Copyright (c) 2017 Oticon A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Per-arch thread definition
 *
 * This file contains definitions for
 *
 *  struct _thread_arch
 *  struct _callee_saved
 *
 * necessary to instantiate instances of struct k_thread.
 */

#ifndef ZEPHYR_INCLUDE_ARCH_POSIX_THREAD_H_
#define ZEPHYR_INCLUDE_ARCH_POSIX_THREAD_H_

#ifndef _ASMLANGUAGE
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct _callee_saved {
	/* IRQ status before irq_lock() and call to z_swap() */
	uint32_t key;

	/* Return value of z_swap() */
	uint32_t retval;

	/* Thread status pointer (points to the thread's struct _thread_arch) */
	void *thread_status;
};


struct _thread_arch {
	/* z_thread_entry() arguments */
	void (*entry_point)(void *p1, void *p2, void *p3);
	void *arg1;
	void *arg2;
	void *arg3;

#if defined(CONFIG_ARCH_POSIX_UPDATE_STACK_INFO)
	/* Original Zephyr-allocated stack info, saved before being
	 * overwritten with the real pthread stack bounds.
	 */
	uintptr_t zephyr_stack_start;
	size_t zephyr_stack_size;
#endif

	/* Index of the thread in the native simulator thread emulator */
	int thread_idx;

#if defined(CONFIG_ARCH_HAS_THREAD_ABORT)
	/* The kernel may indicate that a thread has been aborted several times */
	int aborted;
#endif
};

typedef struct _thread_arch _thread_arch_t;

#ifdef __cplusplus
}
#endif

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_POSIX_THREAD_H_ */
