# Eight-channel TRACKER

This custom firmware is based on monome/teletype main at `e390bac`. Each scene holds eight independent 64-step patterns. TRACKER tracks 1–8 correspond to script pattern numbers `0–7`. For example, `PN 7 63 -120` writes -120 to the last step of track 8.

- `P.N`, the `PN` operators and `PN.MAP`, and the optional pattern argument to `Q.2P` and `Q.P2` clamp to `0–7`. The working pattern defaults to 0. `INIT.P` accepts `0–7` and ignores other values. `INIT.P.ALL` clears all eight.
- The Turtle’s full X range and default fence are `0–7`. Use `@F 0 0 3 63` in a scene’s initialization script to recreate the four-track fence. Explicit fences within `0–3` keep their previous behavior.
- The OLED shows four tracks at a time. Left and Right cross the bank boundary between tracks 4 and 5. One or two dots beside the step numbers identify the visible bank. Grid Control Mode shares the OLED bank. The second key down in the rightmost control column switches banks and lights for tracks 5–8.
- Script names, argument counts, and token IDs are unchanged. The existing parser accepts all eight pattern numbers through its normal integer token path.

## Scenes and flash

USB text scenes with four columns in the `#P` section load into tracks 1–4. Tracks 5–8 start at default values. An export writes four columns when tracks 5–8 have default metadata and zero values. Otherwise it writes eight tab-separated columns, including all 64 stored values per track. Stock v5 only reads the first four columns and can clamp newer pattern arguments in scripts. Keep the original eight-track files when moving scenes between firmware versions. The USB format carries pattern length, wrap, loop bounds, and values. The live playhead resets on USB import.

The binary flash layout has a new format marker and keeps all 32 slots. Plan to export scenes to USB with the old firmware before a future update. The standard updater erases internal flash, and the new firmware initializes scene storage and device settings after the first-run confirmation. Then restore the text scenes from USB. Calibration and device settings are outside the scene text format and will need to be set again. USB import expects files named `tt00.txt` through `tt31.txt`; exports normally use names such as `tt00s.txt`.

The AVR32 build reserves 192 KiB for scene flash. Stored scripts group token tags and operands separately to remove per-token padding. The compiler checks the storage structure against that reservation, and the linker checks the program against the remaining flash. USB import and export reuse one heap-allocated working scene because the structure exceeds the target’s 8 KiB stack.

## Build and verification

Clone with submodules and install Ragel 6.x. Run these from the repository root.

```sh
git submodule update --init --recursive
make -C tests test-travis
make -C simulator test
```

The tests exercise the parser and interpreter on all eight tracks, channel and step bounds, full-length insertion and removal, empty playback, queue transfers, Turtle navigation and access, and pattern reset. Serialization tests cover byte-for-byte round trips for the stock presets, eight-track round trips, and bounded handling of oversized rows, columns, and numbers.

For the hardware image, use the archived [monome AVR32 toolchain](https://github.com/monome/avr32-toolchain-linux) on x86-64 Linux. Extract the GNU toolchain 3.4.3 archive, add its `bin` directory to `PATH`, and unpack the AVR32 headers into the toolchain’s `avr32/include` directory. The repository’s GitHub Actions firmware job performs the same setup.

```sh
make -C module
avr32-size -A module/teletype.elf
```

The AVR32 archive contains Linux x86-64 executables. On Apple Silicon macOS, run the host tests natively and use the Linux CI job or a compatible Linux environment for the AVR32 image. The command-line simulator models the scripting engine. Physical OLED and Grid behavior, USB media transfers, and hardware timing require testing on a module.
