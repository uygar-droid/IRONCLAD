# Milestone 6.1 — art the owner provides (Amp page + shared frame)

Agreed 2026-11-13. All artwork is designed by the product owner (Photoshop); code draws nothing
that is meant to be "the look" except flat vector controls in the header/footer, which use the
owner's colours and font.

## General rules

- **Editor size:** 1100 × 820 at 1×. **Deliver only 2× (2200 × 1640 scale)**; the 1× files are
  produced automatically by high-quality downscaling.
- PNG, sRGB, 8-bit, transparent background; one file per item; Photoshop *Export Layers to Files*
  **without trimming**.
- Static art for the page may be delivered on **full-editor canvases (2200 × 1640)** so positions
  need no measuring. Moving parts (knobs, switches) get their own small canvases plus the centre
  coordinate (2× pixels) in a list.
- File names: lower case, `_` separated, `@2x` suffix, e.g. `amp_head@2x.png`.
- Page icons: SVG (see below).
- Fonts: TTF/OTF whose licence allows embedding in a commercial plugin (e.g. SIL OFL).

## Zones (1× / 2× pixels, top to bottom)

| Zone | 1× | 2× |
|---|---|---|
| Page icons | y 0–64 | 0–128 |
| Header strip | y 64–200 | 128–400 |
| Page content (amp) | y 200–785 | 400–1570 |
| Footer bar | y 785–820 | 1570–1640 |

## Shared frame

| # | Item | Format / size (2×) | Notes |
|---|---|---|---|
| F1 | Page background | 2200 × 1640 PNG | Behind everything; may differ per page (amp page: e.g. floor / wall / cabinet top under the head). |
| F2 | Five page icons | SVG, same viewBox (e.g. 32 × 32) | Pedals, amp head, cabinet, EQ, post FX. Monochrome light line art. |
| F3 | IRONCLAD logo | SVG or PNG (≈ 400 × 60) | Footer right (and/or on the amp panel). |
| F4 | Font(s) | TTF / OTF | One for UI text (labels, values, preset name); optionally a second for displays. |
| F5 | Colour palette | hex values | UI background, header background, text, dimmed text (dummies), accent (selected / arcs / meters), meter colours. |

Header and footer controls are drawn by code as flat vector controls in this palette: Input,
Gate (switch + knob), Output knobs with value readouts, input/output meters, the preset bar,
and the dummies Transpose, Input Mode (Mono/Stereo), Doubler (header) and Metronome (footer).
**Optional:** if the owner wants these as artwork too, a flat knob set (ring/arc background,
pointer) and a switch (off/on), 2×.

## Amp page

| # | Item | Format / size (2×) | Notes |
|---|---|---|---|
| A1 | Amp head (static) | full canvas 2200 × 1640 | Cabinet, tolex, handle, corners, feet, grille, tubes (unlit), control panel **with printed labels and 0–10 scales around each knob position**, input jack + cable. No knobs, no switches. |
| A2 | Tube glow | full canvas | Only the glow/light of the tubes, transparent elsewhere. Lets the code fade it (power on/off; later possibly with signal). |
| A3 | Knob (chicken-head), 5 layers | square, e.g. 160 × 160, all five the same size, rotation centre = canvas centre | See below. One set is reused for every amp knob. |
| A4 | Mini toggle or switch | e.g. 80 × 120, two files: `_off`, `_on` | For Normalize and Input Calibration. |
| A5 | Power switch (rocker) | e.g. 90 × 130, `_off`, `_on` (lit) | = Amp On. |
| A6 | Pilot lamp / LED (optional) | e.g. 40 × 40, `_off`, `_on` | |
| A7 | Capture display | e.g. 600 × 120 window + optional frame | Where the capture name, info (sample rate, loudness, input level) and status are shown by code; owner decides where it sits on the head. |
| A8 | LOAD / CLEAR buttons | e.g. 140 × 60, `_normal`, `_pressed` | Or the owner chooses text buttons drawn by code. |
| A9 | Position list | text | Centre (2× pixels) of every knob, switch, lamp, display and button; plus the knob rotation range (e.g. 0 at −135°, 10 at +135°). |

### A3 — knob layers (see ROADMAP M6 "Layered knob")

Draw the knob with the **pointer straight up (12 o'clock)**; the code rotates it.

1. `knob_base@2x.png` — drop shadow / base on the panel (static).
2. `knob_body@2x.png` — black body and pointer, no highlights (rotates).
3. `knob_brush@2x.png` — the **oval white brush spot** on the pointer, drawn where it sits when
   the pointer is up (rotates; opacity follows the angle, strongest around values 2–5).
4. `knob_light@2x.png` — the **screen-fixed light spot** where the light hits (does not rotate;
   shown only over the knob's own pixels; makes the 8–10 brightening). Optionally a second, weak
   one: `knob_light2@2x.png`.
5. `knob_cap@2x.png` — reflection on the round centre cap (static).

Amp controls that need a place on the panel: **Amp Input, Amp Output, Interface Level, Volume
Pedal** (knobs), **Normalize, Input Calibration** (switches), **Amp On** (power switch),
**capture display + Load / Clear**.

## Delivery order

1. **Prototype knob first:** A3 (five layers) + a small piece of panel behind it. The working
   knob is built and tested in REAPER before the rest is drawn.
2. Then F1–F5 and A1–A9.
