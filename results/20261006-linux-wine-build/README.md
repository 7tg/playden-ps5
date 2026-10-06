# Linux Wine build development evidence

The settings agent built a complete RADV-linked Wine runtime on its Linux VM.
Playden read the report/inventory and source Git status over SSH without changing
the VM outputs. Full source revision was
`7182441424f919bb518027166e121e4ca04c8e17`; no worktree changes were reported.
This differs from Playden's current locked wrapper source and is not an admitted
kit or a replacement for the retained tested runtime.

The fetched report lists 12 built PRX modules with empty unresolved-symbol lists,
plus 14 PE DLLs. It also flags `libvulkan.prx` importing `mkstemp` from libraries
not available to an ordinary title (`webkit_unbound`). Successful linking does
not establish successful title loading. The source has a Vulkan libc shim, but
the RADV link path must supply an appropriate title-compatible implementation
and be revalidated before native acceptance.

The peer reported foundation `9c0b994`, PRX foundation `182f1ea`, Vulkan
`50daad6104db5072a2f3c95283d604d21684f0c2`, Mesa `9d3cd41` and LLVM 18.
Only the full prospero/Vulkan revisions and report hash were read back here;
other provenance remains peer-reported pending complete independent admission.
[inventory.json](inventory.json) records module hashes and import diagnostics
from the report, not independently downloaded and hashed binaries.

Privacy/license inventory, approved source binding, required imports, preparation
and native gameplay remain gates. No console deployment or capability change
was made for these artifacts.
