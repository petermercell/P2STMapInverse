# P2STMapInverse

A Nuke plugin that generates a **stabilize STMap** from a position pass (P) and a camera — the warp-inverse of [P2STMap](https://github.com/petermercell) (matchmove).

Inspired by [Ivan Busquets's C44Matrix](https://www.nukepedia.com/tools/plugins/colour/c44matrix/).

## What It Does

`P2STMap` projects a P pass through a camera to produce a **matchmove** STMap (sticks a clean still onto a moving shot). `P2STMapInverse` produces the **opposite warp**: an STMap that, applied to the moving plate, warps the filmed surface so it sits **still** in a chosen reference frame.

It is *not* made by inverting the matchmove STMap pixel-by-pixel (that would be a scatter problem with no unique solution). It uses the **exact same projection chain** as `P2STMap`:

1. **Inverse Transform** — World space → Camera space
2. **Projection** (with W-divide) — Camera space → NDC
3. **Format** (with W-divide) — NDC → Pixel coordinates
4. **Normalize** — Pixel coordinates → ST coordinates (0-1)

The stabilize behaviour comes from *how the chain is driven*, not from different math:

| | P pass | Camera | Apply STMap to | Result |
|---|---|---|---|---|
| **P2STMap** (matchmove) | animated | held at reference | clean / reference still | still follows the move |
| **P2STMapInverse** (stabilize) | held at reference | animated (live) | moving plate | surface sits still |

To make this self-contained, `P2STMapInverse` **holds the P input at the reference frame internally**, so no external `FrameHold` is needed. The camera stays live.

## Inputs

| Input | Description |
|-------|-------------|
| **P** | Position pass (world coordinates in RGB). Held internally at the reference frame. |
| **cam** | Camera node — **live / animated** (do **not** hold this one). |

## Output

| Channel | Value |
|---------|-------|
| R | U coordinate (normalized X) |
| G | V coordinate (normalized Y) |
| B | Z depth |
| A | W component |

## Knobs

| Knob | Description |
|------|-------------|
| **reference frame** | Frame at which the P pass is held. The output STMap warps the surface back to its position in this frame. |
| **hold P at reference frame** | **On**: P is held internally at the reference frame (recommended). **Off**: P is read at the current frame — wire your own `FrameHold` on the P pass instead. |

## Usage

1. Connect your position pass to the **P** input.
2. Connect your **live** camera to the **cam** input (animated — no FrameHold).
3. Set **reference frame** to the frame you want the plate to stabilize to.
4. Leave **hold P at reference frame** on.
5. Feed the output into an **STMap** node driving your **moving plate**.

**Typical workflow:**
```
[Position Pass] → [P2STMapInverse] → [STMap] → [stabilized plate]
                        ↑                ↑
                  [live Camera]    [moving plate]
```

> **Get the split right.** Stabilize needs the P pass *held* and the camera *live*. This is the opposite of the matchmove setup. If you accidentally hold the camera and leave P live, the projection collapses to near-identity and nothing stabilizes. With **hold P at reference frame** on, the node manages the hold for you — just keep the camera animated.

## Why Use This?

Stabilizing on a 3D surface usually means baking a reproject through 3D geometry or wrangling P passes and FrameHolds by hand. `P2STMapInverse` collapses it to a single node fed by a P pass and a live camera, producing a ready-to-use STMap. Because it shares the projection chain with `P2STMap`, a forward → inverse pairing is mathematically consistent.

> **Surface accuracy.** The result is only as clean as the P pass at silhouettes — expect the usual reprojection tearing at depth discontinuities, the same as any P-based reposition.

## Building from Source

Build with CMake the same way as `P2STMap`, pointing the source at `src/P2STMapInverse.cpp`. See `building_step_by_step.txt` for detailed instructions.

### Requirements
- CMake
- Nuke NDK (included with Nuke installation)
- C++ compiler (MSVC on Windows, GCC on Linux, Clang on macOS)

The node appears under **Transform/P2STMapInverse**.

## Related

- **P2STMap** — the forward (matchmove) node this inverts.
- [C44Matrix](https://github.com/petermercell/C44Matrix) — General 4x4 matrix transformation plugin (with added Axis support).

## Author

[Peter Mercell](https://github.com/petermercell)
