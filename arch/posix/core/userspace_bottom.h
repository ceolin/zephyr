/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Interface between the Zephyr side of the POSIX architecture user mode
 * support (userspace.c) and its host side (userspace_bottom.c), which is
 * built with the host C library.
 *
 * This header must not include any Zephyr or host header.
 */

#ifndef ZEPHYR_ARCH_POSIX_CORE_USERSPACE_BOTTOM_H_
#define ZEPHYR_ARCH_POSIX_CORE_USERSPACE_BOTTOM_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Size of the opaque host state each thread has (struct _thread_arch) */
#define POSIX_HOST_STATE_SIZE 256

/* Maximum number of regions of a memory view */
#define POSIX_MEM_VIEW_MAX 64

/* Access permissions of a region (combinable) */
#define POSIX_PROT_NONE 0
#define POSIX_PROT_R    1
#define POSIX_PROT_W    2
#define POSIX_PROT_X    4

struct posix_mem_region {
	unsigned long start;
	unsigned long size;
	int prot;
};

struct posix_fault_info {
	unsigned long addr;
	unsigned long pc;
	unsigned long sp;
	int sig;
	int code;
};

/*
 * Initialize the user mode support. All memory between ram_start and ram_end
 * (page aligned) belongs to the Zephyr image and is the only memory whose
 * permissions are changed. Returns 0 on success, or -1 if the host page size
 * does not match page_size.
 */
int posix_arch_userspace_init(unsigned long ram_start, unsigned long ram_end,
			      unsigned long page_size);

/* Is the CPU running in user mode */
int posix_arch_is_user_mode(void);

/*
 * Drop to user mode: apply the memory view, switch to the stack
 * [stack, stack + stack_size) and call entry(arg).
 * host_state is the thread's opaque host state (POSIX_HOST_STATE_SIZE bytes).
 * This only returns (with the protection lifted) when the thread caused a
 * fault (invalid access or instruction) in user mode, which is then described
 * in *fault.
 */
void posix_arch_user_enter(void *host_state, void (*entry)(void *), void *arg, void *stack,
			   unsigned long stack_size, const struct posix_mem_region *view,
			   int n_regions, struct posix_fault_info *fault);

/*
 * Apply the user mode memory view of the thread with host state host_state,
 * and enter user mode. In the view, a later region takes precedence over the
 * previous ones, and memory not covered by it stays accessible.
 */
void posix_arch_mem_apply(void *host_state, const struct posix_mem_region *view, int n_regions);

/* Give access to the whole Zephyr image RAM and leave user mode */
void posix_arch_mem_lift(void);

/*
 * Make guard_page (in the Zephyr image RAM) inaccessible, as guard page of
 * the privileged stack of the thread with host state host_state. When the
 * access to the image RAM is given back with page permissions, the guard page
 * of the current thread is made inaccessible again.
 */
void posix_arch_guard_set(void *host_state, unsigned long guard_page);

/* Make the guard page of a thread inaccessible again, after it was blocked */
void posix_arch_guard_rearm(void *host_state);

/* Make the guard page of a thread accessible again, before it is reused */
void posix_arch_guard_release(void *host_state);

/* Call fn(arg) on the stack [stack, stack + stack_size) and return when it returns */
void posix_arch_run_on_stack(void (*fn)(void *), void *arg, void *stack, unsigned long stack_size);

/*
 * Get the part of the host pthread stack of a thread which dropped to user
 * mode which is not in use, where it can block.
 */
void posix_arch_swap_stack_get(void *host_state, void **stack, unsigned long *stack_size);

/*
 * Permissions of the host memory [start, start + size) (outside of the Zephyr
 * image RAM), or -1 if any part of it is not mapped.
 */
int posix_arch_host_range_prot(unsigned long start, unsigned long size);

/*
 * strnlen() which does not fault: on an invalid access *err is set to -1 and
 * 0 is returned.
 */
unsigned long posix_arch_strnlen_nofault(const char *s, unsigned long maxsize, int *err);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_ARCH_POSIX_CORE_USERSPACE_BOTTOM_H_ */
