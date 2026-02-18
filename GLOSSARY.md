# Glossary

Domain terminology used in the COLR v1 paint rendering code (`src/colr.c`), targeted at programmers who are new to font rendering and image compositing.

---

## Affine (transformation)

A geometric operation used in font rendering to scale, rotate, shear, or translate text while preserving straight lines and parallelism. Affine transformations are applied to the vector outlines of glyphs, enabling high-quality, resolution-independent rendering. When applied at the vector level, they maintain typographic integrity; when applied at the bitmap level, they degrade quality due to pixel interpolation. Represented as a 2x3 matrix with six components: `xx`, `xy`, `dx` (first row) and `yx`, `yy`, `dy` (second row). An identity matrix (`xx=1`, `yy=1`, everything else 0) means "no transformation."

## Alpha (channel)

The transparency component of a pixel color. Ranges from 0 (fully transparent) to 255 (fully opaque). In RGBA pixel data, alpha is the fourth byte. When compositing two images, the alpha values determine how much each image contributes to the final result. For example, a red pixel with alpha 128 is 50% transparent — whatever is behind it will partially show through.

## Backdrop

In compositing, the image that already exists underneath. When two images are combined, the backdrop is the bottom layer and the source is the top layer. The compositing mode determines how their colors and alpha values interact to produce the output. Also called the "destination" in some Porter-Duff terminology.

## Bitmap

A rectangular grid of pixels. Each pixel stores color values (and optionally transparency). In this codebase, bitmaps are stored as flat arrays of bytes in RGBA order — four bytes per pixel, laid out row by row from top to bottom. A 10x10 bitmap is 400 bytes (`10 * 10 * 4`).

## Bounding box (bbox)

The smallest axis-aligned rectangle that fully encloses a glyph or image. Used to allocate pixel buffers of the right size before rendering. For example, the letter "g" has a bounding box that extends below the baseline (for the descender) and above (for the body). If the bounding box is wrong, parts of the glyph get clipped off or memory is wasted on empty space.

## Clip box

A rectangle provided by the font that defines the rendering boundary for a COLR v1 color glyph. Paint operations outside this box are discarded. The clip box is stored in the font's COLR table in 26.6 fixed-point coordinates and converted to pixel dimensions before rendering. Not all glyphs have clip boxes — without one, the code falls back to rasterizing a monochrome version to determine the size.

## COLR table

An OpenType font table that stores instructions for rendering multi-color glyphs. This is how color emoji and other colored characters are defined inside a font file. The COLR table comes in two versions: v0 (simple stacked layers) and v1 (a full paint graph with gradients, transforms, and compositing). The COLR table references colors from the CPAL (Color Palette) table.

## COLR v0

The original, simpler version of the COLR table. A color glyph is defined as a stack of flat-colored layers — each layer is a regular glyph outline filled with a single color from the palette. Think of it like stacking colored paper cutouts. FreeType handles v0 rendering automatically via `FT_LOAD_COLOR`.

## COLR v1

The newer version of the COLR table, adding support for gradients, affine transforms, and Porter-Duff compositing. A color glyph is defined as a tree of "paint" operations (the paint graph) that are evaluated recursively. FreeType exposes the paint data but does not render it — this codebase implements the rendering manually by walking the paint tree.

## Color index

A reference to a specific color in the font's color palette (CPAL table). Rather than storing RGB values directly, paint nodes in the COLR table store an index into the palette. A special index value (`0xFFFF`) means "use the foreground color" — the current text color chosen by the application. This allows color glyphs to adapt to different text color settings.

## Color stop

A position-and-color pair that defines a point along a gradient. Each stop has an offset (a number between 0.0 and 1.0 indicating position along the gradient) and an RGBA color. The renderer interpolates between adjacent stops to produce smooth color transitions. For example, a stop at offset 0.0 with red and a stop at offset 1.0 with blue creates a red-to-blue gradient.

## Colorline

An ordered sequence of color stops that defines the full color transition of a gradient. A linear gradient with three stops (red at 0.0, white at 0.5, blue at 1.0) would produce red fading to white fading to blue. The colorline also specifies an extend mode that controls behavior outside the 0.0–1.0 range.

## Compositing

The process of combining two images (source and backdrop) into one, using rules that consider both images' color and alpha values. Different compositing modes produce different visual effects: `SRC_OVER` places the source on top (the most common mode), `MULTIPLY` darkens, `SCREEN` lightens, and so on. Each mode is a mathematical formula applied per-pixel. See also: [Porter-Duff compositing](#porter-duff-compositing).

## Device space

The coordinate system of the output display, measured in pixels. The origin (0,0) is typically the top-left corner, with X increasing rightward and Y increasing downward. This is the opposite of font coordinate conventions (where Y increases upward), requiring a Y-flip during rendering.

## Extend mode

Controls what happens when a gradient is sampled outside its defined range (before the first stop or after the last stop). **PAD** clamps to the nearest stop color (the most common mode — used throughout this codebase). **REPEAT** tiles the gradient. **REFLECT** mirrors the gradient back and forth.

## Fixed-point (number)

A way to represent fractional numbers using integers, by reserving some bits for the fractional part. FreeType uses two fixed-point formats:

- **16.16** — 16 integer bits + 16 fractional bits; divide by 65536 to get a `double`. Used for transform matrices and gradient parameters.
- **26.6** — 26 integer bits + 6 fractional bits; divide by 64 to get a `double`. Used for glyph coordinates and metrics.

## Font units

The coordinate system internal to a font file. Font outlines are designed on a grid (typically 1000 or 2048 units per em-square) that is independent of display size. To render at a specific pixel size, a scale factor converts font units to device pixels. When COLR v1 uses the "root transform," paint coordinates arrive in font units and must be scaled; without it, they arrive pre-scaled to pixels.

## Foreground color

The text color set by the application (e.g., white text on a black background). COLR paint nodes can reference the foreground color via a special color index (`0xFFFF`), allowing parts of a color glyph to match the surrounding text color. Passed as `fg_r`, `fg_g`, `fg_b` throughout the paint evaluation code.

## Glyph

The visual representation of a character in a font. Each glyph has an outline (vector path), metrics (advance width, bearings), and a unique numeric ID within the font. The letter "A" is a glyph; the fire emoji is also a glyph (possibly a color glyph with COLR data). A single Unicode character can map to different glyphs depending on the font.

## Glyph mask

A single-channel (grayscale) bitmap produced by rasterizing a glyph's outline. Pixel values range from 0 (outside the outline) to 255 (inside the outline), with intermediate values at the edges for anti-aliasing. In COLR v1 rendering, the glyph mask acts as a cookie cutter — the paint (gradient, solid color, etc.) is only visible where the mask is non-zero.

## Identity matrix

An affine transform that does nothing — it maps every point to itself. Has `xx=1`, `yy=1` and all other components zero. Used as the starting point for the accumulated transform when beginning paint tree evaluation.

## Layer

One paint operation in a stack that gets composited together to produce a color glyph. A smiling face emoji might have layers for: yellow circle (background), two black ovals (eyes), black curve (mouth). Each layer is rendered into its own temporary buffer, then composited onto the accumulated result using `SRC_OVER` (newer on top of older).

## Linear gradient

A color transition along a straight line between two points (`p0` and `p1`). The color at any pixel is determined by projecting that pixel onto the `p0`-to-`p1` line and looking up the corresponding position in the colorline. Pixels before `p0` or after `p1` are handled by the extend mode (typically PAD — clamp to the nearest stop color).

## Linear interpolation (lerp)

Blending between two values using a ratio `t`, where `t=0.0` gives the first value and `t=1.0` gives the second. The formula is: `result = (1-t)*a + t*b`. Used to smoothly transition between adjacent color stops in a gradient. For example, `t=0.5` between red and blue gives purple.

## Opaque paint

FreeType's handle type for referencing a node in the COLR v1 paint tree. It is an opaque pointer (you cannot inspect its contents directly) that you pass to `FT_Get_Paint()` to retrieve the actual paint data (type, parameters, child references). This handle-based API lets FreeType manage the internal paint table format while exposing a stable interface.

## Outline

The vector path (made of lines and bezier curves) that defines a glyph's shape. Unlike a bitmap, an outline is resolution-independent — it can be rasterized at any size. FreeType loads outlines from the font file and rasterizes them into bitmaps (or masks) at the requested size.

## Paint graph (paint tree)

The tree-shaped data structure that describes how to render a COLR v1 color glyph. Each node is a paint operation: leaf nodes produce color (solid fill, gradient), interior nodes apply transforms (translate, scale, rotate, skew) or compose children together (composite, layers). The tree is evaluated recursively from root to leaves. For example:

```
root -> composite(SRC_OVER) -> [
    backdrop: solid(yellow),
    source: glyph_mask(outline) -> gradient(red-to-blue)
]
```

## Paint format

The type tag on a paint node in the COLR v1 paint tree. Determines what the node does:

| Format                                                 | Purpose                                        |
| ------------------------------------------------------ | ---------------------------------------------- |
| `SOLID`                                                | Fills with a flat color                        |
| `LINEAR_GRADIENT`, `RADIAL_GRADIENT`, `SWEEP_GRADIENT` | Fills with a gradient                          |
| `GLYPH`                                                | Clips paint through a glyph outline            |
| `COMPOSITE`                                            | Combines two sub-trees with a compositing mode |
| `COLR_LAYERS`                                          | Stacks multiple layers with `SRC_OVER`         |
| `TRANSLATE`, `SCALE`, `ROTATE`, `SKEW`, `TRANSFORM`    | Applies geometric operations                   |
| `COLR_GLYPH`                                           | References another color glyph                 |

## Porter-Duff compositing

A set of 12+ standard rules (published by Thomas Porter and Tom Duff in 1984) for combining two images based on their alpha channels. Each rule defines a "source factor" (`Fs`) and "destination factor" (`Fd`) that weight the source and backdrop contributions. `SRC_OVER` (`result = src + backdrop * (1 - srcAlpha)`) is the most intuitive and common mode — it places the source on top of the backdrop. Other modes like `SRC_IN`, `DEST_OUT`, and `XOR` create masking and cutout effects.

## Projection

Mapping a point onto a line to get a scalar position. In gradient rendering, each pixel is projected onto the gradient's direction vector using the dot product: `t = dot(pixel - p0, direction) / |direction|^2`. This gives a value `t` (typically 0.0 to 1.0) that is used to look up the color from the colorline.

## Radial gradient

A color transition that radiates outward from a center point. Defined by a center (`c0`), an inner radius (`r0`), and an outer radius (`r1`). The color at any pixel is determined by its distance from the center — pixels at distance `r0` get the first stop color, pixels at distance `r1` get the last stop color, and pixels between are interpolated. Used in emoji for circular highlights and shading effects.

## Rasterize

Converting a vector outline (resolution-independent curves) into a pixel grid (bitmap) at a specific size. The rasterizer decides which pixels are inside the outline and how much each edge pixel is covered (for anti-aliasing). FreeType's rasterizer produces either a grayscale bitmap (for text) or an RGBA bitmap (for color glyphs).

## Root transform

The initial affine transform that converts coordinates from font units to device pixels. When `FT_COLOR_INCLUDE_ROOT_TRANSFORM` is used, FreeType prepends this transform to the paint tree, and all paint coordinates arrive in font units. Without it, coordinates arrive pre-scaled to pixels. The root transform encodes the font size — changing it is how you render the same glyph at different sizes.

## RGBA

A four-channel color representation: Red, Green, Blue, Alpha. Each channel is typically one byte (0–255). In this codebase, RGBA pixel buffers are stored as flat byte arrays in R, G, B, A order — so pixel N starts at byte offset `N*4`. A fully opaque red pixel is `[255, 0, 0, 255]`.

## Scale factor

The multiplier that converts from font units to pixel coordinates. If a font has 2048 units per em and is rendered at 16 pixels, the scale factor is `16/2048 = 0.0078125`. Every coordinate in font units gets multiplied by this to get pixel positions.

## Singular matrix

A matrix whose determinant is zero, meaning it cannot be inverted. In geometric terms, a singular affine transform collapses 2D space into a line or point (e.g., scaling to zero in one axis). The code checks for this before attempting to compute an inverse transform, falling back to no transformation if inversion fails.

## Skew

A shearing transform that slants shapes along one or both axes. A horizontal skew makes vertical lines lean sideways (like italicizing text). Defined by skew angles for the X and Y axes and a center point. Applied via `tan(angle)` in the affine matrix's off-diagonal components.

## Source

In compositing, the new image being placed on top of the backdrop. The compositing mode determines how the source's colors and alpha interact with the backdrop underneath. In Porter-Duff terminology, the source factor (`Fs`) controls how much the source contributes to the final result.

## Sweep gradient

A color transition that rotates around a center point, like a clock hand sweeping around the dial. Defined by a center point, a start angle, and an end angle. The color at any pixel is determined by the angle from the center to that pixel, normalized into the start-to-end range. Used in emoji for conical shading effects. Also called a "conical" or "angular" gradient.

## Y-flip

Converting between two opposite vertical conventions. FreeType and most font formats use a Y-up coordinate system (positive Y goes upward, like math). Screens and pixel buffers use Y-down (positive Y goes downward, row 0 is the top). The conversion is: `pixel_y = origin_y - freetype_y`. This flip appears throughout the rendering code wherever FreeType coordinates meet pixel coordinates.
