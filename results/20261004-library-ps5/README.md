# Native PS5 library and search check

The FAKE77031 developer-fixture build rendered the library on PS5 at an actual
3840×2160 surface and viewport. `library-native-3840x2160.png` is a lossless
conversion of the renderer’s P6 framebuffer capture; the manifest records the
original PPM hash and size. The two JPEGs are 1920×1080 HDMI captures, not native
resolution evidence.

Injected controller input opened Library (R1), Search (Triangle), entered `q`
(Cross), cancelled (Circle), and opened the previously focused game details
(Cross). Holding Options exited with status 0. Physical-controller acceptance
remains pending. The fixture is synthetic and does not represent owned games.

This is visual and routing evidence only. The capture adds a GPU readback and
file write and disqualifies this run as sustained performance evidence. Recorded
frame intervals also exceed the 60 Hz target. Refresh remains configured, real
presentation counters are unavailable, and RSS is unknown: the SDK-sized sysctl
request failed with ENOMEM (12). A subsequent diagnostic build confirmed that
the console advertises a 1096-byte record instead of the SDK’s 1088 bytes.
No 4K60 or memory-growth acceptance is claimed.

The log redacts the local user ID and controller handle.
