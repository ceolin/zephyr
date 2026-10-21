/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Host side of the POSIX architecture user mode support.
 *
 * This file is built in the native simulator runner context, with the host C
 * library: it is the only part of the user mode support which uses the host
 * memory protection, signal handling and context switching functions.
 *
 * The RAM of the Zephyr image is protected in one of two ways:
 *
 * - With memory protection keys, when the host supports them: the image RAM
 *   is tagged with a "kernel" key, and the memory a user thread can access
 *   (its stack and the partitions of its memory domain) with "user" keys.
 *   Entering and leaving user mode only changes the access rights of the
 *   keys of the current host thread, which is cheap, and the memory is only
 *   tagged again when the memory a user thread can access changes.
 * - Otherwise, with the page permissions (mprotect()), which are changed every
 *   time user mode is entered or left.
 *
 * Only one Zephyr thread (host pthread) executes at a time, so the global
 * state below needs no locking. This state is in host memory, which is never
 * protected, so it is accessible in both kernel and user mode.
 */

/* For pthread_getattr_np(), the memory protection keys and the ucontext registers */
#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
#include "nsi_cmdline.h"
#include "nsi_tasks.h"
#include "nsi_tracing.h"
#include "nsi_utils.h"
#include "userspace_bottom.h"

#define PREFIX "POSIX arch user mode: "

/* Size of the alternate signal stack, shared by all threads */
#define ALTSTACK_SIZE (64 * 1024)

/* Part of the pthread stack kept free above the swap stack, for the frames
 * which are still active when a thread blocks
 */
#define SWAP_STACK_MARGIN (64 * 1024)

#define MAX_REGIONS POSIX_MEM_VIEW_MAX

#define PROT_RW (POSIX_PROT_R | POSIX_PROT_W)

/* Thread state, kept in the opaque host_state of each Zephyr thread */
struct host_state {
	/* Kernel context of the thread when it dropped to user mode */
	sigjmp_buf kernel_jb;
	/* Where to report a memory fault */
	struct posix_fault_info *fault;
	/* Stack pointer of the kernel context, and host pthread stack bounds */
	char *kernel_sp;
	char *pthread_stack;
	unsigned long pthread_stack_size;
	/* Guard page of the privileged stack, or 0 */
	unsigned long guard_page;
	int altstack_set;
};

_Static_assert(sizeof(struct host_state) <= POSIX_HOST_STATE_SIZE,
	       "POSIX_HOST_STATE_SIZE is too small");

static unsigned long ram_start;
static unsigned long ram_size;
static unsigned long page_size;

static volatile sig_atomic_t user_mode;
static pthread_t user_pthread;
static struct host_state *current_hs;

static sigjmp_buf fixup_jb;
static volatile sig_atomic_t fixup_active;

static void *altstack;

/* Memory protection keys, if supported by the host and not disabled */
static bool no_pkeys;
static int use_pkeys;
static int pkey_kernel;
static int pkey_user_rw;
static int pkey_user_ro;

/* Segments of the image RAM accessible from user mode (memory protection
 * keys), or not accessible as in kernel mode (page permissions), currently
 * set
 */
static struct posix_mem_region applied[MAX_REGIONS];
static int n_applied;

/* Last view applied, to skip applying it again when it did not change */
static struct posix_mem_region last_view[MAX_REGIONS];
static int n_last_view = -1;

/* Staging area to pass the function to call to a new context */
static struct {
	void (*fn)(void *arg);
	void *arg;
} staging;

static int to_host_prot(int prot)
{
	return ((prot & POSIX_PROT_R) ? PROT_READ : 0) | ((prot & POSIX_PROT_W) ? PROT_WRITE : 0) |
	       ((prot & POSIX_PROT_X) ? PROT_EXEC : 0);
}

static void host_mprotect(unsigned long start, unsigned long size, int prot)
{
	if (mprotect((void *)start, size, to_host_prot(prot)) != 0) {
		nsi_print_error_and_exit(PREFIX "mprotect(0x%lx, 0x%lx) failed\n", start, size);
	}
}

static void host_pkey_mprotect(unsigned long start, unsigned long size, int prot, int pkey)
{
	if (pkey_mprotect((void *)start, size, to_host_prot(prot), pkey) != 0) {
		nsi_print_error_and_exit(PREFIX "pkey_mprotect(0x%lx, 0x%lx) failed\n", start,
					 size);
	}
}

static void pkeys_kernel_access(void)
{
	pkey_set(pkey_kernel, 0);
	pkey_set(pkey_user_rw, 0);
	pkey_set(pkey_user_ro, 0);
}

static void pkeys_user_access(void)
{
	pkey_set(pkey_user_rw, 0);
	pkey_set(pkey_user_ro, PKEY_DISABLE_WRITE);
	pkey_set(pkey_kernel, PKEY_DISABLE_ACCESS);
}

/*
 * Allocate the protection keys, before any Zephyr thread is created: the
 * threads inherit the access rights of their creator, and so get access to
 * the memory of all keys in kernel mode.
 */
static void pkeys_init(void)
{
	if (no_pkeys) {
		return;
	}

	pkey_kernel = pkey_alloc(0, 0);
	pkey_user_rw = pkey_alloc(0, 0);
	pkey_user_ro = pkey_alloc(0, 0);

	if ((pkey_kernel < 0) || (pkey_user_rw < 0) || (pkey_user_ro < 0)) {
		/* Not supported by the host, use the page permissions */
		return;
	}

	pkeys_kernel_access();
	use_pkeys = 1;
}

NSI_TASK(pkeys_init, PRE_BOOT_2, 0);

static void add_options(void)
{
	static struct args_struct_t options[] = {
		{
			.is_switch = true,
			.option = "userspace-no-pkeys",
			.type = 'b',
			.dest = (void *)&no_pkeys,
			.descript = "Protect the memory in user mode with the page permissions "
				    "instead of the memory protection keys, even if the host "
				    "supports them (slower)",
		},
		ARG_TABLE_ENDMARKER,
	};

	nsi_add_command_line_opts(options);
}

NSI_TASK(add_options, PRE_BOOT_1, 0);

/* Leave user mode. This is async-signal-safe */
static void lift(void)
{
	user_mode = 0;

	if (use_pkeys) {
		pkeys_kernel_access();
		return;
	}

	(void)mprotect((void *)ram_start, ram_size, PROT_READ | PROT_WRITE);
	n_applied = 0;
	n_last_view = -1;
	if ((current_hs != NULL) && (current_hs->guard_page != 0)) {
		(void)mprotect((void *)current_hs->guard_page, page_size, PROT_NONE);
	}
}

/* Signals raised by invalid accesses or instructions */
static const int fault_signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE};

static void fault_handler(int sig, siginfo_t *info, void *ucontext)
{
	ucontext_t *uc = ucontext;

	/* The access rights are reset when a signal is delivered */
	if (use_pkeys && !user_mode) {
		pkeys_kernel_access();
	}

	if (fixup_active) {
		fixup_active = 0;
		siglongjmp(fixup_jb, 1);
	}

	if (!user_mode || (current_hs == NULL) || !pthread_equal(pthread_self(), user_pthread)) {
		/* Fault in kernel mode: crash as without this handler */
		struct sigaction act;

		memset(&act, 0, sizeof(act));
		act.sa_handler = SIG_DFL;
		sigaction(sig, &act, NULL);
		return;
	}

	/* Fault in user mode: give access back to the kernel and report it */
	lift();

	current_hs->fault->addr = (unsigned long)info->si_addr;
	current_hs->fault->sig = sig;
	current_hs->fault->code = info->si_code;
#if defined(__x86_64__)
	current_hs->fault->pc = uc->uc_mcontext.gregs[REG_RIP];
	current_hs->fault->sp = uc->uc_mcontext.gregs[REG_RSP];
#elif defined(__i386__)
	current_hs->fault->pc = uc->uc_mcontext.gregs[REG_EIP];
	current_hs->fault->sp = uc->uc_mcontext.gregs[REG_ESP];
#elif defined(__aarch64__)
	current_hs->fault->pc = uc->uc_mcontext.pc;
	current_hs->fault->sp = uc->uc_mcontext.sp;
#else
	(void)uc;
	current_hs->fault->pc = 0;
	current_hs->fault->sp = 0;
#endif

	siglongjmp(current_hs->kernel_jb, 1);
}

int posix_arch_userspace_init(unsigned long start, unsigned long end, unsigned long page)
{
	struct sigaction act;

	if ((unsigned long)sysconf(_SC_PAGESIZE) != page) {
		return -1;
	}

	page_size = page;
	ram_start = start;
	ram_size = end - start;

	if (use_pkeys) {
		host_pkey_mprotect(ram_start, ram_size, PROT_RW, pkey_kernel);
	}

	altstack = malloc(ALTSTACK_SIZE);
	if (altstack == NULL) {
		nsi_print_error_and_exit(PREFIX "Can't allocate memory\n");
	}

	memset(&act, 0, sizeof(act));
	act.sa_sigaction = fault_handler;
	act.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigemptyset(&act.sa_mask);
	for (size_t i = 0; i < NSI_ARRAY_SIZE(fault_signals); i++) {
		if (sigaction(fault_signals[i], &act, NULL) != 0) {
			nsi_print_error_and_exit(PREFIX "sigaction() failed\n");
		}
	}

	return 0;
}

int posix_arch_is_user_mode(void)
{
	return user_mode;
}

void posix_arch_mem_lift(void)
{
	lift();
}

/* Give the memory accessible to the current view back to the kernel */
static void pkeys_release_applied(void)
{
	for (int i = 0; i < n_applied; i++) {
		host_pkey_mprotect(applied[i].start, applied[i].size,
				   PROT_RW | (applied[i].prot & POSIX_PROT_X), pkey_kernel);
	}
	n_applied = 0;
}

void posix_arch_guard_set(void *host_state, unsigned long guard_page)
{
	struct host_state *hs = host_state;

	hs->guard_page = guard_page;
	host_mprotect(guard_page, page_size, POSIX_PROT_NONE);
}

void posix_arch_guard_release(void *host_state)
{
	struct host_state *hs = host_state;

	if (hs->guard_page != 0) {
		host_mprotect(hs->guard_page, page_size, PROT_RW);
		hs->guard_page = 0;
	}

	/* The thread memory may be reused: forget the view, which may include it */
	if (use_pkeys) {
		pkeys_release_applied();
	}
	n_last_view = -1;
	if (current_hs == hs) {
		current_hs = NULL;
	}
}

void posix_arch_guard_rearm(void *host_state)
{
	struct host_state *hs = host_state;

	/* With memory protection keys the guard page permissions never change */
	if (!use_pkeys && (hs->guard_page != 0)) {
		host_mprotect(hs->guard_page, page_size, POSIX_PROT_NONE);
	}
}

/*
 * Permissions the view gives to addr: the last region of the view containing
 * it wins.
 */
static int view_prot(const struct posix_mem_region *view, int n, unsigned long addr)
{
	int prot = PROT_RW;

	for (int i = 0; i < n; i++) {
		if ((addr >= view[i].start) && (addr - view[i].start < view[i].size)) {
			prot = view[i].prot;
		}
	}

	return prot;
}

static int cmp_ulong(const void *a, const void *b)
{
	unsigned long x = *(const unsigned long *)a;
	unsigned long y = *(const unsigned long *)b;

	return (x > y) - (x < y);
}

/*
 * Split the view in disjoint segments with their final permissions, merging
 * the adjacent ones with the same permissions. Returns the number of segments.
 */
static int view_segments(const struct posix_mem_region *view, int n,
			 struct posix_mem_region *segments)
{
	static unsigned long bounds[2 * MAX_REGIONS];
	int n_bounds = 0;
	int n_seg = 0;

	for (int i = 0; i < n; i++) {
		bounds[n_bounds++] = view[i].start;
		bounds[n_bounds++] = view[i].start + view[i].size;
	}
	qsort(bounds, n_bounds, sizeof(bounds[0]), cmp_ulong);

	for (int i = 0; i + 1 < n_bounds; i++) {
		unsigned long start = bounds[i];
		unsigned long end = bounds[i + 1];
		int prot;

		if (start == end) {
			continue;
		}
		prot = view_prot(view, n, start);
		if ((n_seg > 0) && (segments[n_seg - 1].prot == prot) &&
		    (segments[n_seg - 1].start + segments[n_seg - 1].size == start)) {
			segments[n_seg - 1].size += end - start;
		} else {
			if (n_seg == 2 * MAX_REGIONS) {
				nsi_print_error_and_exit(PREFIX "Too many memory segments\n");
			}
			segments[n_seg].start = start;
			segments[n_seg].size = end - start;
			segments[n_seg].prot = prot;
			n_seg++;
		}
	}

	return n_seg;
}

static void pkeys_apply(const struct posix_mem_region *segments, int n_seg)
{
	pkeys_release_applied();

	for (int i = 0; i < n_seg; i++) {
		int prot = segments[i].prot;
		int pkey;

		if ((prot & POSIX_PROT_R) == 0) {
			continue;
		}
		pkey = (prot & POSIX_PROT_W) ? pkey_user_rw : pkey_user_ro;
		host_pkey_mprotect(segments[i].start, segments[i].size,
				   PROT_RW | (prot & POSIX_PROT_X), pkey);
		if (n_applied == MAX_REGIONS) {
			nsi_print_error_and_exit(PREFIX "Too many memory regions\n");
		}
		applied[n_applied++] = segments[i];
	}
}

static void mprotect_apply(const struct posix_mem_region *segments, int n_seg)
{
	for (int i = 0; i < n_seg; i++) {
		if (segments[i].prot != PROT_RW) {
			host_mprotect(segments[i].start, segments[i].size, segments[i].prot);
		}
	}
}

void posix_arch_mem_apply(void *host_state, const struct posix_mem_region *view, int n)
{
	/* Not on the stack, which may be a small user mode stack */
	static struct posix_mem_region regions[MAX_REGIONS];
	static struct posix_mem_region segments[2 * MAX_REGIONS];

	if (n > MAX_REGIONS) {
		nsi_print_error_and_exit(PREFIX "Too many memory regions (%i)\n", n);
	}

	/*
	 * The view may be on the stack this function runs on, which is in the
	 * Zephyr image RAM: copy it to host memory before changing any
	 * permission. With page permissions, the memory which stays accessible
	 * (like the current stack) must also never become inaccessible in
	 * between, which applying disjoint segments ensures.
	 */
	memcpy(regions, view, n * sizeof(regions[0]));

	if ((n != n_last_view) || (memcmp(regions, last_view, n * sizeof(regions[0])) != 0)) {
		int n_seg = view_segments(regions, n, segments);

		if (use_pkeys) {
			pkeys_apply(segments, n_seg);
		} else {
			mprotect_apply(segments, n_seg);
		}
		memcpy(last_view, regions, n * sizeof(regions[0]));
		n_last_view = n;
	}

	current_hs = host_state;
	user_pthread = pthread_self();
	user_mode = 1;

	if (use_pkeys) {
		pkeys_user_access();
	}
}

static void stack_trampoline(void)
{
	void (*fn)(void *arg) = staging.fn;
	void *arg = staging.arg;

	fn(arg);
}

void posix_arch_run_on_stack(void (*fn)(void *), void *arg, void *stack, unsigned long stack_size)
{
	ucontext_t caller;
	ucontext_t callee;

	if (getcontext(&callee) != 0) {
		nsi_print_error_and_exit(PREFIX "getcontext() failed\n");
	}
	callee.uc_stack.ss_sp = stack;
	callee.uc_stack.ss_size = stack_size;
	callee.uc_link = &caller;
	makecontext(&callee, stack_trampoline, 0);

	staging.fn = fn;
	staging.arg = arg;

	if (swapcontext(&caller, &callee) != 0) {
		nsi_print_error_and_exit(PREFIX "swapcontext() failed\n");
	}
}

void posix_arch_user_enter(void *host_state, void (*entry)(void *), void *arg, void *stack,
			   unsigned long stack_size, const struct posix_mem_region *view, int n,
			   struct posix_fault_info *fault)
{
	struct host_state *hs = host_state;
	pthread_attr_t attr;
	ucontext_t uc;
	void *pth_stack;
	size_t pth_stack_size;

	if (sigsetjmp(hs->kernel_jb, 1) != 0) {
		/* Back from a fault in user mode */
		return;
	}

	hs->fault = fault;
	hs->kernel_sp = __builtin_frame_address(0);

	if ((pthread_getattr_np(pthread_self(), &attr) != 0) ||
	    (pthread_attr_getstack(&attr, &pth_stack, &pth_stack_size) != 0)) {
		nsi_print_error_and_exit(PREFIX "Can't get the pthread stack\n");
	}
	pthread_attr_destroy(&attr);
	hs->pthread_stack = pth_stack;
	hs->pthread_stack_size = pth_stack_size;

	if (!hs->altstack_set) {
		stack_t ss = {
			.ss_sp = altstack,
			.ss_size = ALTSTACK_SIZE,
		};

		if (sigaltstack(&ss, NULL) != 0) {
			nsi_print_error_and_exit(PREFIX "sigaltstack() failed\n");
		}
		hs->altstack_set = 1;
	}

	if (getcontext(&uc) != 0) {
		nsi_print_error_and_exit(PREFIX "getcontext() failed\n");
	}
	uc.uc_stack.ss_sp = stack;
	uc.uc_stack.ss_size = stack_size;
	uc.uc_link = NULL;
	makecontext(&uc, stack_trampoline, 0);

	staging.fn = entry;
	staging.arg = arg;

	posix_arch_mem_apply(hs, view, n);

	setcontext(&uc);

	nsi_print_error_and_exit(PREFIX "setcontext() failed\n");
}

void posix_arch_swap_stack_get(void *host_state, void **stack, unsigned long *stack_size)
{
	struct host_state *hs = host_state;
	/* Skip the lowest page, in case it is the pthread stack guard */
	char *bottom = hs->pthread_stack + page_size;
	char *top = hs->kernel_sp - SWAP_STACK_MARGIN;

	if (top <= bottom + SWAP_STACK_MARGIN) {
		nsi_print_error_and_exit(PREFIX "pthread stack too small\n");
	}

	*stack = bottom;
	*stack_size = top - bottom;
}

/* Cache of the host memory map, refreshed when an address is not found */
#define MAX_MAPS 1024
static struct {
	unsigned long start;
	unsigned long end;
	int prot;
} maps[MAX_MAPS];
static int n_maps;

static void maps_refresh(void)
{
	static char buf[128 * 1024];
	size_t len = 0;
	ssize_t ret;
	char *line;
	int fd;

	fd = open("/proc/self/maps", O_RDONLY);
	if (fd < 0) {
		nsi_print_error_and_exit(PREFIX "Can't open /proc/self/maps\n");
	}
	while ((len < sizeof(buf) - 1) &&
	       ((ret = read(fd, buf + len, sizeof(buf) - 1 - len)) > 0)) {
		len += ret;
	}
	close(fd);
	buf[len] = '\0';

	n_maps = 0;
	line = buf;
	while ((*line != '\0') && (n_maps < MAX_MAPS)) {
		char *end;
		unsigned long start = strtoul(line, &end, 16);
		unsigned long stop = strtoul(end + 1, &end, 16);
		const char *perms = end + 1;

		maps[n_maps].start = start;
		maps[n_maps].end = stop;
		maps[n_maps].prot = ((perms[0] == 'r') ? POSIX_PROT_R : 0) |
				    ((perms[1] == 'w') ? POSIX_PROT_W : 0) |
				    ((perms[2] == 'x') ? POSIX_PROT_X : 0);
		n_maps++;

		line = strchr(perms, '\n');
		if (line == NULL) {
			break;
		}
		line++;
	}
}

static int maps_lookup(unsigned long start, unsigned long size)
{
	unsigned long addr = start;
	unsigned long end = start + size;
	int prot = POSIX_PROT_R | POSIX_PROT_W | POSIX_PROT_X;

	while (addr < end) {
		int i;

		for (i = 0; i < n_maps; i++) {
			if ((addr >= maps[i].start) && (addr < maps[i].end)) {
				break;
			}
		}
		if (i == n_maps) {
			return -1;
		}
		prot &= maps[i].prot;
		addr = maps[i].end;
	}

	return prot;
}

int posix_arch_host_range_prot(unsigned long start, unsigned long size)
{
	int prot = maps_lookup(start, size);

	if (prot < 0) {
		maps_refresh();
		prot = maps_lookup(start, size);
	}

	return prot;
}

unsigned long posix_arch_strnlen_nofault(const char *s, unsigned long maxsize, int *err)
{
	unsigned long len = 0;

	if (sigsetjmp(fixup_jb, 1) != 0) {
		*err = -1;
		return 0;
	}

	fixup_active = 1;
	while ((len < maxsize) && (((volatile const char *)s)[len] != '\0')) {
		len++;
	}
	fixup_active = 0;

	*err = 0;
	return len;
}
