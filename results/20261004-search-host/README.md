# Controller search host evidence

The 3840×2160 `keyboard.png` capture comes from the graphical `host-fixtures`
consumer with RmlFocus `31d2d8fd39ca3e3bb3ba91a43f50561c04429d57`:

```sh
build/host-fixtures/playden-launcher --developer-fixtures --keys R1,Triangle \
  --run-for-seconds 2 --shot /tmp/playden-search.ppm
ffmpeg -y -i /tmp/playden-search.ppm -pix_fmt rgb24 keyboard.png
```

The `search` behavioral test uses semantic controller commands to enter text,
confirm it, reopen and cancel, preserve game identity and opener focus, produce
an empty result, and recover through Clear search. Screenshot mode disables
host swap synchronization. This is layout/controller-logic evidence, not PS5
performance or physical DualSense acceptance.
