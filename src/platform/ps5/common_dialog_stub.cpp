// common_dialog_stub.cpp - Link-only import of libSceCommonDialog for the system keyboard.
// From ps5-native-app-boilerplate's system-keyboard example (BlackBearReloaded, GPL-3.0-or-later).
// SPDX-License-Identifier: GPL-3.0-or-later

// The payload SDK has no stub for this module. Linked as a shared library named
// libSceCommonDialog.sprx, this symbol becomes a system import; the body is never
// linked into or shipped with Playden.
extern "C" int sceCommonDialogInitialize() { return 0; }
