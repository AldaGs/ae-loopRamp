# Loop Ramp

A better Gradient Ramp for After Effects: any number of color stops edited right in the Effect Controls panel, linear or radial, Ramp Scatter to hide banding, and an **Offset** that slides the colors along the ramp and wraps around — animate it and the colors cycle forever.

## Install

1. Download `LoopRamp.aex` from [Releases](https://github.com/AldaGs/ae-loopRamp/releases) (Windows x64).
2. Close After Effects.
3. Copy it to `C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore\` (needs admin).
4. Apply **Effect → Learning → Loop Ramp** to a solid (or any layer — the ramp fills the layer's own alpha).

## Editing the gradient

| Action | Result |
|---|---|
| Click the bar | Add a stop (takes the color already there); keep dragging to place it |
| Drag a marker | Move the stop |
| Double-click a marker | Pick its color |
| Alt+click a marker, or drag it well below the bar | Delete it (minimum 2 stops) |

The gradient is one keyframeable property. Keyframes with the same number of stops tween every stop's position and color; keyframes with different counts hold.

## Controls

| Control | What it does |
|---|---|
| **Gradient** | The stops (up to 256). |
| **Start / End of Ramp** | Linear: the ramp runs from Start to End. Radial: Start is the center, the distance to End is one cycle. |
| **Ramp Shape** | Linear or Radial. |
| **Offset (%)** | Slides the colors along the ramp; 100 % = one full loop. Try the expression `time*100`. |
| **Repeat** | How many times the gradient repeats between Start and End. |
| **Ramp Scatter** | Static noise on each pixel's ramp position, like AE's Gradient Ramp, to break up banding. |
| **Interpolation** | Constant (hard bands), Linear, or Smooth (Catmull-Rom). |
| **Blend With Original** | Mix the ramp back over the layer. |

### Seamless loops

The ramp is periodic: after the last stop it keeps blending into the first, so Offset never shows a seam. If you *want* a hard edge (a sawtooth), put one stop at 0 % and another at 100 %.

## Details

- 8 / 16 / 32-bit float, SmartFX, Multi-Frame Rendering.
- Resolution-independent: the ramp and the scatter grain are computed in full-resolution layer pixels, so Half / Quarter / Custom previews match Full.

## Building

Built against the After Effects SDK (25.6). Place this folder under `Examples/Template/LoopRamp` in the SDK, open `Win/LoopRamp.sln` in Visual Studio, and build **Release | x64**. Set `AE_PLUGIN_BUILD_DIR` to choose where the `.aex` is written.

| File | Contents |
|---|---|
| `LoopRamp.cpp` | Params, the periodic color LUT, per-pixel render (classic + SmartFX). |
| `LoopRamp_UI.cpp` | The Gradient param: arbitrary-data callbacks (save / copy / keyframe) and the editor bar. |
| `LoopRamp.h` | Param indices, disk IDs, the stop-list struct. |
