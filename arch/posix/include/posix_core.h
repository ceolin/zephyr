/*
 * Copyright (c) 2017 Oticon A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_ARCH_POSIX_INCLUDE_POSIX_CORE_H_
#define ZEPHYR_ARCH_POSIX_INCLUDE_POSIX_CORE_H_

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif


/* The per thread status of this architecture is kept in the thread object */
typedef struct _thread_arch posix_thread_status_t;


void posix_irq_check_idle_exit(void);
void posix_arch_init(void);
void posix_arch_clean_up(void);
void posix_swap(int next_allowed_thread_nbr, int this_th_nbr);
void posix_main_thread_start(int next_allowed_thread_nbr);
int posix_new_thread(void *payload);
void posix_abort_thread(int thread_idx);
int posix_arch_get_unique_thread_id(int thread_idx);
int posix_arch_thread_name_set(int thread_idx, const char *str);
void posix_arch_get_thread_stack(int thread_idx, void **stack_addr, unsigned long *stack_size);

#ifndef POSIX_ARCH_DEBUG_PRINTS
#define POSIX_ARCH_DEBUG_PRINTS 0
#endif

#if POSIX_ARCH_DEBUG_PRINTS
#define PC_DEBUG(fmt, ...) posix_print_trace("POSIX arch core:" fmt, __VA_ARGS__)
#else
#define PC_DEBUG(...)
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_ARCH_POSIX_INCLUDE_POSIX_CORE_H_ */
