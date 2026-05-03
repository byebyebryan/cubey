# Cubey C++ Style

Cubey follows a small project-specific C++ style. Use the Google C++ Style Guide
as background reading, not as a wholesale rulebook. The source of truth for this
repo is this document plus `.clang-format`, `.clang-tidy`, and the CMake warning
settings.

## Formatting

Formatting is mechanical:

- base style: LLVM
- indentation: 4 spaces
- column limit: 100
- pointer/reference alignment: `T* value`, `T& value`
- braces: attached

Run formatting through `clang-format` before committing C++ changes.

## Naming

Use names to make scope and ownership obvious without adding noisy prefixes.

| Scope / kind | Style | Example |
| --- | --- | --- |
| Types, classes, structs, enums | `PascalCase` | `Device`, `Swapchain`, `CommandPool` |
| Functions and methods | `snake_case` | `create_swapchain()`, `submit_frame()` |
| Local variables | `snake_case` | `image_count`, `frame_index` |
| Function parameters | `snake_case` | `width`, `usage`, `debug_name` |
| Private data members | `snake_case_` | `device_`, `swapchain_`, `frame_index_` |
| Constants | `kPascalCase` | `kFramesInFlight`, `kValidationLayers` |
| Enum values | `PascalCase` | `RenderFrameResult::RecreateSwapchain` |
| CMake options | `CUBEY_UPPER_SNAKE` | `CUBEY_ENABLE_VALIDATION` |

Avoid `m_`, `s_`, `g_`, and Hungarian-style prefixes. Prefer avoiding mutable
global state. If a global or namespace-scope value is genuinely constant, use
`kPascalCase`.

For Vulkan wrapper types, keep raw Vulkan handles named plainly:

```cpp
class Device {
public:
    VkDevice handle() const { return device_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
};
```

Only add `vk_` when there is a real same-scope ambiguity.

## C++ Usage

- Use C++20, but keep it plain and readable.
- Prefer RAII for owning Vulkan resources once code leaves spike mode.
- Prefer `std::span`, `std::array`, `std::vector`, and `std::string_view` where
  they naturally express ownership and lifetime.
- Prefer scoped enums.
- Prefer explicit types when they make Vulkan code easier to inspect; do not
  force `auto`.
- Keep templates and generic abstractions narrow. Add them when they remove real
  duplication or clarify ownership.

## Vulkan Code

- Keep important synchronization, image layout transitions, and ownership
  boundaries visible near the code that needs them.
- Prefer small Vulkan-native modules over a broad backend abstraction:
  `Device`, `Swapchain`, `Buffer`, `Image`, `Pipeline`, `CommandPool`.
- Validation-layer and headless smoke paths are first-class workflows, not debug
  afterthoughts.
- Use comments for synchronization reasoning, lifetime boundaries, and
  non-obvious Vulkan requirements. Do not comment obvious assignments.

## Review Standard

When reviewing code, prioritize:

- correctness and resource lifetime
- clear synchronization and layout transitions
- recoverable surface/swapchain behavior
- testable headless behavior
- narrow, practical abstractions

Style rules should support those goals. If a rule gets in the way of clear
Vulkan code, revise the rule rather than contorting the implementation.
