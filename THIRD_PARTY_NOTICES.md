# Third-party notices

MiBoX is built out of other people's work, and this file says
whose. It lists **every** component the build pulls in, where it comes from, the
pin it is held at, and the licence it is under.

Two things about how to read it:

* **The vendored components ship their licence next to their source.** Everything
  checked into `ThirdParty/` carries its own `LICENSE` / `COPYING` file, so this
  file names the holder and points at the copy rather than duplicating it.
* **The fetched components carry their licence in their own repository.** The
  thirty-odd packages below `cmake/J37Dependencies.cmake` fetches are pinned to a
  tag or a commit; their licence text lives in the checkout CPM downloads, and
  the upstream link in the table is where to read it without building anything.

The two licence families this repository has to reproduce in full - MIT and
BSD 3-Clause - are at the bottom under [Licence texts](#licence-texts), because
almost every component here is under one of them.

## Vendored in `ThirdParty/`

These are checked into the repository at the exact pin `cmake/J37Dependencies.cmake`
would otherwise fetch, so a fresh clone configures and builds offline. The licence
column names the file in the tree that carries the text.

| Component | Version / pin | Licence | Licence file |
| --- | --- | --- | --- |
| **RTNeural** - Jatin Chowdhury | `95c3c0f9` | BSD 3-Clause, Copyright (c) 2020, jatinchowdhury18 | `ThirdParty/RTNeural/LICENSE` |
| **farbot** - Fabian Renn | `d8f132c2` | MIT, Copyright (c) 2019 Fabian Renn | `ThirdParty/farbot/LICENSE` |
| **glm** (OpenGL Mathematics) - G-Truc Creation | `1.0.3` | The Happy Bunny License **or** the MIT License, by the user's choice | `ThirdParty/glm/copying.txt` |
| **nlohmann/json** - Niels Lohmann | `v3.12.0` | MIT, Copyright (c) 2013-2025 Niels Lohmann | `ThirdParty/nlohmann_json/LICENSE.MIT` |
| **pocketfft** - Max-Planck-Society | `c90e55b3` | BSD 3-Clause, Copyright (C) 2010-2018 Max-Planck-Society | `ThirdParty/pocketfft/LICENSE.md` |
| **signalsmith-stretch** - Signalsmith Audio | `1.4.0` | MIT, Copyright (c) 2022 Geraint Luff / Signalsmith Audio Ltd. | `ThirdParty/signalsmith-stretch/LICENSE.txt` |
| **signalsmith-linear** (inside the stretch checkout) | `0.6.4` | MIT, Copyright (c) 2022 Geraint Luff / Signalsmith Audio Ltd. | `ThirdParty/signalsmith-stretch/signalsmith-linear/LICENSE.txt` |
| **SoundTouch** - Olli Parviainen | `2.4.1` (`0047e0b1`) | **LGPL 2.1** - see the note below | `ThirdParty/soundtouch/COPYING.TXT` |
| **xsimd** - xtensor-stack | `14.3.0` | BSD 3-Clause, Copyright (c) 2016 Johan Mabille, Sylvain Corlay, Wolf Vollprecht, Martin Renou; 2016 QuantStack; 2018 Serge Guelton | `ThirdParty/xsimd/LICENSE` |

## Fetched by CMake (CPM)

Pinned in `cmake/J37Dependencies.cmake`. "Licence" is what the upstream project
publishes; the link is the repository whose own licence file governs. Being
fetched is not the same as being linked: the plugin's link line carries only the
libraries `Source/` actually includes, and
`tests/linkline/check_linkline.py` fails a build when it does not. A component
fetched and not linked is a candidate for a future stage rather than part of a
binary - see
[docs/plugin-analysis.md](docs/plugin-analysis.md#5-the-link-line-is-larger-than-the-code).

| Component | Pin | Licence | Upstream |
| --- | --- | --- | --- |
| **JUCE** | `9.0.4` | **AGPLv3 or commercial** | <https://github.com/juce-framework/JUCE> · <https://juce.com/legal/juce-9-licence/> |
| **chowdsp_utils** | `v2.4.0` | **per module**: `chowdsp_dsp_utils` **GPLv3**, `chowdsp_math` BSD, `chowdsp_simd` BSD - see the note below | <https://github.com/Chowdhury-DSP/chowdsp_utils> |
| **melatonin_inspector** | `9c483f86` | MIT | <https://github.com/sudara/melatonin_inspector> |
| **melatonin_blur** | `v1.4` | MIT | <https://github.com/sudara/melatonin_blur> |
| **foleys_gui_magic** | `d53d998e` | BSD 3-Clause (stated in the module header) | <https://github.com/ffAudio/foleys_gui_magic> |
| **fftconvolver** | `f2cdeb04` | MIT | <https://github.com/HiFi-LOFi/FFTConvolver> |
| **readerwriterqueue** | `v1.0.7` | Simplified BSD (BSD-2-Clause) | <https://github.com/cameron314/readerwriterqueue> |
| **xsimd** (fallback for the vendored copy) | `14.3.0` | BSD 3-Clause | <https://github.com/xtensor-stack/xsimd> |
| **signalsmith-stretch** (fallback) | `1.4.0` | MIT | <https://github.com/Signalsmith-Audio/signalsmith-stretch> |
| **signalsmith-linear** (fallback) | `0.6.4` | MIT | <https://github.com/Signalsmith-Audio/linear> |
| **pocketfft** (fallback) | `c90e55b3` | BSD 3-Clause | <https://github.com/mreineck/pocketfft> |
| **nlohmann_json** (fallback) | `v3.12.0` | MIT | <https://github.com/nlohmann/json> |
| **farbot** (fallback) | `d8f132c2` | MIT | <https://github.com/hogliux/farbot> |
| **glm** (fallback) | `1.0.3` | Happy Bunny / MIT | <https://github.com/g-truc/glm> |
| **RTNeural** (fallback) | `95c3c0f9` | BSD 3-Clause | <https://github.com/jatinchowdhury18/RTNeural> |
| **SoundTouch** (fallback) | `0047e0b1` | **LGPL 2.1** | <https://codeberg.org/soundtouch/soundtouch> |
| **sokol_gfx** | `2e75443d` | zlib | <https://github.com/floooh/sokol> |
| **q_dsp** / **cycfi_infra** | `v1.0.1` / `2024_MAY` | MIT | <https://github.com/cycfi/q> |
| **AudioFFT** | `0893b532` | MIT | <https://github.com/HiFi-LoFi/AudioFFT> |
| **dattorro-verb** | `41e976a0` | MIT | <https://github.com/el-visio/dattorro-verb> |
| **dr_libs** | `wav-0.14.5` | Unlicense **or** MIT-0, the user's choice | <https://github.com/mackron/dr_libs> |
| **whetstone** | `83d715a0` | BSD 3-Clause | <https://github.com/unicornsasfuel/whetstone> |
| **multiband-transient-shaper** | `7c77e23c` | MIT | <https://github.com/sevagh/multiband-transient-shaper> |
| **transdes** | `0.0.3` | MIT | <https://github.com/ylmrx/transdes> |
| **kissfft** | `131.1.0` | BSD-3-Clause (`SPDX-License-Identifier` in its `COPYING`) | <https://github.com/mborgerding/kissfft> |
| **imgui** | `v1.91.5` | MIT | <https://github.com/ocornut/imgui> |
| **yasio** | `v4.4.1` | MIT, with the Anti-996 Licence appended in the same file | <https://github.com/yasio/yasio> |
| **soundpipe** | `3efb43bd` | MIT | <https://github.com/paulbatchelor/soundpipe> |
| **ChowDSP_ChowCentaur** | `2c0414f3` | BSD 3-Clause | <https://github.com/AviateAudio/ChowDSP_ChowCentaur> |
| **libsndfile** | `b9103bd4` | **LGPL 2.1** | <https://github.com/libsndfile/libsndfile> |
| **aubio** | `ad5cf975` | **GPL 3.0** | <https://github.com/aubio/aubio> |
| **VCVRack/Rack** (`rack_dsp`) | `v2.6.6` | GPL-3.0-or-later, with a plugin exception and a commercial option - see the note below | <https://github.com/VCVRack/Rack> |
| **VCV-Prototype** | `7cb75fda` | GPLv3 (upstream) | <https://github.com/VCVRack/VCV-Prototype> |
| **iPlug2** | `b1aac7ec` | permissive, custom (upstream) | <https://github.com/iPlug2/iPlug2> |
| **pffft** - Martin Pichler | `e1dbebc9` | BSD-like (FFTPACK lineage; upstream states no SPDX id) | <https://github.com/marton78/pffft> |
| **Yin-Pitch-Tracking** | `69483b04` | see upstream | <https://github.com/ashokfernandez/Yin-Pitch-Tracking> |
| **audio_fft** | `922b30a8` | see upstream | <https://github.com/adamski/audio_fft> |
| **pitch_detector** | `d3b60970` | see upstream | <https://github.com/adamski/pitch_detector> |

### Optional renderer backends

Fetched only when `J37_RENDER_BACKEND` selects one of them. None of the three is
in the default build.

| Component | Pin | Licence | Upstream |
| --- | --- | --- | --- |
| **bgfx** (via `bgfx.cmake`) | `eb88f6be` | CC0-1.0 wrapper, BSD 2-Clause core | <https://github.com/bkaradzic/bgfx.cmake> |
| **wgpu-native** | `250eeb67` | Apache 2.0 (MIT for parts) | <https://github.com/gfx-rs/wgpu-native> |
| **Dawn** | `32f77c02` | BSD 3-Clause (upstream) | <https://github.com/google/dawn> |

### Development-only

Fetched only under `-DJ37_BUILD_TESTS=ON`, which no shipped build sets, and
`-DJ37_BUILD_HOTPATH_BENCH` for the benchmark beside it.

| Component | Pin | Licence | Upstream |
| --- | --- | --- | --- |
| **Catch2** | `v3.9.1` | BSL-1.0 | <https://github.com/catchorg/Catch2> |
| **nanobench** | `v4.6.0` | MIT | <https://github.com/martinus/nanobench> |
| **CPM.cmake** (the fetch tool itself) | `0.40.2` | MIT | <https://github.com/cpm-cmake/CPM.cmake> |

### Vendor vector libraries

Discovered on the build machine rather than fetched - see
`cmake/J37VendorLibraries.cmake`. Both are optional accelerators, and the
portable path is used when neither is present.

**IPP is not merely probed, and that is the one licence fact here that depends on
whose machine built the binary.** The build throws the IPP linking-model macro
JUCE's own FFT engine is gated on, so on a machine with the single-threaded
static IPP layout `juce::dsp::FFT` - and with it the Convolution behind the IR
cabinet - is Intel's rather than JUCE's, and the binary carries Intel's IPP
object code. Whether that is redistributable is a question for Intel's own terms
(and Intel's FAQ on the Simplified Software License), exactly as the copyleft
components below are for theirs; a build on a machine without IPP, and every
macOS build, is unaffected. On Apple the vDSP that runs is JUCE's, from
Accelerate, under the Xcode licence - this repository throws no switch for it.

| Component | Platform | Licence |
| --- | --- | --- |
| **Intel Integrated Performance Primitives (IPP)** | Windows / Linux | Commercial, free of charge for use with Intel silicon under the Intel Simplified Software License; installed by the user, never vendored here - and **linked into a binary built where it is present** |
| **Apple Accelerate / vDSP** | macOS | Apple SDK, covered by the Xcode licence |

## What the copyleft components mean for a binary

Most of the list above is MIT or BSD and only asks for the notices in this file.
The components below are the exceptions, and they are the reason this section
exists. Read them with the link line in front of you, because **what a package
is licensed under and what a shipped binary links are two different questions** -
and today they do not agree.

* **JUCE 9.0.4** is AGPLv3 or a commercial JUCE licence. A distributed plugin
  that links it is under one of those two; there is no third answer.
* **`chowdsp_dsp_utils` is GPLv3.** It is one module of `chowdsp_utils`, whose
  other modules are not: `chowdsp_math` and `chowdsp_simd` are BSD. The plugin
  includes `<chowdsp_dsp_utils/chowdsp_dsp_utils.h>` and links the module, so a
  distributed binary is bound by GPLv3 unless a non-GPL licence is bought from
  the library's author - the licence file says so in as many words. This is the
  first thing to settle before publishing a build.
* **SoundTouch 2.4.1** is LGPL-2.1 and **used to be linked statically** - which
  LGPL-2.1 section 6 makes a deliberate decision about object files or a
  relinkable form. It is no longer on the link line: nothing under `Source/`
  called it, so a shipped binary carries none of it, and the vendored copy and
  its licence text stay for the stage that would use a stretcher.
* **libsndfile** (LGPL-2.1) is fetched and **not linked**, and nothing under
  `Source/` includes it either - see
  [plugin-analysis.md, §5](docs/plugin-analysis.md#5-the-link-line-is-larger-than-the-code).
* **`rack_dsp`** comes from VCVRack/Rack, which is GPL-3.0-or-later **with a
  plugin exception**: a plugin may link Rack's API regardless of its own licence
  as long as it is distributed free of charge; selling it needs Rack's
  commercial option. It is fetched and **not linked**, so that exception is not
  on the critical path of this binary at all.
* **aubio** is fetched by CMake and is **GPL-3.0**, but it is not linked and not
  included - it cannot affect a binary. It is dead dependency weight rather than
  a licensing problem, and it is on the list to remove.

Before publishing a binary, read the three documents that already say this in
the build's own words: `cmake/J37Dependencies.cmake` (what each package is for),
[docs/plugin-analysis.md](docs/plugin-analysis.md#5-the-link-line-is-larger-than-the-code)
(which entries the source actually uses), and
[docs/plugin-improvement-plan.md](docs/plugin-improvement-plan.md#priority-1--what-a-distributed-binary-owes)
(what to do about it).

## Licence texts

### MIT License

```
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

The copyright line that belongs above this text is the one in the component's
own table row - each holder's notice is theirs, and the vendored checkouts carry
it in the file named there.

### BSD 3-Clause License

```
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software without
   specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### LGPL 2.1 and AGPLv3

Neither is reproduced here, and neither is here by accident:

* the **LGPL 2.1** text ships with SoundTouch at
  `ThirdParty/soundtouch/COPYING.TXT`, and is also at
  <https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt>;
* the **AGPLv3** text ships with JUCE at `JUCE/LICENSE.md` inside the submodule,
  and is also at <https://www.gnu.org/licenses/agpl-3.0.html>.

## Fonts

The editor draws its own type: the panel lettering is JUCE text rendering with
the platform's default typeface, the tape-deck artwork is generated geometry, and
no font file is bundled, embedded or redistributed by this repository. There is
therefore no font licence to record here - an earlier revision of this file
listed a font collection that the plugin has never used.
