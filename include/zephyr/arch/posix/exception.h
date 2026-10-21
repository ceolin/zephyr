/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 * Copyright (c) 2017 Oticon A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_ARCH_POSIX_EXCEPTION_H_
#define ZEPHYR_INCLUDE_ARCH_POSIX_EXCEPTION_H_

#ifndef _ASMLANGUAGE
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct arch_esf {
#if defined(CONFIG_USERSPACE)
	/* Faulting address, 0 if the error was not a host memory fault */
	uintptr_t fault_addr;
	/* Program counter and stack pointer where the error happened */
	uintptr_t pc;
	uintptr_t sp;
	/* Host signal and signal code for host memory faults, 0 otherwise */
	int host_sig;
	int host_code;
#else
	uint32_t dummy; /*maybe we will want to add something someday*/
#endif
};

#ifdef __cplusplus
}
#endif

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_POSIX_EXCEPTION_H_ */
