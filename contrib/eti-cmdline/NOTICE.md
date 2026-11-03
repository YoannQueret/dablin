# Vendored: eti-cmdline

This directory contains a trimmed copy of **eti-cmdline**, vendored from
<https://github.com/JvanKatwijk/eti-stuff> (subdirectory `eti-cmdline`),
written by Jan van Katwijk (J.vanKatwijk@gmail.com), licensed under the
**GNU General Public License, version 2 or (at your option) any later version**
(see `LICENSE`).

eti-cmdline turns an RTL-SDR dongle's raw samples into a DAB/DAB+ ETI stream
on stdout. dablin builds it as part of its own CMake build and launches it as
a subprocess (`EtiCmdlineETISource`, same as it would an externally-installed
copy) so that live SDR reception works without having to separately obtain,
build or install eti-cmdline.

## What was changed from upstream

- All input device backends other than RTL-SDR were removed (AirSpy, SDRplay,
  SDRplay v3, HackRF, LimeSDR, PlutoSDR, raw/WAV/XML file readers, rtl_tcp),
  along with their SDK-specific headers - dablin only ever drives this as an
  RTL-SDR live source, and the other backends pull in unrelated SDK
  dependencies that would otherwise be required to configure the build.
- The optional WAV-dump feature (`-DDUMPING`) and its `libsndfile` dependency
  were dropped along with it (dead weight for this use case).
- `CMakeLists.txt` was rewritten to only configure the RTL-SDR build: the
  `FFTW3f` and `libsamplerate` checks were removed since neither is actually
  used anywhere in the compiled sources (the FFT is the vendored, BSD-licensed
  KISS FFT under `src/support/fft/`, not FFTW; nothing in this trimmed source
  tree calls into libsamplerate) - these were leftover requirements from
  upstream's shared CMakeLists template across its sibling device backends.
  The `install`/`uninstall` targets were also removed, since the resulting
  `eti-cmdline-rtlsdr` binary is only ever used in place, next to `dablin_gtk`.
- The Viterbi decoder is built using the portable `spiral-no-sse.c` variant
  only (no `-DX64_DEFINED`/SSE or `-DRPI_DEFINED`/NEON-specific builds).
- Fixed a crash in `devices/rtlsdr-handler/rtlsdr-handler.cpp`: the device
  enumeration loop passed uninitialized `char*` pointers to
  `rtlsdr_get_device_usb_strings()`, which writes into them rather than
  allocating them itself - this reliably segfaulted on startup. Replaced
  with fixed-size stack buffers. No other DSP/decoding logic was modified.
- Fixed a missing `break` in `main.cpp`'s option-parsing `switch` (the `-L`
  case, select RTL-SDR device by serial number): without it, `-L <serial>`
  fell through into the next compiled case in this RTL-SDR-only build and
  silently also set `isSilent = true`.
- Raised `cmake_minimum_required` from 3.5 to 3.10, matching dablin's own
  requirement and silencing a "compatibility with CMake < 3.10 will be
  removed" deprecation warning.
- Silenced a handful of `-Wunused-parameter` warnings (`protection.cpp`,
  `fic-handler.cpp`, `eti-generator.cpp`, `rtlsdr-handler.cpp`) by commenting
  out the unused parameter names; no behavior was changed. Note:
  `rtlsdrHandler::restartReader()`'s frequency parameter was already unused
  upstream - the function retunes to the `frequency` member instead, which
  happens to equal the passed-in value at dablin's only call site (right
  after construction), so this is a pre-existing, currently-harmless quirk
  rather than something introduced or fixed here.

## Updating

To refresh this vendored copy against a newer upstream release, re-clone
<https://github.com/JvanKatwijk/eti-stuff>, copy over the `eti-cmdline`
subdirectory, and re-apply the trimming described above (the device
directories, Find modules and `CMakeLists.txt` edits listed here).
