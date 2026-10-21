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
#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_USERSPACE)
/* Size of the opaque host state kept in each thread object */
#define POSIX_ARCH_HOST_STATE_SIZE 256
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

#if defined(CONFIG_USERSPACE)
	/* Privileged stack, used for system calls once in user mode */
	uintptr_t priv_stack_start;
	size_t priv_stack_size;

	/* POSIX_THREAD_* flags */
	uint32_t flags;

	/* Host side state used to enter and leave user mode, only
	 * interpreted by the host side of the architecture.
	 */
	uint8_t host_state[POSIX_ARCH_HOST_STATE_SIZE] __aligned(16);
#endif
};

typedef struct _thread_arch _thread_arch_t;

#ifdef __cplusplus
}
#endif

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_POSIX_THREAD_H_ */
