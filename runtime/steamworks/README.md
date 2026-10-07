# gbe_fork Steam API (LGPL-3.0)

`steam_api.dll` and `steam_api64.dll` are [gbe_fork](https://github.com/Detanup01/gbe_fork), a fork of the Goldberg
Steam Emulator by Mr_Goldberg, licensed under the GNU Lesser General Public License v3.0. They are shipped unmodified.
Playden does not link against them: when a game is prepared, they replace the game's own `steam_api(64).dll` in its
folder (the original is kept as `<dll>.orig`) and the game loads them. Source code: https://github.com/Detanup01/gbe_fork.

They are the same files the macOS Playden app and GameNative ship (GameNative-android
`app/src/main/assets/steampipe/`, file commit `08d2db098917ae029e4a4acccb73f37f8f699434`). No gbe_fork release tag is
recorded for this build.

| File | SHA-256 |
| --- | --- |
| `steam_api.dll` | `4cc9dfcab8b4df0db507fa91d0acec6e58693fd4edd78dc80d39bda8072d08ed` |
| `steam_api64.dll` | `513430ac8b869b0eed258d9e5feba5176a35446b3b61a63d01ab1d87683cdd1b` |

Playden checks both hashes (`src/runtime/steamworks.h`) before every use.
