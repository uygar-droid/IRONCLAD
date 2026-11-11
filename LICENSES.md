# IRONCLAD — Licences of dependencies and assets

Last updated: 2026-11-11 (Session 1).
This is an engineering assessment, **not legal advice**. Before commercial release, have a
lawyer review this file, the final dependency list and the installer's third-party notices.

Principle (spec §3): no dependency's licence is assumed to cover another. Every item is listed
separately, with the evidence used.

## 1. Code linked into the shipped product

| Component | Licence | Evidence | Obligations for a closed-source commercial product |
|---|---|---|---|
| NeuralAmpModelerCore | MIT (© 2023 Steven Atkinson) | `LICENSE` in repo | Include copyright + licence text in distributed notices. |
| NeuralAmpModelerPlugin (only code we port) | MIT (© 2022 Steven Atkinson) | `LICENSE` in repo | Same; mark ported files with origin. Do **not** reuse its branding, images or fonts as IRONCLAD identity. |
| AudioDSPTools | MIT (© 2023 Steven Atkinson) | `LICENSE` in repo | Notice. |
| Eigen | MPL-2.0 (primarily; some files BSD/Apache/other) | `COPYING.README`, `COPYING.MPL2` at pinned commit | File-level copyleft: unmodified use only needs the notice + telling users where the Eigen source is available. If we **modify** Eigen files, those files' changes must be published under MPL-2.0. Define `EIGEN_MPL2_ONLY` so no LGPL-only code is pulled in. |
| nlohmann/json | MIT | header licence block | Notice. |
| iPlug2 | zlib-style | `LICENSE.txt` | Don't misrepresent origin; mark altered sources; keep notice in source distributions. Acknowledgement in docs appreciated, not required. |
| └ Cockos WDL | zlib-like | iPlug2 `LICENSE.txt` | Same style as zlib. |
| └ NanoVG / NanoSVG | zlib | iPlug2 `LICENSE.txt` | — |
| └ MetalNanoVG | MIT | iPlug2 `LICENSE.txt` | Notice. |
| └ Skia (if chosen) | BSD-3-Clause | iPlug2 `LICENSE.txt` mentions BSD | Notice; no endorsement. |
| └ iPlug2 WebView extra (only if used) | MIT | iPlug2 `LICENSE.txt` | Notice. |
| VST3 SDK | **MIT since VST 3.8** (Oct 2025); GPLv3/proprietary options discontinued | Steinberg VST 3 Licensing page and 3.8 release notes | Notice. Using the "VST" name/logo is optional; if used, Steinberg's trademark usage guidelines apply. VST2 is **not** covered — IRONCLAD will not build VST2. |
| Apple system frameworks (Cocoa, Metal) | Apple SDK terms | Xcode licence | Normal macOS development terms. |
| PFFFT (planned, M3) | FFTPACK licence (BSD-like) + Pommier's changes under same terms | `pffft.c` header | Notice in docs; no use of NCAR/UCAR names for endorsement. Re-verify at pin time. |
| r8brain-free-src (planned, M3) | MIT; bundles Ooura FFT and PFFFT | repo README | Notices for r8brain and its bundled code. Re-verify at pin time. |
| FFTConvolver (reference/test) | MIT in current version (© 2017 HiFi-LoFi) | header of `FFTConvolver.h` at `f2cdeb0` | Notice if code is adopted. ⚠ Older copies (2012) circulating in other projects are **GPLv3** — use only the current upstream version. |

**Result:** every component planned for the shipped binary is permissive or file-level copyleft
(Eigen, unmodified). Nothing requires disclosing IRONCLAD's own source code. There is no
GPL/AGPL code in the product.

## 2. Evaluated and rejected

| Component | Licence | Why rejected |
|---|---|---|
| JUCE 9 | AGPLv3 or commercial JUCE 9 EULA | AGPLv3 would oblige us to publish IRONCLAD's source. The commercial route is free only under the Starter revenue limit (USD 20k/yr; Indie ≤ 300k, Pro unlimited, per the JUCE 8 EULA that JUCE 9 keeps unchanged), then becomes a recurring cost with terms set by a third party. iPlug2 gives equivalent capability for this product without that dependency. |
| NAM plugin's iPlug2 fork | zlib-style (same as iPlug2) | Not a licence problem — rejected for maintainability (see ARCHITECTURE §1.3). |

## 3. Development-only tools (not distributed)

| Tool | Licence | Note |
|---|---|---|
| doctest | MIT | Test binaries only. |
| Steinberg `validator` | MIT (part of VST3 SDK) | Run locally/CI. |
| pluginval | GPLv3 | Used as an external program; never linked or shipped. |
| Blender | GPL (application) | Output renders belong to the author; Blender's licence does not apply to images you create with it. |
| CMake, Ninja, Git | BSD-3 / Apache-2.0 / GPLv2 | Tools only. |

## 4. Assets and content

| Asset | Status | Rule |
|---|---|---|
| Amp/pedal/cab artwork | To be created in Blender (M6) | 100 % original. No Neural DSP, Mesa/Boogie, Marshall or other brand logos, panel layouts, knob designs or trade dress. Reference photos are used for realism study only, never traced or textured into renders. |
| HDRI / textures | TBD | Only CC0 (e.g. Poly Haven) or purchased with a licence allowing redistribution inside software. Record source URL + licence per file in `Assets/ASSET_SOURCES.md`. |
| Fonts | Roboto Regular 1.100141 (© Google 2012, **Apache-2.0**, licence stated in the font's name table) — *temporary M1 editor only* | Only SIL OFL-1.1 or Apache-2.0 fonts embedded; include licence files. The NAM plugin's fonts (Michroma, Roboto) are **not** reused as IRONCLAD's typographic identity. |
| Factory NAM captures | None yet | Captures are copyrighted works of their creators (often published with their own terms on sites such as TONE3000). Factory content must be **our own captures** or captures with a **written licence permitting redistribution in a commercial product**. User-loaded captures are the user's responsibility; IRONCLAD never uploads or redistributes them. |
| Factory IRs | None yet | Same rule as captures. Many commercial IR packs forbid redistribution. |
| Example models from NAM Core (`example_models/*.nam`) | MIT repo | Used only in tests; not shipped as factory content. |

## 5. Names and trademarks

- Product name "IRONCLAD": a trademark/clearance search in the target markets (EU/TR/US) is
  recommended before public release — the word is common and may already be registered in
  software or music-equipment classes.
- "NAM"/"Neural Amp Modeler": describe compatibility factually ("loads Neural Amp Modeler
  `.nam` captures"); do not imply endorsement by the NAM project.
- Never use "Neural DSP", "Archetype", "Mesa/Boogie", "Marshall" or other amp brand names in
  the UI, preset names, marketing or file names.

## 6. Distribution checklist (filled in at M8)

- [ ] `THIRD_PARTY_NOTICES.txt` generated from this table and installed with the product.
- [ ] Notices reachable from the plugin's About/Settings panel.
- [ ] Eigen source location stated (MPL-2.0).
- [ ] Every asset in `Assets/` listed in `ASSET_SOURCES.md` with licence.
- [ ] Legal review done.
