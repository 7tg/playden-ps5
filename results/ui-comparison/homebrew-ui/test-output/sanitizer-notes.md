# Sanitizer boundary

The strict C++20 simulation test also runs with UndefinedBehaviorSanitizer. Its result is in `model-ubsan.log`.

An AddressSanitizer + UBSan binary was attempted on macOS 26.6.2. It stalled before the application entered main, inside the Apple Clang ASan shadow-memory initializer/dyld shared-cache enumeration. The process was stopped after inspecting its startup call stack. That attempt supplies no application test result. ASan therefore remains blocked by this host/toolchain initialization; rerun it with a compatible sanitizer runtime. No performance profiling or frame measurements were taken.
