# RFC: Enable Userspace Syscall Validation When Userspace is Not Enabled

## Introduction

This RFC proposes enabling userspace syscall validation functionality even when the `CONFIG_USERSPACE` option is disabled. Currently, syscall validation only occurs when userspace is enabled, meaning that applications running without userspace support miss out on valuable parameter validation that could catch bugs early in development.

This proposal targets developers, maintainers, and anyone working with Zephyr applications that don't require full userspace isolation but would benefit from enhanced API parameter validation.

## Problem Description

Currently in Zephyr, when `CONFIG_USERSPACE` is disabled, all syscall APIs directly call their implementation functions without any validation. According to the documentation in `doc/kernel/usermode/syscalls.rst`:

If `CONFIG_USERSPACE` is not enabled, all APIs just directly call the implementation function.

This means that applications running without userspace support do not benefit from the comprehensive parameter validation that syscall verification functions provide, including:

- **Kernel object validation**: Ensuring kernel object pointers are valid, of the correct type, initialized, and that the calling thread has permissions
- **Memory buffer validation**: Verifying that memory buffers are accessible and within valid bounds
- **Parameter bounds checking**: Validating that arguments are within acceptable ranges
- **Type safety**: Ensuring correct kernel object types are used

Without this validation, bugs such as:
- Passing invalid kernel object pointers
- Using uninitialized kernel objects
- Passing out-of-bounds memory buffers
- Using incorrect kernel object types

...may go undetected until runtime failures occur, making debugging more difficult and potentially leading to system crashes or data corruption.

**Important Note**: It should be understood that enabling validation without userspace has significant limitations:
- Memory access permission validation cannot be performed (kernel threads have access to all memory)
- Error handling via thread termination (`K_OOPS()`) is problematic for kernel threads
- Only basic validation (pointer validity, object type, initialization state) can be performed
- The primary security and safety benefits of userspace validation (memory protection, privilege isolation) are not available

## Proposed Change (Summary)

Enable syscall validation functions to run even when `CONFIG_USERSPACE` is disabled by:

1. Introducing a new configuration option (e.g., `CONFIG_SYSCALL_VALIDATION`) that can be enabled independently of `CONFIG_USERSPACE`
2. Modifying the syscall generation logic to invoke verification functions when this option is enabled, regardless of userspace state
3. Ensuring that validation functions can operate correctly in supervisor-only mode (without privilege elevation)
4. Maintaining backward compatibility - existing applications without this option enabled will continue to work as before

This would allow developers to benefit from enhanced parameter validation during development and testing without requiring the full overhead of userspace isolation.

## Proposed Change (Detailed)

### Configuration Option

Add a new Kconfig option in `kernel/Kconfig`:

```kconfig
config SYSCALL_VALIDATION
	bool "Enable syscall parameter validation"
	depends on !USERSPACE
	help
	  When enabled, syscall APIs will invoke verification functions
	  to validate parameters even when userspace is disabled.

	  Note: When CONFIG_USERSPACE is enabled, validation is always
	  performed. This option only affects behavior when userspace
	  is disabled.
```

### Code Generation Changes

Modify the syscall generation logic in `scripts/gen_syscalls.py` to:

1. Check for `CONFIG_SYSCALL_VALIDATION` in addition to `CONFIG_USERSPACE`
2. Generate syscall wrappers that invoke verification functions when either option is enabled
3. Ensure verification functions are compiled and linked when `CONFIG_SYSCALL_VALIDATION` is enabled

The generated syscall wrapper would change from:

```c
static inline void k_sem_init(struct k_sem * sem, unsigned int initial_count, unsigned int limit)
{
#ifdef CONFIG_USERSPACE
    if (z_syscall_trap()) {
        arch_syscall_invoke3(...);
        return;
    }
    compiler_barrier();
#endif
    z_impl_k_sem_init(sem, initial_count, limit);
}
```

To something like:

```c
static inline void k_sem_init(struct k_sem * sem, unsigned int initial_count, unsigned int limit)
{
#if defined(CONFIG_USERSPACE) || defined(CONFIG_SYSCALL_VALIDATION)
    #ifdef CONFIG_USERSPACE
    if (z_syscall_trap()) {
        arch_syscall_invoke3(...);
        return;
    }
    compiler_barrier();
    #endif

    #ifdef CONFIG_SYSCALL_VALIDATION
    if (!IS_ENABLED(CONFIG_USERSPACE)) {
        /* Direct validation call when userspace is disabled */
        z_vrfy_k_sem_init(sem, initial_count, limit);
        return;
    }
    #endif
#endif
    z_impl_k_sem_init(sem, initial_count, limit);
}
```

### Verification Function Modifications

Verification functions currently assume they are called from user mode and may rely on userspace-specific infrastructure. When `CONFIG_SYSCALL_VALIDATION` is enabled without `CONFIG_USERSPACE`, we need to ensure:

1. **Kernel object validation** can work without userspace permissions system:
   - Modify `k_object_validate()` and related functions to handle supervisor-mode validation
   - Ensure kernel object tracking works without full userspace infrastructure

2. **Memory validation** adapts to supervisor mode:
   - `K_SYSCALL_MEMORY_READ()` and `K_SYSCALL_MEMORY_WRITE()` macros currently check user thread memory access permissions, which are meaningless in kernel space
   - In supervisor mode, we can only perform limited validation:
     * Pointer is not NULL
     * Pointer alignment (if applicable)
     * Buffer size doesn't cause integer overflow
     * Basic bounds checking (if RAM region information is available)
   - Memory permission checks (read/write access, memory domain boundaries) cannot be performed without userspace infrastructure
   - This significantly limits the value of memory validation when userspace is disabled

3. **Build system changes**:
   - Ensure verification functions and related infrastructure are compiled when `CONFIG_SYSCALL_VALIDATION` is enabled
   - Update `kernel/CMakeLists.txt` to include necessary source files

### Implementation Considerations

1. **Performance Impact**: Validation adds overhead. This should be documented and measured.

2. **Error Handling**: Currently, verification functions use `K_OOPS()` which kills the calling thread. This is problematic in kernel space because:
   - Killing kernel threads (especially essential threads) can crash the system
   - Kernel threads may be in interrupt context or holding locks, making termination unsafe
   - Kernel threads are trusted code, so killing them may be too harsh for validation failures
   - Alternative approaches need careful consideration:
     * Return error codes (but changes API semantics - APIs that don't currently return errors would need to)
     * Log and continue (may mask bugs)
     * Configurable error handling (adds complexity)
     * Use assertions that can be disabled (defeats the purpose)
   - This is a fundamental design challenge that needs resolution before implementation

3. **Dependencies**: Some validation functions may depend on userspace infrastructure. These dependencies need to be identified and made optional or replaced.

4. **Testing**: Comprehensive tests need to be added to verify validation works correctly in both userspace-enabled and userspace-disabled scenarios.

## Dependencies

This change affects:

- **Kernel subsystem**: Core kernel syscall infrastructure
- **Build system**: CMake and Kconfig configuration
- **Scripts**: `scripts/gen_syscalls.py` and related syscall generation scripts
- **Documentation**: `doc/kernel/usermode/syscalls.rst` and related documentation

Components that may be affected:
- All subsystems that define syscalls (kernel APIs, drivers, etc.)
- Kernel object management (`kernel/userspace.c`, `kernel/userspace_handler.c`)
- Memory validation macros (`include/zephyr/internal/syscall_handler.h`)

## Concerns and Unresolved Questions

1. **Performance overhead**: What is the performance impact of enabling validation? Should there be a way to disable it for release builds? Validation adds function call overhead and parameter checking on every API call, which could significantly impact real-time performance characteristics of applications.

2. **Memory access validation limitations**: A fundamental challenge is that many validation checks are designed around the concept of user thread memory access permissions. In kernel space, all threads have supervisor privileges and can access any memory address. The current validation macros like `K_SYSCALL_MEMORY_READ()` and `K_SYSCALL_MEMORY_WRITE()` check:
   - Whether the calling thread has read/write permissions on the memory buffer
   - Whether the buffer is within the thread's memory domain
   - Whether the buffer crosses memory domain boundaries

   These checks are meaningless in kernel space where there are no memory protection boundaries. We can only perform basic sanity checks like:
   - Pointer is not NULL
   - Pointer is within valid RAM regions (if such information is available)
   - Buffer size doesn't cause integer overflow

   This significantly reduces the value proposition of enabling validation without userspace.

3. **Error handling semantics - thread termination**: Currently, verification functions use `K_OOPS()` which kills the calling thread when validation fails. This approach works for user threads because:
   - User threads are considered untrusted
   - Killing a misbehaving user thread doesn't compromise system integrity
   - The kernel can continue operating with other threads

   However, in kernel space without userspace:
   - All threads are kernel threads, potentially including critical system threads
   - Killing a kernel thread (especially an essential thread) could crash the entire system
   - Kernel threads are typically trusted code, so killing them on validation failure may be too harsh
   - Many kernel threads may be in interrupt context or holding locks, making termination unsafe

   Alternative error handling approaches need to be considered:
   - Return error codes instead of killing the thread (but this changes API semantics)
   - Log errors and continue (but this may mask bugs)
   - Use assertions that can be disabled in release builds (but this defeats the purpose)
   - Make error handling configurable (adds complexity)

4. **Kernel object validation limitations**: Kernel object validation functions like `k_object_validate()` check:
   - Object permissions (whether the thread has been granted access)
   - Object initialization state
   - Object type correctness

   Without userspace, the permission system may not be fully initialized or may not apply. We need to determine:
   - Can we validate kernel objects without the userspace permission infrastructure?
   - Should we skip permission checks when userspace is disabled?
   - How do we handle kernel objects that are created on the stack or in ways that don't register with the object tracking system?

5. **Kernel object tracking dependencies**: The kernel object tracking system (`k_object_find()`, `k_object_validate()`, etc.) may have dependencies on userspace infrastructure. We need to verify:
   - Does object tracking work correctly without `CONFIG_USERSPACE`?
   - Are there data structures or initialization code paths that require userspace?
   - Can we make these dependencies optional or provide stub implementations?

## Alternatives Considered

### Alternative 1: Always Enable Validation When Syscalls Are Defined

**Approach**: Remove the conditional compilation and always include validation when syscalls are defined, regardless of userspace state.

**Pros**: Simpler implementation, consistent behavior
**Cons**: May add unnecessary overhead for applications that don't need it, potential breaking changes

**Rejected because**: The overhead may be significant for some applications, and forcing it may not be desirable.

### Alternative 2: Runtime Validation Flag

**Approach**: Add a runtime flag that can enable/disable validation dynamically.

**Pros**: Flexible, can be toggled at runtime
**Cons**: More complex, adds runtime overhead for checking the flag, may not catch all issues if disabled

**Rejected because**: Compile-time configuration is more efficient and aligns with Zephyr's design philosophy.

### Alternative 3: Separate Validation APIs

**Approach**: Create separate validation functions that can be called explicitly from application code.

**Pros**: Explicit control, no automatic overhead
**Cons**: Requires code changes in applications, easy to forget, doesn't provide the same seamless experience

**Rejected because**: The goal is to provide automatic validation without requiring application code changes.

### Alternative 4: Enhanced Assertions

**Approach**: Instead of syscall validation, enhance assertion macros to perform similar checks.

**Pros**: Simpler, no syscall infrastructure needed
**Cons**: Assertions can be disabled, less comprehensive, different semantics

**Rejected because**: Assertions have different semantics (can be disabled) and don't provide the same level of validation as syscall verification functions.

## Test Strategy

1. **Unit tests**: Create tests for validation functions in both userspace-enabled and userspace-disabled scenarios
2. **Integration tests**: Test syscall APIs with validation enabled but userspace disabled
3. **Performance tests**: Measure overhead of validation in various scenarios
4. **Regression tests**: Ensure existing functionality is not broken
5. **Sample applications**: Create sample applications demonstrating the feature

## References

- `doc/kernel/usermode/syscalls.rst` - System call documentation
- `doc/kernel/usermode/overview.rst` - Userspace overview
- `kernel/userspace_handler.c` - Userspace handler implementation
- `scripts/gen_syscalls.py` - Syscall generation script
