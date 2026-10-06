# Scaling and 120 Hz for DXVK games (roadmap R3 / U5)

Date: 2026-10-06. Investigation only: nothing here is built or checked on a console. Sources are
prospero-win at `410ac1c` (`~/.chauffeur/worktrees/prospero-win-input-tuning`) and its Wine
patch series.

## Why the Scaling and 120 Hz rows are hidden for DXVK

prospero-win has two ways of putting frames on the TV:

- **GDI and the loading spinner.** Wine hands BGRA frames to the title (`pw_wine_frame_box`), and
  `wine64_main.c` draws them with `pw_videoout_ps5_draw_scaled(frame, scaling, ...)`. This path
  honors `[display] scaling` (fit, integer, stretch).
- **Vulkan (DXVK).** Frames never reach the title. Patch 0460 (`win32u: Present Vulkan surfaces
  on the PS5's display plane`) makes each Win32 surface a `VK_KHR_display` surface on the host
  instance, "always in the display's native mode, which scans out full screen; the desktop is
  scaled to it". When the swapchain differs from the window, `win32u/vulkan.c` chains
  `VkSwapchainPresentScalingCreateInfoEXT` with `VK_PRESENT_SCALING_STRETCH_BIT_EXT` (patch 0456
  keeps DXVK's own structures in that chain). The title's `vulkan_frames_*` code only shows
  GDI frames and the spinner while the Vulkan driver is idle.

So a DXVK game is always stretched to the native mode at the native refresh, whatever the
profile says. OpenGL games get 120 Hz through `WINE_PS5_GL_REFRESH`, which the PS5 OpenGL SDK
reads; nothing equivalent exists for Vulkan.

## What each piece would need

| Piece | Change | Repository | Build needs |
| --- | --- | --- | --- |
| Fit (keep aspect) | Pass the profile's scaling to Wine (for example `WINE_PS5_SCALING`) and have `win32u/vulkan.c` request `VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_EXT` instead of `STRETCH` | prospero-win: new Wine patch after 0456, `wine64_main.c` env | Wine rebuild: `tools/build_wine_ps5.sh` with `PS5_PRX_FOUNDATION` (a foundation checkout with module exports, branch `exp/prx-module`) |
| Fit, driver side | The display-plane WSI must honor `VK_EXT_swapchain_maintenance1` scaling behaviors; today only stretch is known to work | Mesa/RADV for ps5vk (`--ps5vk-sdk` / `--radv` in `build_wine_ps5.sh`) | ps5vk/RADV source and SDK |
| Integer | Not a Vulkan scaling mode. Options: `ONE_TO_ONE` with a desktop that is an exact divisor of the output (1920x1080 to 3840x2160), or a blit in the WSI that picks the largest whole multiple | Mesa/RADV WSI, or a recipe rule in Playden | Same as above |
| Stretch | Works today (the default) | — | — |
| 120 Hz | 0460 always picks the display's native mode. It would need to pick a 120 Hz mode from `vkGetDisplayModePropertiesKHR` when the profile asks (and the TV offers one), and VideoOut must accept the mode change | prospero-win Wine patch near 0460; RADV display code | Wine rebuild and RADV |

## Why this stops at a note

- This Mac has no `PS5_PRX_FOUNDATION` checkout (see [dependencies.md](dependencies.md)), so
  Wine cannot be rebuilt here, and the ps5vk/RADV sources are not checked out either.
- Even a title-only change cannot be console-checked without installing a modified prospero-win
  title. The installed PPSA99995 runtime (`e16ec8f`) must stay intact, and a parallel test title
  belongs to the integration's packaging work.

## Playden side when it lands

Playden already gates both rows (`Unavailable` in `game_settings.cpp`: "Not available with DXVK
output yet", "Only available with OpenGL"). With a runtime that does the above, add a kit
capability (for example `settings-dxvk-scaling`, `settings-dxvk-120hz`) and lift those two reasons
for it. The profile keys stay the same (`[display] scaling`, `[display] refresh`).
