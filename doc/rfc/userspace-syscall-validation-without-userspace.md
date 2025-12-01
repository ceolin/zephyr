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
   - `K_SYSCALL_MEMORY_READ()` and `K_SYSCALL_MEMORY_WRITE()` macros may need adaptation
   - In supervisor mode, we can still validate pointer validity and bounds, but permission checks may be simplified

3. **Build system changes**:
   - Ensure verification functions and related infrastructure are compiled when `CONFIG_SYSCALL_VALIDATION` is enabled
   - Update `kernel/CMakeLists.txt` to include necessary source files

### Implementation Considerations

1. **Performance Impact**: Validation adds overhead. This should be documented and measured.

2. **Error Handling**: Currently, verification functions use `K_OOPS()` which kills the calling thread. In supervisor mode without userspace, this behavior may need to be configurable (e.g., return error codes vs. oops).

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

1. **Performance overhead**: What is the performance impact of enabling validation? Should there be a way to disable it for release builds?

2. **Error handling semantics**: Should validation failures in supervisor mode behave differently than in user mode? Currently `K_OOPS()` kills the thread - is this appropriate for supervisor mode?

3. **Memory validation**: How should memory validation work in supervisor mode? Can we validate pointer validity without full userspace memory protection infrastructure?

4. **Kernel object tracking**: Does the kernel object tracking system work correctly without full userspace? Are there dependencies that need to be addressed?

5. **Backward compatibility**: Will this change affect existing applications? Should this be opt-in via the config option?

6. **Testing strategy**: How do we ensure comprehensive test coverage for this feature?

7. **Documentation**: How should this feature be documented? Should it be presented as a development/debugging aid?

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
