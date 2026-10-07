# One PS5 title per game, made on the console

Status: steps 0–4 done, step 5 in progress ([results](../../../results/20261007-title-shells/README.md)). Written 2026-10-07 while kit B builds.

## Goal

Remove the ten-game limit. Today every kit carries ten prebuilt PS5 title shells (FAKE78001–FAKE78010), and a game
can only be prepared when one of them is free. Instead, the kit carries **one** shell and **one** root helper, and
Playden makes a new PS5 title for each game on the console when it prepares the game.

## Why there are ten today

Each shell is built for one title ID, and each root helper is locked to one title ID:

| Part | What ties it to one ID |
| --- | --- |
| Shell (`eboot.bin`, prospero-win `native/wine64_main.c`) | `PW_TITLE_ID` is compiled in. It builds the sandbox path `/mnt/sandbox/<ID>_000/app0`, names the title descriptor and the log. |
| `sce_sys/param.json`, name, icon | Written per slot. Playden already rewrites name and icon on the console (`RuntimeHost::WriteTitleMetadata`). |
| Root helper (`lapy.elf`, PS5-Lapy-JB-Daemon `owned_root_daemon.c`) | `TARGET_TITLE` is compiled in; the helper serves a request only when `sceKernelGetAppInfo(pid)` of the caller returns exactly that ID. |
| Kit manifest | One variant per ID, each with its own helper hash. |
| Playden (`RuntimeHost`, install repository) | `Reserve` picks a free ID from the kit's variants (`kit_title_variants`). |

ShadowMountPlus registers any title folder, so registration does not depend on the ID.

## Design

- **Shell:** learns its title ID at startup instead of at build time. It uses the compiled ID only as a fallback, which
  keeps upstream's default PPSA99995 title working. One binary serves every game; nothing is edited per game, so the
  fake-signed SELF never needs re-signing on the console.
- **Root helper:** accepts a *rule* instead of one ID. The rule: the caller's title ID is in Playden's range
  (`FAKE78xxx`) **and** listed in an allow-list file that only root can write (written by Playden's own helper, the
  one embedded in FAKE77031, at preparation). Removal deletes the entry.
- **Kit:** one variant marked "any ID" plus a capability, e.g. `title-any-id`. Old kits keep the ten fixed variants;
  Playden supports both.
- **Playden:** when the kit has `title-any-id`, `Reserve` allocates the next unused ID in the range instead of a free
  variant, copies the shell and helper, writes `param.json` (ID, name), the icon and the allow-list entry, then
  registers as today. Games already on FAKE78001–FAKE78010 keep their titles.

## Step 0: console probes (decide before any code) — done

All four passed. The shell uses `sceKernelGetAppInfo(getpid())`; Playden writes the allow-list itself after its grant
(`seteuid(0)`), so the helper protocol is unchanged and there is no second request kind.

Each probe uses a throwaway title outside Playden's slots (FAKE78090+), uploaded and removed by hand. Claim the console
first; never sign out of anything. Record results in `results/2026-10-xx-title-shell-probes/`.

1. **Own title ID from inside the app.** A probe shell (prospero-win native build with a probe flag) logs at startup:
   - `sceKernelGetAppInfo(getpid())` result and `title_id`;
   - whether `/app0` exists, and what `/mnt/sandbox/` lists;
   - its executable path (`kern.proc.pathname` sysctl) and working directory.

   It passes if at least one of these gives the right ID before the shell needs its sandbox path. If
   `sceKernelGetAppInfo` is missing from the app's import stubs, add the stub (libkernel) first.
2. **Same binary, two IDs.** Copy the probe's `eboot.bin` unchanged into a second title (FAKE78091) with only
   `param.json` changed. Both register on PS5 Home and both start, and each logs its own ID.
3. **Helper rule.** Build the Lapy helper with the rule mode. It serves a listed ID, refuses an unlisted FAKE78xxx ID,
   refuses an ID outside the range, and refuses when the allow-list file isn't root-owned or is writable by others.
4. **How many titles PS5 Home shows.** Register about 30 copies of the probe title (FAKE78090–FAKE78119), check they all
   appear and start, then remove them all and check Home is clean. This finds any homebrew-title limit before we
   promise "unlimited".

Stop and report after step 0. If probe 1 fails every way, the fallback is still useful: Playden stamps the ID into a
small file next to the shell, and the shell finds it through the sandbox listing. Decide with the results in hand.

## Step 1: prospero-win shell (branch from `feat/playden-ps5`) — done

- `wine64_main.c`: resolve the title ID once at startup (method chosen in probe 1), build `PW_SANDBOX_APP0`, the title
  descriptor name and the log name from it; `PW_TITLE_ID` stays as the fallback.
- Host tests for the resolver (mock the kernel call). `tools/build_native.sh`: a build mode with no fixed ID.
- Upstream-shaped: the same change lets any launcher use one shell, so it belongs in the upstream proposal list.

## Step 2: Lapy helper (branch from `feat/playden-ps5`) — done

- Rule mode next to the exact-ID mode: range prefix + root-owned allow-list path, both compiled in.
- ~~The allow-list writer is a second request kind~~: Playden writes the list itself as root (step 0 decision), so the
  protocol header and its hash are unchanged. Tests for every refusal case in probe 3.

## Step 3: kit — done

- Packager: an "any ID" variant (one shell, one helper, a template `param.json`) and the `title-any-id` capability.
- Build as part of the next kit after kit B (or kit B's successor), with the same path-neutral pipeline.

## Step 4: Playden — done

- `KitTitleVariant` gains "any ID"; `KitCapabilities` gains `title_any_id`.
- Install repository: allocate the lowest unused FAKE78xxx not on disk and not in a record; keep the fixed-variant path for
  old kits.
- Preparation: copy the shell, stamp `param.json` and icon (extend `WriteTitleMetadata`), add the allow-list entry
  through Playden's helper, register. Removal: unregister, delete the folder, drop the allow-list entry.
- Host tests: allocation, reuse after removal, mixed records from old fixed slots, refusal when the kit lacks the
  capability, and crash safety (an ID reserved but not registered is reclaimed on startup).

## Step 5: console acceptance

- With the new kit: prepare 12 or more games (more than ten), all on PS5 Home, start several from Home.
- Remove a few, prepare new ones, and check the IDs are reused without leftovers.
- Recheck Half-Life 2, Super Meat Boy, Hexcells and Pizza Tower on their existing FAKE78001–FAKE78005 titles.

## Risks

- **The shell can't learn its ID in the app sandbox:** probe 1, with the fallback above.
- **Helper security:** a rule is looser than one exact ID. It's bounded by the root-owned allow-list, and only Playden's
  title can add entries. Review this before it ships.
- **PS5 Home limits:** probe 4.
- **Upstream:** the shell change and helper rule mode are general; propose them upstream with probe evidence.

Never push or open pull requests until the user says so.
