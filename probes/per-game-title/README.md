# Per-game title acceptance bridge

Developer-only, one-shot ELF using the production `TitleRegistry` adapter and
the existing ShadowMountPlus loopback API. This is not a runtime installer or
production access bootstrap. It does not restart any console service.

Build from the repository root with the patched runtime's foundation SDK 0.42:

```sh
mkdir -p build/per-game-title-probe
scripts/with-env.sh \
  deps/ps5-payload-sdk-helper/bin/prospero-clang++ \
  -std=c++17 -Wall -Wextra -Werror -Isrc \
  probes/per-game-title/main.cpp \
  src/platform/ps5/title_registry.cpp src/platform/ps5/title_runtime.cpp \
  -o build/per-game-title-probe/playden-per-game-title-probe.elf
```

The operator must claim the console, inspect its state and establish an owned
`/data/playden/development/FAKE78001` directory. `owner.txt` contains exactly
`Playden per-game wrapper acceptance FAKE78001` followed by a newline. The
probe refuses a missing/wrong marker. With no `register.request`, it only
appends its diagnostic log and queries inventory. HTTP response size and socket
waits are bounded; unsupported transfer encoding is rejected.

Registration is explicitly gated by a `register.request` file containing the
same marker. Before registration, the operator must finish staging the tested
title and its `playden-acceptance-owner.txt` marker. The probe independently
requires native `/user/app/FAKE78001` absence, the exact source marker and
ShadowMount inventory absence. It submits manual/add + scan through the existing
adapter, logs the returned result and consumes its owned request. An accepted
scan is pending work, not proof of registered readiness or startup. It never
removes a source or title.

`probe.log` records inventory and payload-process directory-fsync/random-device
results. Those API results do not establish behavior after the per-title Lapy
grant. Verify that separately through the actual per-game wrapper/session path.
Release the PS5 immediately after the probe and evidence collection.
