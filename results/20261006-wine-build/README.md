# Isolated Wine PRX/PE development build

The second build completed with observed process exit code **0**. The first
attempt failed during PE generation because the host tools were incomplete,
despite its PRX report being successful. Building the pinned source's
`__tooldeps__` target supplied the missing tools before the retry. Overall process
status and both output sets are required evidence.

[Inventory](inventory.json) records exact source revisions, applied patches and
SHA-256/size values checked against all **11 PRX modules and 14 PE DLLs** on disk.
Wine source is `490f6d5dcbb2a5047345b8af88d114bbcaad69a8`; the per-game wrapper
branch includes the generated-header inventory fix `0895` at `3d05a57`. The native
foundation remains `9c0b994`; PRX conversion uses isolated foundation `182f1ea`.
The settings peer's `0745` input/audio extension is not included.

The PRX report has no unresolved symbols, title-unbound symbols, WebKit-unbound
symbols or raw syscalls. Its libc data imports remain visible in the inventory
and require native loader acceptance. Some ELF-stage working outputs have
unresolved helper references before final PRX assembly; the final PRX report is
the scope of the unresolved-symbol result.

This is **build evidence**, not a complete compatibility kit. No RADV/libvulkan
driver or OpenGL SDK was supplied to this build. Full runtime payload, template
repairs, licenses, provenance, producer/consumer admission, FW13.60 loading and
gameplay remain unverified. Retained runtime `e16ec8f` and capability gates remain
unchanged. No build from this experiment has been deployed to the PS5.

Local working outputs: `build/wine-integration/ps5/{prx,pe,report.json}`; script log:
`/tmp/playden-wine-prx-build.log`. Host tools used LLVM18 and Homebrew bison, with
the native foundation's SDK v0.42. The first failed report/log are retained as
`build/wine-integration/first-prx-{report.json,build.log}`.
