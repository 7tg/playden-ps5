# Compatibility

Games tested on a PS5 on firmware 13.60, plus community test reports (linked in the notes) on other firmware. Last updated 2026-10-19.

> [!NOTE]
> Games were checked on runtime kit **0.10**, the first stable kit, unless the Kit column names a newer one.
> OpenGL games draw through Mesa (Zink on Vulkan). See [Kits](#kits) for what each kit version changes.

Did you test a game? [Open a test report](../../issues/new?template=test-report.yml), even when it works.

## Status

| Status | Meaning |
| --- | --- |
| ✅ Works | Reaches the menu and plays with the controller, controller preset or USB input noted. |
| 🟡 Partial | Plays, with a problem that gets in the way. |
| ❌ Broken | Starts, but does not reach a playable state. |
| ⛔ Refused | Playden refuses to prepare it and tells you why. For .NET and shared-section refusals, Game settings › Known problems › Try anyway prepares it anyway. |

The **Kit** column is the newest runtime kit the game was checked on. The **Graphics** column is what the game
uses: Direct3D 8–11 goes through DXVK to Vulkan, and Direct3D 12 through vkd3d-proton (from kit 0.11).

## Games

| Game | Status | Kit | Engine · Graphics · CPU | Notes |
| --- | --- | --- | --- | --- |
| [STAR WARS: The Force Unleashed Ultimate Sith Edition](https://store.steampowered.com/app/32430) | ✅ Works | 0.10 | D3D9 · DXVK · 32-bit | In the game's own launcher, set Input Device to Xbox360 Controller (`IsKBAndMouse=FALSE`), then start `SWTFU.exe`. Loading a save and walking as Darth Vader with the controller work. Full campaign not tested. |
| [IMMORTALITY](https://store.steampowered.com/app/1350200) | ✅ Works | 0.10 | Unity · D3D11 · DXVK · 64-bit | Default configuration. Controller menus, film-grid zoom/navigation, opening clip playback and scrubbing, pause/image mode, and face selection to a second clip verified. Bundled Bink DLLs handle video; this does not verify the missing MF backend. Limited opening test; audio synchronization, later clips and full playthrough untested. |
| [Machinarium](https://store.steampowered.com/app/40700) | ✅ Works | 0.11 (dev) | — · D3D12 · vkd3d-proton · 64-bit | Game settings › Program = `Machinarium.exe`, Graphics = DXVK, Direct3D 12 = **vkd3d-proton**. Title screen, menus, New Game, the opening cutscene and the first junkyard scene with the controller cursor. With Wine's own Direct3D 12 it crashes before the first frame; on kit 0.10, `Machinarium_old_32bit_DX9.exe` (Recreate bottle after switching) works instead. |
| [Cyberpunk 2077](https://store.steampowered.com/app/1091500) | ✅ Works | 0.11 (dev) | REDengine 4 · D3D12 · vkd3d-proton · 64-bit | Game settings › Program = `bin/x64/Cyberpunk2077.exe` (the default launcher cannot start the game), Graphics = DXVK, Direct3D 12 = **vkd3d-proton**. The first start opens at 1024×768: set 1920×1080 in the game's Video settings. Intro, menus, character creation, the Nomad opening and walking in the garage with the controller; about 3 GB of video memory, no crash in a 13-minute session. Frame rate not measured yet. |
| [MULLET MADJACK](https://store.steampowered.com/app/2111190) | ✅ Works | 0.11 (dev) | Unity · D3D12 · vkd3d-proton · 64-bit | Graphics = DXVK, Direct3D 12 = **vkd3d-proton** (Playden then starts it with `-force-d3d12`). Language select, menus, intro cutscene and the first floor (punch, pick up the gun) with the controller. |
| [Bullet Witch](https://store.steampowered.com/app/696180) | ✅ Works | 0.11 (dev) | — · D3D9 · DXVK · 32-bit | Playden installs the game's PhysX package on first Play. Its launcher needs Enter: set Controller preset = **Mouse** and press Options on *Play Game* (or use a USB keyboard). Intro, menus, the opening cutscene and the first level with the controller. |
| [CULTIC](https://store.steampowered.com/app/1684930) | ✅ Works | 0.11 (dev) | Unity · D3D12 · vkd3d-proton · 64-bit | Graphics = DXVK, Direct3D 12 = **vkd3d-proton** (Playden then starts it with `-force-d3d12`). With Direct3D 11 it stays on a black loading screen. Language menu, main menu, the intro and the first level (moving, picking up the axe) with the controller. |
| [Deep Rock Galactic](https://store.steampowered.com/app/548430) | ✅ Works | 0.11 (dev) | Unreal 4 · D3D12 · vkd3d-proton · 64-bit | Program = *Deep Rock Galactic (DirectX 12)*, Direct3D 12 = **vkd3d-proton** (Playden then passes `-dx12`). Title screen, the intro and the tutorial mission (moving, shooting) with the controller. Online play not tested. |
| [ARMORED CORE VI FIRES OF RUBICON](https://store.steampowered.com/app/1888160) | ❌ Broken | 0.11 (dev) | — · D3D12 · 64-bit | Easy Anti-Cheat starts even with Program = `Game/armoredcore6.exe` and stops with "Failed to initialize Wine helper buffer". |
| [Hi-Fi RUSH](https://store.steampowered.com/app/1817230) | ❌ Broken | 0.11 (dev) | Unreal 4 · 64-bit | The game's copy protection (Denuvo) stops it: the PS5 reports an app error (CE-108255-1) under a second after start, before any graphics, with Direct3D 11 or 12. The default launch entry also needs a second program (`CreateProcess() returned 32`). |
| [Bulletstorm: Full Clip Edition](https://store.steampowered.com/app/501590) | ✅ Works | 0.10 | Unreal 3 · DXVK · 64-bit | The startup movies, the title screen and the menus work with the controller. Campaign › Continue plays the loading movie and reaches the prologue. Longer gameplay untested. |
| [Fallout: New Vegas](https://store.steampowered.com/app/22380) | ✅ Works | 0.10 | Gamebryo · D3D9 · DXVK · 32-bit | Press Play once with the default program, the game's launcher, so it writes its settings. Then set Game settings › Program = `FalloutNV.exe`. The legal screen, the logos and the main menu (Continue, New, Load) work. Gameplay not tested yet. |
| [DoDonPachi Resurrection](https://store.steampowered.com/app/464450) | ✅ Works | 0.10 | — · D3D9 · DXVK · 32-bit | Intro, title screen, menus, the attract demo and a stage with the controller work. |
| [Muchi Muchi Pork! & PinkSweets Boosted](https://store.steampowered.com/app/4229200) | ✅ Works | 0.10 | — · D3D11 · DXVK · 64-bit | Title screen, menus and the first Muchi Muchi Pork! stage with the controller. The first start after installing stayed on a black window once; starting it again with Play worked. Japanese text in the window title shows as boxes. |
| [DOOM Eternal](https://store.steampowered.com/app/782330) | ✅ Works | 0.10 | id Tech 7 · Vulkan · 64-bit | Set Program to `DOOMEternalx64vk.exe`. Graphics can stay as detected; the game draws with Vulkan directly. |
| [FINAL FANTASY XIII](https://store.steampowered.com/app/292120) | ✅ Works | 0.10 | Crystal Tools · D3D9 · DXVK · 32-bit | Default settings: Playden applies the game's recipe (program `ffxiiiimg.exe`, DXVK, the Steam emulator, the video memory the game checks for, full-screen at the Resolution setting). Plays at about 30 fps; videos play at 60 fps. ([#38](https://github.com/leolobato/playden-ps5/pull/38), firmware 13.60) |
| [Half-Life 2](https://store.steampowered.com/app/220) | ✅ Works | 0.10 | Source · D3D9 · 64-bit | Menu, New Game and the G-Man intro. |
| [Super Meat Boy](https://store.steampowered.com/app/40800) | ✅ Works | 0.10 | Tommunism · D3D9 · 32-bit | Intro, menus and play with the D-pad. |
| [FTL: Faster Than Light](https://store.steampowered.com/app/212680) | ✅ Works | 0.10 | SIL · OpenGL · 32-bit | Main menu through Zink. Mouse game: use Controller preset = **Mouse** (left stick and Cross) or a USB mouse. Asks for the language on every start. |
| [Super Hexagon](https://store.steampowered.com/app/221640) | ✅ Works | 0.10 | — · D3D11 · 32-bit | Menu and game at about 60 fps; the D-pad steers. |
| [Shadow Warrior Classic Redux](https://store.steampowered.com/app/225160) | ✅ Works | 0.10 | Build · OpenGL · 32-bit | Main menu at about 60 fps through Zink; the DualSense works. |
| [METAL GEAR RISING: REVENGEANCE](https://store.steampowered.com/app/235460) | ✅ Works | 0.10 | Platinum · D3D9 · 32-bit | Plays with the DualSense; Cloud saves restore. A mouse pointer shows in the top-left corner on logos and menus. 26 GB. |
| [Papers, Please](https://store.steampowered.com/app/239030) | ✅ Works | 0.10 | Unity · D3D11 · 64-bit | Mouse-only game: use Game settings › Controller preset = **Mouse**, or a USB mouse and keyboard. |
| [Hexcells](https://store.steampowered.com/app/265890) | ✅ Works | 0.10 | Unity · D3D11 · 32-bit | Reaches the main menu with the Unity `-force-d3d9` option. |
| [Undertale](https://store.steampowered.com/app/391540) | ✅ Works | 0.10 | GameMaker · D3D9 · 32-bit | Use Controller preset = **Arrow keys**. The game's own gamepad support does not see the DualSense yet. |
| [Katana ZERO](https://store.steampowered.com/app/460950) | ✅ Works | 0.10 | GameMaker · D3D11 · 32-bit | Plays with the DualSense. |
| [Into the Breach](https://store.steampowered.com/app/590380) | ✅ Works | 0.10 | — · OpenGL · 32-bit | Menu, new game and island map with the DualSense, at about 60 fps through Zink. |
| [Loop Hero](https://store.steampowered.com/app/1282730) | ✅ Works | 0.10 | GameMaker · D3D11 · 64-bit | The controller drives the menus. |
| [Vampire Survivors](https://store.steampowered.com/app/1794680) | ✅ Works | 0.10 | Unity · D3D11 · 64-bit | Plays with the DualSense; Cloud saves restore. A mouse pointer was seen once in the top-left corner. |
| [Sektori](https://store.steampowered.com/app/2105620) | ✅ Works | 0.10 | Unity · D3D11 · 64-bit | The DualSense shows up as an Xbox controller (Windows.Gaming.Input). |
| [Pizza Tower](https://store.steampowered.com/app/2231450) | ✅ Works | 0.10 | GameMaker · D3D11 · 64-bit | Starts and reaches the menu with the controller. |
| [Slay the Spire 2](https://store.steampowered.com/app/2868840) | ✅ Works | 0.10 | Godot (.NET) · OpenGL · 64-bit | Add `--rendering-driver opengl3` in Game settings › Extra arguments: Playden picks Godot's Vulkan renderer, which did not work for this game. On OpenGL, 60 fps on the menu and in combat. Tested from an exFAT USB drive. |
| [CHRONO TRIGGER](https://store.steampowered.com/app/613830) | ✅ Works | 0.10 | Cocos2d · OpenGL · 32-bit | Title, menu, and Resume loads the bookmark; Crono walks. Through Zink. |
| [A Short Hike](https://store.steampowered.com/app/1055540) | ✅ Works | 0.10 | Unity (Mono) · D3D11 · 32-bit | Title screen, Continue loads the save, and the hiker walks. Crashed on older kits. |
| [140](https://store.steampowered.com/app/242820) | ✅ Works | 0.10 | Unity 5.3 · D3D11 · 32-bit | Title animation, hub world and movement with the stick. Never drew on older kits. |
| [Azure Striker GUNVOLT](https://store.steampowered.com/app/388800) | ✅ Works | 0.10 | Inti Creates · D3D9 · 32-bit | In the settings window at start, Circle confirms and Cross cancels. Then title screen and menu with the controller. |
| [Blades of Time](https://store.steampowered.com/app/208670) | ✅ Works | 0.10 | Dagor · D3D9 · 32-bit | Intro, main menu and Story with the controller. Playden removes its old Steam DRM (SteamStub 2.1). |
| [BALL x PIT](https://store.steampowered.com/app/2062430) | ✅ Works | 0.10 | Unity · D3D11 · 64-bit | Intro and the first cutscene. |
| [The Silent Age](https://store.steampowered.com/app/352520) | ✅ Works | 0.10 | Unity · D3D9 · 32-bit | Use Controller preset = **Mouse**: Options confirms the Unity settings window, then the left stick and Cross play. Cloud saves restore. |
| [Future Knight](https://store.steampowered.com/app/4235410) | ✅ Works | 0.10 | Unity · D3D11 · 64-bit | Logo and the intro story; Cross advances. |
| [Schildmaid MX](https://store.steampowered.com/app/2154760) | ✅ Works | 0.10 | Unity · D3D11 · 64-bit | Logos, then the main menu with the controller. |
| [Redout: Enhanced Edition](https://store.steampowered.com/app/517710) | ✅ Works | 0.10 | Unreal 4 · D3D11 · 64-bit | Menus, Quick Race and the garage with the controller. |
| [Project Warlock](https://store.steampowered.com/app/893680) | ✅ Works | 0.10 | Unity · D3D11 · 64-bit | Plays the first level with the sticks. The D-pad does not move the menu focus. |
| [STAR WARS Knights of the Old Republic II: The Sith Lords](https://store.steampowered.com/app/208580) | ✅ Works | 0.10 | Odyssey · OpenGL · 32-bit | New game, cutscenes and the prologue with the controller. |
| [BULLET SOUL](https://store.steampowered.com/app/544580) | ✅ Works | 0.10 | — · D3D9 · 32-bit | Set Game settings › Program = `BulletSoulWin.exe`, then Recreate bottle: Playden picks the launcher, which the controller cannot use. Then it plays with the stick. |
| [BULLET SOUL INFINITE BURST](https://store.steampowered.com/app/545060) | ✅ Works | 0.10 | — · D3D9 · 32-bit | Plays with the controller. |
| [REPLACED](https://store.steampowered.com/app/1663850) | ✅ Works | 0.10 | Unity · D3D11 · 64-bit | Cloud saves restore; Continue loads the save and the character moves. |
| [SENRAN KAGURA Peach Ball](https://store.steampowered.com/app/1074080) | ✅ Works | 0.10 | — · D3D11 · 64-bit | Title menu and the story scenes with the controller. |
| [Brütal Legend](https://store.steampowered.com/app/225260) | ✅ Works | 0.10 | Buddha · D3D9 · 32-bit | Plays with the controller. |
| [Star Wars: Battlefront II (Classic, 2005)](https://store.steampowered.com/app/6060) | ✅ Works | 0.10 | — · D3D9 · 32-bit | Use Controller preset = **Mouse**: the left stick moves the pointer, Cross clicks, Options is Enter. Menus and the first mission load. |
| [ENSLAVED: Odyssey to the West Premium Edition](https://store.steampowered.com/app/245280) | ✅ Works | 0.10 | Unreal 3 · D3D9 · 32-bit | Main menu and New Journey with the controller. |
| [GUN](https://store.steampowered.com/app/2610) | ✅ Works | 0.10 | — · D3D9 · 32-bit | Menus and New Game with the controller. |
| [DUSK](https://store.steampowered.com/app/519860) | ✅ Works | 0.10 | Unity · D3D11 · 64-bit | Menus with the left stick and Cross; the first level plays. |
| [R.E.P.O.](https://store.steampowered.com/app/3241660) | ✅ Works | 0.10 | Unity · 64-bit | Reaches the menu and plays single player with a USB mouse and keyboard; multiplayer does not work. Controller not tested. ([#8](https://github.com/leolobato/playden-ps5/issues/8), firmware 9.40) |
| [Portal 2](https://store.steampowered.com/app/620) | ✅ Works | 0.10 | Source · D3D9 · 32-bit | Default settings, DualSense; gameplay confirmed. ([#36](https://github.com/leolobato/playden-ps5/issues/36), firmware 13.60) |
| [Team Fortress 2](https://store.steampowered.com/app/440) | ✅ Works | 0.10 | Source · D3D9 · DXVK · 64-bit | Plays with a USB mouse and keyboard. Online play does not work: the game shows it is disconnected. The menu does not react to the DualSense, even with Controller preset = **Mouse**. Keep Game settings › Steam features on **Offline emulation**: with the game's own files it quits after about 18 seconds. On Steam accounts from before about 2010, Playden installed only the game's executables until the fix for [#58](https://github.com/leolobato/playden-ps5/issues/58). |
| [Megabonk](https://store.steampowered.com/app/3405340) | ✅ Works | 0.10 | — | DualSense; Max/Ultra graphics reported. ([#30](https://github.com/leolobato/playden-ps5/issues/30), firmware 13.60) |
| [American Truck Simulator](https://store.steampowered.com/app/270880) | ✅ Works | 0.10 | — | Disable news, World of Trucks and image spotlight. DualSense or USB mouse and keyboard; startup takes over a minute. ([#27](https://github.com/leolobato/playden-ps5/issues/27), firmware 9.40) |
| [Fast Food Simulator](https://store.steampowered.com/app/2916430) | ✅ Works | 0.10 | — | Single-player with USB mouse and keyboard. Controller and multiplayer untested. ([#26](https://github.com/leolobato/playden-ps5/issues/26), firmware 9.40) |
| [Wobbly Life](https://store.steampowered.com/app/1211020) | ✅ Works | 0.10 | — | Single-player with DualSense or USB mouse and keyboard; multiplayer does not work. Loading takes about a minute. ([#25](https://github.com/leolobato/playden-ps5/issues/25), firmware 9.40) |
| [IT Specialist Simulator](https://store.steampowered.com/app/3266090) | ✅ Works | 0.10 | — | USB mouse and keyboard; controller untested. ([#21](https://github.com/leolobato/playden-ps5/issues/21), firmware 9.40) |
| [Gas Station Simulator](https://store.steampowered.com/app/1149620) | ✅ Works | 0.10 | — | Medium graphics preset, USB mouse and keyboard; controller untested. ([#17](https://github.com/leolobato/playden-ps5/issues/17), firmware 9.40) |
| [Teardown](https://store.steampowered.com/app/1167630) | ✅ Works | 0.10 | OpenGL | Set Game settings › Graphics = **OpenGL**. USB mouse and keyboard works; controller untested. Tester recommends borderless window, 1920×1080, render scale 75%, and changing the two settings below render scale from High to Low. ([#16](https://github.com/leolobato/playden-ps5/issues/16#issuecomment-6169881987), firmware 9.40) |
| [PEAK](https://store.steampowered.com/app/3527290) | ✅ Works | 0.10 | Unity | Default settings, DualSense. Menus and hosting a game work. Cloud saves did not sync, and other players probably cannot join. ([#44](https://github.com/leolobato/playden-ps5/issues/44), firmware 12.40) |
| [Portal](https://store.steampowered.com/app/400) | ✅ Works | 0.10 | Source · D3D9 | Default settings, DualSense. The mouse does not work in the menus, and no button opens the options (Esc) during play. ([#18](https://github.com/leolobato/playden-ps5/issues/18), firmware 13.60) |
| [Tomb Raider](https://store.steampowered.com/app/203160) | ✅ Works | 0.10 | — | USB keyboard works. The DualSense was not detected at start, but started to work after the game ran for a while. ([#11](https://github.com/leolobato/playden-ps5/issues/11), firmware 12.00 with a modified kit manifest) |
| [Fallout 4](https://store.steampowered.com/app/377160) | ✅ Works | 0.10 | Creation Engine · D3D11 · DXVK · 64-bit | Set Program to `Fallout4.exe`. Sound effects, voices and music play: the game's xWMA sound goes through the kit's media backend (`winegstreamer`). A bottle made with an earlier kit gets the backend on the next Play. Verified loading a save in the pre-war house, walking, doors and talking to Codsworth. |
| [Metal Wolf Chaos XD](https://store.steampowered.com/app/820630) | ✅ Works | 0.10 | — · D3D11 · 32-bit | The opening movie plays (Options skips it) and the main menu responds to the controller. Do not set `PW_MEDIA_DISABLE=1`: without the media backend the game crashes. Gameplay untested on r10; on the r7 development kit a save loaded into controllable gameplay. |
| [Mullet Mad Jack](https://store.steampowered.com/app/2111190) | ✅ Works | 0.10 | Unity · D3D11 · 64-bit | Plays with the controller. The account's Cloud saves did not show in the game; not checked again since. |
| [DAEMON X MACHINA](https://store.steampowered.com/app/1167450) | ✅ Works | 0.10 | — | Plays with the DualSense. The game's Select/Back is on the Share button, and pressing Share or PS makes the game stop responding: rebind Select/Back (for example to the touchpad) or play with a USB mouse and keyboard. ([#55](https://github.com/leolobato/playden-ps5/issues/55), firmware 12.40) |
| [PlateUp!](https://store.steampowered.com/app/1599600) | 🟡 Partial | 0.10 | Unity · 64-bit | Reaches the menu at 30+ fps with the DualSense (default and **Arrow keys** presets); the game's FPS limit options in Settings › Graphics reduce lag. Typing the profile name needs a USB keyboard. Loading takes 30–45 seconds. ([#3](https://github.com/leolobato/playden-ps5/issues/3), firmware 9.40) |
| [Retro Rewind - Video Store Simulator](https://store.steampowered.com/app/3552140) | 🟡 Partial | 0.10 | — | Reaches the menu and plays well with a USB mouse and keyboard; the DualSense does not work. ([#7](https://github.com/leolobato/playden-ps5/issues/7), firmware 9.40) |
| [No Rest for the Wicked](https://store.steampowered.com/app/1371980) | 🟡 Partial | 0.10 | — | Plays with the DualSense (default, **Mouse** and **Arrow keys** presets). Creating a realm needs a typed name, and there is no on-screen keyboard; a USB keyboard was not tried. Cloud sync fails. ([#14](https://github.com/leolobato/playden-ps5/issues/14), firmware 12.02) |
| [BattleBlock Theater](https://store.steampowered.com/app/238460) | 🟡 Partial | 0.10 | — | Single player works with the DualSense. Multiplayer and the Steam inventory do not work, so yarn, gems, weapons and heads cannot be earned or used. ([#40](https://github.com/leolobato/playden-ps5/issues/40), firmware 13.60) |
| [Antichamber](https://store.steampowered.com/app/219890) | 🟡 Partial | 0.10 | Unreal 3 · D3D9 · 32-bit | Reaches the first room; the controller is ignored. With Controller preset = **Mouse** the stick turns the view; walking needs a USB keyboard (WASD) or Button mapping. |
| [Return to Castle Wolfenstein](https://store.steampowered.com/app/9010) | 🟡 Partial | 0.10 | id Tech 3 · OpenGL · 32-bit | Starts and plays its intro. The game has no controller support: use a USB mouse and keyboard, or Controller preset = **Mouse** (not tested yet). |
| [Call of Juarez Gunslinger](https://store.steampowered.com/app/204450) | 🟡 Partial | 0.10 | D3D9 · DXVK · 32-bit | Reaches the menu with the controller. Play Game shows the story intro movie with subtitles, then the first chapter loads (about two minutes) into controllable gameplay. Plays on in the first chapter (tested for several minutes) since Wine patch 0972 fixed the XAudio2 mixer crash, but gameplay stops and starts: about three seconds frozen, then about three seconds moving. |
| [Bloody Spell](https://store.steampowered.com/app/992300) | 🟡 Partial | 0.10 | — | Plays with the DualSense. With the game in Chinese, all text shows as empty boxes; English works. ([#46](https://github.com/leolobato/playden-ps5/issues/46), firmware 12.00) |
| [Balatro](https://store.steampowered.com/app/2379780) | ❌ Broken | 0.10 | LÖVE · OpenGL · 64-bit | Stops at start-up: its file system (PhysFS) fails to start. |
| [Grand Theft Auto IV: The Complete Edition](https://store.steampowered.com/app/12210) | ❌ Broken | 0.10 | RAGE · D3D9 · 32-bit | Starts through Rockstar's launcher, which shows "Request not supported" and then "Unable to install prerequisites". The launcher needs to start other programs, which the runtime can't do yet. ([#53](https://github.com/leolobato/playden-ps5/issues/53), firmware 12.40) |
| [Grand Theft Auto: Vice City](https://store.steampowered.com/app/12110) | ❌ Broken | 0.10 | RenderWare · D3D8 · 32-bit | Stays on the loading spinner: it starts through Rockstar's launcher, which needs to start other programs, which the runtime can't do yet. ([#54](https://github.com/leolobato/playden-ps5/issues/54), firmware 12.40) |
| [Look Outside](https://store.steampowered.com/app/3373660) | ⛔ Refused | 0.10 | RPG Maker (NW.js) · 64-bit | Runs on NW.js (a built-in web browser), which Playden can't start on the PS5 yet. |
| [BLUE REVOLVER](https://store.steampowered.com/app/439490) | ✅ Works | 0.10 | LÖVE · OpenGL · 64-bit | Default settings. The intro, the language menu and the story opening work with the controller. The earlier PhysFS start-up failure no longer happens; its cause is unknown. Gameplay not tested on r6. |
| [Fellowship](https://store.steampowered.com/app/2352620) | ❌ Broken | 0.10 | Unreal · 64-bit | Shows "The UE-fellowship Game has crashed and will close" (Fatal error) at start-up. ([#2](https://github.com/leolobato/playden-ps5/issues/2), firmware 13.60) |
| [BeamNG.drive](https://store.steampowered.com/app/284160) | ❌ Broken | 0.10 | D3D11 · DXVK · 64-bit | Launcher reports error 50 (request not supported). Starting `Bin64/BeamNG.drive.x64.exe` directly with Graphics = **DXVK** and `-gfx dx11` returns to the console home screen before drawing a frame. Logs show failed virtual-memory reservations followed by `chrome_elf.dll` initialization failure. ([#23](https://github.com/leolobato/playden-ps5/issues/23), firmware 9.40) |
| [Far Cry 3 Blood Dragon](https://store.steampowered.com/app/233270) | ❌ Broken | 0.10 | Dunia 2 · D3D11 · 32-bit | Needs Ubisoft Connect. |
| [Spec Ops: The Line](https://store.steampowered.com/app/50300) | ❌ Broken | 0.11 (dev) | Unreal 3 · D3D9 · 32-bit | Exits after about 30 seconds without a picture, before Direct3D starts. Not its redistributables: Playden installs those now, and the game still stops the same way. |
| [Singularity](https://store.steampowered.com/app/42670) | ❌ Broken | 0.11 (dev) | Unreal 3 · D3D9 · 32-bit | Crashes a few seconds after start on a null read (`Unhandled page fault on read access to 00000000`), before a picture. |
| [Subnautica 2](https://store.steampowered.com/app/1962700) | ❌ Broken | 0.10 | Unreal · 64-bit | Cannot start with `-dx11`; without it, reports DirectX 12 is unsupported. ([#20](https://github.com/leolobato/playden-ps5/issues/20), firmware 13.60) |
| [Hotline Miami](https://store.steampowered.com/app/219150) | ❌ Broken | 0.10 | GameMaker · D3D8 · 32-bit | Steam starts it through a .NET launcher, so it is refused. With Game settings › Program = `HotlineMiami_Original.exe` it starts, but the keyboard and controller do nothing. |
| [STAR WARS Empire at War: Gold Pack](https://store.steampowered.com/app/32470) | ❌ Broken | 0.10 | Alamo · D3D9 · DXVK · 64-bit | Set Program to `GameData/StarWarsG.exe` (`runme.exe` only starts a launcher), Graphics to DXVK and Controller preset to **Mouse**. The intro and the main menu's 3D scene play and the menus work with the pointer (on the very first start the menu was 4:3 and ignored clicks; later starts were fine). Starting any tutorial crashes while the map loads (NULL read in `ucrtbase.dll`). |
| [Need for Speed Most Wanted](https://store.steampowered.com/app/1262560) | ⛔ Refused | 0.10 | Chameleon · D3D11 · 32-bit | Needs the EA app. |

## Kits

Kit versions are `<generation>.<revision>`, for example 0.10:

- A new **revision** (0.10 → 0.11) adds fixes and features. Games you already set up keep working; there is nothing
  to do.
- A new **generation** (0.x → 1.0) rebuilds the kit, for example on a newer Wine. Use Game settings › Recreate bottle
  for games set up on an earlier generation.

Development builds carry the version they lead to, such as `0.11-dev.20261020.1`.

| Kit | What changed for games |
| --- | --- |
| 0.10 | First stable kit. Windows video and audio decoding through GStreamer and FFmpeg, so games play their movies and compressed audio (Fallout 4, Bulletstorm, Metal Wolf Chaos XD). Source games (Portal 2, Team Fortress 2, Black Mesa) no longer hang at start-up with the controller. Call of Juarez Gunslinger's audio no longer freezes the game. A game that crashes now quits instead of freezing. |
| 0.11 (in development) | Direct3D 12 through vkd3d-proton: Playden turns it on for games that only use Direct3D 12, and Game settings › Direct3D 12 turns it on for others (Unity and Unreal games then get `-force-d3d12` or `-dx12`). 64-bit games can reserve more address space (Cyberpunk 2077). |
| Earlier | Test kits before the first stable release, named by letter and revision (D to H r9). Kit 0.10 follows H r9: games set up on H r6 or later need nothing, and games set up earlier need Recreate bottle. |

To see which kit your console has, run the [log script](tools/collect_logs.py): it prints the kit version.

## Notes for all games

- **32-bit games** can run through an x86 translator, which is slower than native. For 32-bit OpenGL games, Mesa
  runs inside the translator too.
- **New games can stay at *Waiting for the PS5*** when ShadowMountPlus stops registering titles. Reload
  ShadowMountPlus.
- **Direct3D 12 games** need a kit with vkd3d-proton (0.11 or later). Playden turns it on for games that only use
  Direct3D 12. For games that can also use Direct3D 11, set Game settings › Graphics = DXVK and Direct3D 12 =
  vkd3d-proton. Its own variables (`VKD3D_DEBUG`, `VKD3D_CONFIG`, …) go in Environment variables.
- **Denuvo Anti-Tamper** games (for example Hi-Fi RUSH) cannot run, and support is not planned.
- **Kernel anti-cheat** games (for example Easy Anti-Cheat or BattlEye in kernel mode) cannot run.
- Games that need a **third-party launcher or account** (EA app, Ubisoft Connect, Rockstar) are not supported yet.
