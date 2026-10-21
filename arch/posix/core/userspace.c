/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * User mode support for the POSIX architecture.
 *
 * There is no privilege level to switch to on the host. User mode is emulated
 * with the host memory protection instead (see userspace_bottom.c): while a
 * thread runs in user mode, the RAM of the Zephyr image is inaccessible,
 * except for the thread stack buffer and the partitions of its memory domain.
 * Text and read-only data stay accessible.
 *
 * A system call is a function call which gives the kernel access to the image
 * RAM back and runs the system call handler on the privileged stack of the
 * thread. The user mode view of the memory is rebuilt every time the thread
 * returns to user mode, so changes to memory domains need no extra handling.
 *
 * All context switches happen in kernel mode, with access to the whole image
 * RAM. A thread which dropped to user mode blocks on the part of its host
 * pthread stack which is not in use: if it is aborted while blocked, the host
 * thread exits from there without touching the Zephyr image RAM, whose
 * permissions may meanwhile have been changed by another thread.
 *
 * Memory outside of the image RAM (host C library state, host heap, the
 * native simulator runner) is not protected. This is a development aid, not a
 * security boundary.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/internal/syscall_handler.h>
#include <zephyr/linker/linker-defs.h>
#include <zephyr/arch/posix/posix_soc_if.h>
#include <zephyr/logging/log.h>
#include <kernel_internal.h>

#include "posix_core.h"
#include "userspace_bottom.h"

LOG_MODULE_DECLARE(os, CONFIG_KERNEL_LOG_LEVEL);

BUILD_ASSERT(sizeof(((struct _thread_arch *)0)->host_state) == POSIX_HOST_STATE_SIZE);

#define PAGE_SIZE CONFIG_ARCH_POSIX_PAGE_SIZE

/* Image RAM, the stack, and one region per partition */
#define VIEW_MAX (CONFIG_MAX_DOMAIN_PARTITIONS + 2)
BUILD_ASSERT(VIEW_MAX <= POSIX_MEM_VIEW_MAX, "CONFIG_MAX_DOMAIN_PARTITIONS is too large");

struct user_entry_args {
	k_thread_entry_t entry;
	void *p1;
	void *p2;
	void *p3;
};

struct syscall_args {
	uintptr_t arg[6];
	uintptr_t call_id;
	uintptr_t ret;
	uintptr_t pc;
	uintptr_t sp;
};

struct fatal_args {
	unsigned int reason;
	const struct arch_esf *esf;
};

struct swap_args {
	int next_thread_idx;
	int this_thread_idx;
};

/* Keeps the image read-only data section type the same in all link passes,
 * see include/zephyr/arch/posix/linker.ld
 */
static const uint8_t rodata_anchor __used Z_GENERIC_SECTION(.posix_rodata_anchor);

static bool userspace_ready;

static void userspace_init_once(void)
{
	if (userspace_ready) {
		return;
	}

	if (posix_arch_userspace_init((unsigned long)_image_ram_start,
				      (unsigned long)_image_ram_end, PAGE_SIZE) != 0) {
		posix_print_error_and_exit("POSIX arch: the host page size is not %d\n", PAGE_SIZE);
	}
	userspace_ready = true;
}

static int attr_to_prot(k_mem_partition_attr_t attr)
{
	int prot = POSIX_PROT_NONE;

	if ((attr.attrs & Z_POSIX_MEM_U_R) != 0U) {
		prot |= POSIX_PROT_R;
	}
	if ((attr.attrs & Z_POSIX_MEM_U_W) != 0U) {
		prot |= POSIX_PROT_W;
	}
	if ((attr.attrs & Z_POSIX_MEM_U_X) != 0U) {
		prot |= POSIX_PROT_X;
	}

	return prot;
}

/* Add the page aligned part of [start, start + size) inside the image RAM to the view */
static void view_add(struct posix_mem_region *view, int *n, uintptr_t start, size_t size, int prot)
{
	uintptr_t ram_start = (uintptr_t)_image_ram_start;
	uintptr_t ram_end = (uintptr_t)_image_ram_end;
	uintptr_t end = ROUND_UP(start + size, PAGE_SIZE);

	start = ROUND_DOWN(start, PAGE_SIZE);
	start = MAX(start, ram_start);
	end = MIN(end, ram_end);
	if (start >= end) {
		return;
	}

	view[*n].start = start;
	view[*n].size = end - start;
	view[*n].prot = prot;
	(*n)++;
}

/* Build the user mode view of the memory of thread */
static int view_build(struct k_thread *thread, struct posix_mem_region *view)
{
	struct k_mem_domain *domain = thread->mem_domain_info.mem_domain;
	k_spinlock_key_t key;
	int n = 0;

	view_add(view, &n, (uintptr_t)_image_ram_start,
		 (uintptr_t)_image_ram_end - (uintptr_t)_image_ram_start, POSIX_PROT_NONE);

	key = k_spin_lock(&z_mem_domain_lock);
	for (int i = 0; i < CONFIG_MAX_DOMAIN_PARTITIONS; i++) {
		struct k_mem_partition *part = &domain->partitions[i];

		if (part->size != 0U) {
			view_add(view, &n, part->start, part->size, attr_to_prot(part->attr));
		}
	}
	k_spin_unlock(&z_mem_domain_lock, key);

	view_add(view, &n, thread->stack_info.start, thread->stack_info.size,
		 POSIX_PROT_R | POSIX_PROT_W);

	return n;
}

static struct z_posix_thread_stack_header *stack_header(struct k_thread *thread)
{
	return (struct z_posix_thread_stack_header *)thread->stack_obj;
}

static void return_to_user(struct k_thread *thread)
{
	struct posix_mem_region view[VIEW_MAX];
	int n = view_build(thread, view);

	posix_arch_mem_apply(thread->arch.host_state, view, n);
}

static void run_on_priv_stack(struct k_thread *thread, void (*fn)(void *), void *arg)
{
	posix_arch_run_on_stack(fn, arg, (void *)thread->arch.priv_stack_start,
				thread->arch.priv_stack_size);
}

static void fatal_on_priv_stack(void *arg)
{
	struct fatal_args *args = arg;

	z_fatal_error(args->reason, args->esf);
	/* The faulting thread was aborted */
	CODE_UNREACHABLE;
}

static FUNC_NORETURN void user_fault_from_host(struct k_thread *thread,
					       const struct posix_fault_info *fault)
{
	uintptr_t stack_start = thread->stack_info.start;
	struct arch_esf esf = {
		.fault_addr = fault->addr,
		.pc = fault->pc,
		.sp = fault->sp,
		.host_sig = fault->sig,
		.host_code = fault->code,
	};
	struct fatal_args args = {
		.reason = K_ERR_CPU_EXCEPTION,
		.esf = &esf,
	};

	/* A stack pointer outside of the stack buffer means a stack overflow */
	if ((esf.sp != 0U) &&
	    ((esf.sp < stack_start) || (esf.sp - stack_start >= thread->stack_info.size))) {
		args.reason = K_ERR_STACK_CHK_FAIL;
	}

	EXCEPTION_DUMP("Fault in user mode (host signal %d, code %d)", esf.host_sig, esf.host_code);
	EXCEPTION_DUMP("Faulting address: 0x%lx, pc: 0x%lx, sp: 0x%lx",
		       (unsigned long)esf.fault_addr, (unsigned long)esf.pc, (unsigned long)esf.sp);

	run_on_priv_stack(thread, fatal_on_priv_stack, &args);
	CODE_UNREACHABLE;
}

static void user_entry(void *arg)
{
	struct user_entry_args *args = arg;

	z_thread_entry(args->entry, args->p1, args->p2, args->p3);
}

FUNC_NORETURN void arch_user_mode_enter(k_thread_entry_t user_entry_fn, void *p1, void *p2,
					void *p3)
{
	struct k_thread *thread = _current;
	struct z_posix_thread_stack_header *hdr = stack_header(thread);
	/* These stay valid while the thread runs: this function never returns,
	 * and its frame is in the host pthread stack, which user mode can access.
	 */
	struct user_entry_args args = {
		.entry = user_entry_fn,
		.p1 = p1,
		.p2 = p2,
		.p3 = p3,
	};
	struct posix_mem_region view[VIEW_MAX];
	struct posix_fault_info fault;
	uintptr_t stack_top;
	int n;

	userspace_init_once();

	thread->arch.priv_stack_start = (uintptr_t)hdr->privilege_stack;
	thread->arch.priv_stack_size = sizeof(hdr->privilege_stack);
#ifdef CONFIG_INIT_STACKS
	(void)memset(hdr->privilege_stack, 0xaa, sizeof(hdr->privilege_stack));
#endif
	posix_arch_guard_set(thread->arch.host_state, (unsigned long)hdr->guard_page);
	thread->arch.flags |= POSIX_THREAD_ENTERED_USER;

	stack_top = Z_STACK_PTR_ALIGN(thread->stack_info.start + thread->stack_info.size -
				      thread->stack_info.delta);

	n = view_build(thread, view);

	/* User mode always runs with interrupts enabled */
	posix_irq_full_unlock();

	posix_arch_user_enter(thread->arch.host_state, user_entry, &args,
			      (void *)thread->stack_info.start,
			      stack_top - thread->stack_info.start, view, n, &fault);

	/* Only reached after a fault in user mode */
	user_fault_from_host(thread, &fault);
}

static void syscall_on_priv_stack(void *arg)
{
	struct syscall_args *args = arg;
	struct arch_esf ssf = {
		.pc = args->pc,
		.sp = args->sp,
	};
	uintptr_t call_id = args->call_id;
	uintptr_t arg1 = args->arg[0];

	if (call_id >= K_SYSCALL_LIMIT) {
		arg1 = call_id;
		call_id = K_SYSCALL_BAD;
	}

	args->ret = _k_syscall_table[call_id](arg1, args->arg[1], args->arg[2], args->arg[3],
					      args->arg[4], args->arg[5], &ssf);
}

uintptr_t z_posix_syscall(uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t arg4,
			  uintptr_t arg5, uintptr_t arg6, uintptr_t call_id)
{
	struct syscall_args args = {
		.arg = {arg1, arg2, arg3, arg4, arg5, arg6},
		.call_id = call_id,
		.pc = (uintptr_t)__builtin_return_address(0),
		.sp = (uintptr_t)__builtin_frame_address(0),
	};

	/* Nothing in the kernel memory can be accessed before this */
	posix_arch_mem_lift();

	run_on_priv_stack(_current, syscall_on_priv_stack, &args);

	return_to_user(_current);

	return args.ret;
}

static void swap_on_host_stack(void *arg)
{
	struct swap_args *args = arg;

	posix_swap(args->next_thread_idx, args->this_thread_idx);
}

void posix_userspace_swap(posix_thread_status_t *this_thread, int next_thread_idx)
{
	struct swap_args args = {
		.next_thread_idx = next_thread_idx,
		.this_thread_idx = this_thread->thread_idx,
	};
	void *stack;
	unsigned long stack_size;

	if ((this_thread->flags & POSIX_THREAD_ENTERED_USER) == 0U) {
		posix_swap(next_thread_idx, this_thread->thread_idx);
		return;
	}

	posix_arch_swap_stack_get(this_thread->host_state, &stack, &stack_size);
	posix_arch_run_on_stack(swap_on_host_stack, &args, stack, stack_size);

	/* Other threads may have made the guard page accessible meanwhile */
	posix_arch_guard_rearm(this_thread->host_state);
}

void posix_userspace_thread_abort(struct k_thread *thread)
{
	if ((thread->arch.flags & POSIX_THREAD_ENTERED_USER) != 0U) {
		/* The stack object may be reused for something else */
		posix_arch_guard_release(thread->arch.host_state);
	}
}

FUNC_NORETURN void arch_syscall_oops(void *ssf)
{
	z_fatal_error(K_ERR_KERNEL_OOPS, ssf);
	CODE_UNREACHABLE;
}

void z_impl_posix_user_fault(unsigned int reason)
{
	if (((_current->base.user_options & K_USER) != 0U) && (reason != K_ERR_STACK_CHK_FAIL)) {
		reason = K_ERR_KERNEL_OOPS;
	}

	z_fatal_error(reason, _current->syscall_frame);
	CODE_UNREACHABLE;
}

static inline void z_vrfy_posix_user_fault(unsigned int reason)
{
	z_impl_posix_user_fault(reason);
}
#include <zephyr/syscalls/posix_user_fault_mrsh.c>

static bool is_within(uintptr_t start, size_t size, uintptr_t outer_start, size_t outer_size)
{
	return (start >= outer_start) && (size <= outer_size) &&
	       (start - outer_start <= outer_size - size);
}

int arch_buffer_validate(const void *addr, size_t size, int write)
{
	uintptr_t start = (uintptr_t)addr;
	struct k_mem_domain *domain = _current->mem_domain_info.mem_domain;
	int needed = write ? (POSIX_PROT_R | POSIX_PROT_W) : POSIX_PROT_R;
	int ret = -1;
	bool matched = false;
	k_spinlock_key_t key;
	int prot;

	if (size == 0U) {
		return 0;
	}
	if (start + size < start) {
		return -1;
	}

	/* The thread stack buffer */
	if (is_within(start, size, _current->stack_info.start, _current->stack_info.size)) {
		return 0;
	}

	/* The partitions of the thread memory domain */
	key = k_spin_lock(&z_mem_domain_lock);
	for (int i = 0; i < CONFIG_MAX_DOMAIN_PARTITIONS; i++) {
		struct k_mem_partition *part = &domain->partitions[i];

		if ((part->size == 0U) || !is_within(start, size, part->start, part->size)) {
			continue;
		}
		matched = true;
		if ((attr_to_prot(part->attr) & needed) == needed) {
			ret = 0;
		}
		break;
	}
	k_spin_unlock(&z_mem_domain_lock, key);

	if (matched) {
		return ret;
	}

	/* Anything else in the image RAM belongs to the kernel */
	if ((start < (uintptr_t)_image_ram_end) && (start + size > (uintptr_t)_image_ram_start)) {
		return -1;
	}

	/* Host memory (text, read-only data, host heap...): what the host allows */
	prot = posix_arch_host_range_prot(start, size);
	if ((prot < 0) || ((prot & needed) != needed)) {
		return -1;
	}

	return 0;
}

size_t arch_user_string_nlen(const char *s, size_t maxsize, int *err_arg)
{
	size_t len = 0;

	*err_arg = 0;

	while (len < maxsize) {
		uintptr_t addr = (uintptr_t)s + len;
		size_t chunk = MIN(PAGE_SIZE - (addr & (PAGE_SIZE - 1)), maxsize - len);
		size_t n;
		int err;

		if (arch_buffer_validate((const void *)addr, chunk, 0) != 0) {
			*err_arg = -1;
			return 0;
		}

		n = posix_arch_strnlen_nofault((const char *)addr, chunk, &err);
		if (err != 0) {
			*err_arg = -1;
			return 0;
		}

		len += n;
		if (n < chunk) {
			break;
		}
	}

	return len;
}

int arch_mem_domain_max_partitions_get(void)
{
	return CONFIG_MAX_DOMAIN_PARTITIONS;
}

int arch_thread_priv_stack_space_get(const struct k_thread *thread, size_t *stack_size,
				     size_t *unused_ptr)
{
	if (!IS_ENABLED(CONFIG_INIT_STACKS) || !IS_ENABLED(CONFIG_THREAD_STACK_INFO)) {
		return -EINVAL;
	}

	if (thread->arch.priv_stack_start == 0U) {
		return -EINVAL;
	}

	*stack_size = thread->arch.priv_stack_size;

	return z_stack_space_get((const uint8_t *)thread->arch.priv_stack_start, *stack_size,
				 unused_ptr);
}
