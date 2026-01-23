# Unified Glyph Rendering Redesign: FreeType + HarfBuzz + SDL3

## Executive Summary

This document tracks the redesign of the glyph rendering system to eliminate Cairo dependency and leverage FreeType, HarfBuzz, and SDL3 for high-performance, feature-rich text rendering with support for variable fonts, complex text shaping, and COLR v1 color emoji.

## System Versions

**Installed Library Versions:**

- FreeType: 2.13+ (with COLR v1 APIs)
- HarfBuzz: 11.5.1
- SDL3: 3.2.24

## Implementation Status

### ✅ Phase 1: Foundation (COMPLETED)

**Goal:** Replace Cairo with pure FreeType rasterization

**Completed:**

- ✅ Removed Cairo dependency from build system and code
- ✅ Implemented FreeType-only rasterization for grayscale/monochrome glyphs
- ✅ COLR v0 layer compositing via `FT_Get_Color_Glyph_Layer()` implemented
- ✅ COLR v1 paint graph traversal implemented:
  - `render_colr_paint_glyph()` - Entry point using `FT_Get_Color_Glyph_Paint()`
  - `paint_colr_paint_recursive()` - Recursive paint evaluator
  - Paint types implemented: PaintSolid, PaintLinearGradient, PaintRadialGradient, PaintSweepGradient, PaintTranslate, PaintScale, PaintRotate, PaintSkew, PaintTransform, PaintColrGlyph, PaintColrLayers, PaintComposite (SRC_OVER, PLUS, MULTIPLY), PaintGlyph (mask-based)
- ✅ Affine transform matrix helpers implemented
- ✅ BGRA→RGBA conversion for `FT_LOAD_COLOR` fast path

**Build system:**

- ✅ Cairo removed from `configure.ac` and `src/Makefile.am`
- ✅ Project builds successfully without Cairo

### ✅ Phase 2: HarfBuzz Integration (COMPLETED)

**Goal:** Add text shaping support

**Completed:**

- ✅ HarfBuzz dependency added to build system
- ✅ `hb_font_t` added to `FtFontData` structure
- ✅ `hb_ft_font_create_referenced()` used at font init time
- ✅ Variable font sync: `hb_ft_font_changed()` called after `FT_Set_Var_Design_Coordinates()`
- ✅ `ft_render_shaped()` implemented: shapes with HarfBuzz, rasterizes per-glyph with FreeType
- ✅ `ShapedGlyphs` structure exposed in public API
- ✅ Variable font axis APIs: `ft_set_variation_axis()` and `ft_set_variation_axes()` implemented
- ✅ MM_Var caching for performance

### ✅ Phase 3: Renderer Integration (COMPLETED)

**Goal:** Wire shaped text rendering into display pipeline

**Completed:**

- ✅ Renderer detects multi-codepoint sequences and calls `font_render_shaped_text()`
- ✅ Per-glyph texture upload with LRU cache implemented
- ✅ Shaped run positioning integrated (HarfBuzz x/y positions used)

### ⚠️ Phase 4: Bug Fixes & Coordinate Systems (CURRENT - IN PROGRESS)

**Goal:** Fix coordinate system mismatches preventing correct emoji rendering

**Status:** Emoji glyphs now visible but layers render at wrong offsets / upside down

**Initial bugs 1-4 FIXED:**

- ✅ Bug 1: ClipBox double-scaling fixed
- ✅ Bug 2: PaintGlyph case label corrected
- ✅ Bug 3: Root transform coordinate logic clarified
- ✅ Bug 4: PaintTransform case implemented

**NEW ISSUES DISCOVERED:**

- ❌ **Bug 5: Y-axis coordinate system inversion** (CRITICAL)
- ❌ **Bug 6: PaintGlyph offset confusion** (HIGH)
- ❌ **Bug 7: Gradient coordinates not Y-flipped** (HIGH)

---

## Critical Bugs Discovered (MUST FIX)

### ✅ Bug 1: FT_Get_Color_Glyph_ClipBox Double-Scaling (FIXED)

**Status:** RESOLVED

**Location:** `src/font_ft.c:1315-1318` (in `render_colr_paint_glyph`)

**Fix Applied:** Removed `* ft_data->scale` from ClipBox coordinate conversion (lines 1315-1318)

---

### ✅ Bug 2: Wrong Case Label for PaintGlyph (FIXED)

**Status:** RESOLVED

**Location:** `src/font_ft.c:1206`

**Fix Applied:** Case label changed from `FT_COLR_PAINTFORMAT_TRANSFORM` to `FT_COLR_PAINTFORMAT_GLYPH`

---

### ✅ Bug 3: Root Transform Coordinate Space (FIXED)

**Status:** RESOLVED

**Location:** `src/font_ft.c:1359`

**Fix Applied:** Set `matrix_maps_font_units = have_root_transform` (line 1359)

---

### ✅ Bug 4: Missing PaintTransform Case (FIXED)

**Status:** RESOLVED

**Location:** `src/font_ft.c:1256-1264`

**Fix Applied:** Implemented `FT_COLR_PAINTFORMAT_TRANSFORM` case handler

---

### ❌ Bug 5: Y-Axis Coordinate System Inversion ⚠️ CRITICAL

**Location:** Multiple locations in `src/font_ft.c`

**Problem:**

FreeType uses a **Y-up coordinate system** (positive Y goes upward from baseline), but pixel buffers use a **Y-down coordinate system** (positive Y goes downward from top-left). The current code does not flip Y coordinates, causing:

1. **Upside-down rendering** for some layers
2. **Wrong vertical offsets** for paint elements
3. **Inverted gradient directions**

**Coordinate System Mismatch:**

```
FreeType/COLR Space:          Pixel Buffer Space:
      +Y                             0,0 ───────► +X
       ↑                              │
       │                              │
       │                              ↓
baseline ───────► +X                 +Y
```

**Affected Code Sections:**

**5a) ClipBox Y-coordinate interpretation** (`src/font_ft.c:1311-1316`)

```c
// CURRENT (WRONG):
double bly = ft_pos_to_double(clip.bottom_left.y);  // e.g., -10 (below baseline)
double try_ = ft_pos_to_double(clip.top_right.y);   // e.g., 30 (above baseline)
xoff = (int)floor(blx);
yoff = (int)ceil(try_);  // ❌ Uses top_right.y directly as pixel Y-offset
```

**Issue:** `yoff` should represent the distance from top of buffer to baseline, but we're using FreeType's Y-up coordinate directly. For a glyph with `top_right.y = 30`, this means "30 pixels above baseline" in FreeType space, but we treat it as "Y=30 pixels from top" in buffer space.

**Fix for ClipBox:**

The ClipBox gives us the bounding box in FreeType's Y-up space. For correct Y-down pixel buffer:

```c
// Convert to Y-down pixel buffer space
// In Y-up: bly is bottom (negative), try_ is top (positive)
// In Y-down buffer: row 0 should correspond to FreeType's "top"
double blx = ft_pos_to_double(clip.bottom_left.x);
double bly = ft_pos_to_double(clip.bottom_left.y);
double trx = ft_pos_to_double(clip.top_right.x);
double try_ = ft_pos_to_double(clip.top_right.y);

xoff = (int)floor(blx);
yoff = (int)ceil(try_);  // This is correct: y_offset = distance from baseline to top

out_w = (int)ceil(trx - blx);
out_h = (int)ceil(try_ - bly);  // Height is still top - bottom
```

**Actually, the current ClipBox handling may be correct for FreeType's y_offset convention (distance from baseline to top). The issue is elsewhere.**

---

**5b) PaintGlyph coordinate mapping** (`src/font_ft.c:1220, 1227-1228`)

```c
// Render child paint into mask-sized buffer with offsets
paint_colr_paint_recursive(ft_data, child, matrix, matrix_maps_font_units,
                          tmp, mw, mh,
                          left + dst_x_off, (top - mh) + dst_y_off,  // ❌ Wrong Y offset
                          fg_r, fg_g, fg_b);

// Then composite mask pixels into destination:
for (int y = 0; y < mh; y++) {
    for (int x = 0; x < mw; x++) {
        int dst_x = left + x - dst_x_off;            // ❌ Offset confusion
        int dst_y = (top - mh) + y - dst_y_off;      // ❌ Wrong mapping
        ...
    }
}
```

**Issue:** The offset handling is confused. The mask buffer `tmp` has its own local coordinate space (0,0 to mw,mh). When rendering the child paint into `tmp`, we pass offsets, then when compositing we subtract them again. This double-offset causes misalignment.

**Root Cause Understanding:**

- `left, top` are FreeType glyph metrics (bitmap_left, bitmap_top from rasterized mask)
- `top` is Y-distance from baseline to top of mask (Y-up convention)
- `dst_x_off, dst_y_off` are passed from parent to indicate where in gradient/paint space this buffer lives
- When compositing, we need to map mask pixel (x,y) → destination buffer pixel

**Fix for PaintGlyph:**

The child paint should be rendered into the `tmp` buffer in its own local coordinate space, then composited into the destination buffer accounting for the mask's position:

```c
// Render child paint into tmp buffer (mask-sized) with gradient space offsets
// The paint coordinates are relative to the glyph's bounding box origin
paint_colr_paint_recursive(ft_data, child, matrix, matrix_maps_font_units,
                          tmp, mw, mh,
                          left, top - mh,  // ❌ Need to rethink this
                          fg_r, fg_g, fg_b);

// Composite: map mask buffer (x,y) to destination buffer
for (int y = 0; y < mh; y++) {
    for (int x = 0; x < mw; x++) {
        // In Y-down buffer space:
        // - Mask row 0 corresponds to FreeType's "top" (top of glyph bbox)
        // - Need to compute destination Y relative to buffer's coordinate frame
        int dst_x = left + x;  // Horizontal: just offset by left
        int dst_y = ???;       // Need to map FreeType Y to buffer Y
        ...
    }
}
```

**The Real Issue:** We're mixing coordinate systems. The destination buffer `buf` has its own origin determined by the ClipBox. We need to map the mask's FreeType coordinates (left, top) to buffer pixel coordinates.

**Correct Approach:**

The destination buffer `buf` is sized to fit the ClipBox. Its origin (0,0) corresponds to ClipBox's bottom-left in FreeType space, but in Y-down raster space, row 0 should be the top of the bounding box.

```c
// Given:
// - ClipBox: bottom_left = (blx, bly), top_right = (trx, try_)
// - Mask: left, top, width=mw, height=mh (FreeType metrics)
// - Destination buffer origin: (0,0) maps to (blx, try_) in FreeType space

// Composite mapping (Y-down buffer):
for (int y = 0; y < mh; y++) {
    for (int x = 0; x < mw; x++) {
        // Mask pixel (x,y) in local mask space
        // Mask bottom-left in FreeType: (left, top - mh)
        // Mask top-left in FreeType: (left, top)

        // Map to destination buffer (Y-down):
        // Buffer Y=0 corresponds to FreeType Y=try_ (top of ClipBox)
        // Mask row 0 corresponds to FreeType Y=top (top of mask)
        int dst_x = left - xoff + x;  // Relative to ClipBox left
        int dst_y = (yoff - top) + y;  // yoff=try_, so (try_ - top) + y

        if (dst_x < 0 || dst_x >= w || dst_y < 0 || dst_y >= h)
            continue;
        ...
    }
}
```

But wait, we're inside `paint_colr_paint_recursive` which doesn't have access to ClipBox origin. The `dst_x_off, dst_y_off` parameters are supposed to convey this information.

**Revisiting the Offset Semantics:**

Looking at the gradient code (line 787-788):

```c
double px = (double)(x + dst_x_off);
double py = (double)(y + dst_y_off);
```

The `dst_x_off, dst_y_off` are added to buffer pixel coordinates to get paint-space coordinates. This means:

- Buffer pixel (x,y) maps to paint-space (x + dst_x_off, y + dst_y_off)

For PaintGlyph:

- Child paint is rendered into `tmp` buffer (size mw×mh)
- `tmp` buffer pixel (0,0) should map to mask's top-left in paint-space
- Mask top-left in FreeType: (left, top)
- But paint-space for the parent buffer was: parent_buffer(x,y) → paint(x + dst_x_off, y + dst_y_off)

So for the child:

- `tmp` pixel (0,0) should map to paint-space (left, ??Y??)

**Y-Coordinate Confusion:** FreeType `top` is Y-up (distance from baseline to mask top). If the parent buffer is Y-down, we need to flip.

**Proposed Solution:**

The fundamental issue is that `dst_y_off` in the gradient code assumes Y-down pixel buffer but Y-up paint coordinates are being used without flipping.

**We need to consistently flip Y when converting from FreeType/gradient paint space to pixel buffer space.**

```c
// In paint_linear_gradient and similar:
for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
        // Buffer pixel (x, y) - Y is down
        // Paint space: X stays same, Y needs to be flipped
        // If buffer height is h, buffer Y=0 corresponds to paint Y=top
        // Buffer Y=h-1 corresponds to paint Y=bottom

        // For now, assume dst_y_off encodes the top of buffer in paint space
        double px = (double)(x + dst_x_off);
        double py = (double)((h - 1 - y) + dst_y_off);  // Flip Y
        ...
    }
}
```

But this is getting too invasive. Let me think of a cleaner approach...

---

**Cleaner Solution: Y-Flip at ClipBox Level**

The root issue is that FreeType's Y-up coordinate system is being used directly in Y-down pixel buffers. The cleanest fix:

1. **Accept that pixel buffers are Y-down** (row 0 = top of glyph)
2. **Reinterpret FreeType's `top` metric** as the row index for the top of the glyph
3. **Flip Y when applying gradient/paint coordinates**

**Specific Fixes:**

**Fix 5a: Gradient Y-coordinate flip**

In `paint_linear_gradient`, `paint_radial_gradient`, `paint_sweep_gradient`:

```c
// Current:
double py = (double)(y + dst_y_off);

// Should be (to account for Y-up paint space, Y-down buffer):
// If dst_y_off encodes the baseline's Y position in buffer space,
// and FreeType paint coords are relative to baseline (Y-up),
// then buffer Y=0 is above baseline, so paint_y = baseline - buffer_y
double py = (double)(dst_y_off - y);  // Flip Y direction
```

But we need to know what `dst_y_off` represents. Looking at the call from `render_colr_paint_glyph` (line 1366):

```c
paint_colr_paint_recursive(ft_data, root, &root_matrix, matrix_maps_font_units,
                          out->pixels, out_w, out_h,
                          0, 0,  // dst_x_off=0, dst_y_off=0
                          fg_r, fg_g, fg_b);
```

The initial call uses `dst_x_off=0, dst_y_off=0`. This means paint-space coordinates are used directly as buffer pixel coordinates. So the issue is:

**FreeType paint coordinates (Y-up) are being used as buffer pixel indices (Y-down) without flipping.**

**Correct Fix:**

The paint coordinates from FreeType are in Y-up space (relative to em-box origin or baseline). When mapping to pixel buffer (Y-down), we need to flip.

**For the root call:** If ClipBox gives us bounding box (blx, bly) to (trx, try\_) in FreeType Y-up space:

- Buffer pixel (0, 0) should map to FreeType point (blx, try\_) (top-left in Y-down = top of bbox in Y-up)
- Buffer pixel (w-1, h-1) should map to FreeType point (trx, bly) (bottom-right in Y-down = bottom of bbox in Y-up)

So:

- Buffer X = FT_X - blx (offset by left edge)
- Buffer Y = try\_ - FT_Y (flip: top of bbox minus FT Y-coord)

This means:

- `dst_x_off = blx` (origin X in paint space)
- `dst_y_off` should encode the Y-flip: `dst_y_off = try_` (top of bbox)

And in gradient code:

```c
double px = (double)(x + dst_x_off);          // Buffer x → paint x
double py = (double)(dst_y_off - y);          // Buffer y → paint y (flipped)
```

Or equivalently:

```c
double px = (double)(x) + dst_x_off;
double py = dst_y_off - (double)(y);  // Y-flip
```

**Summary of Fix 5:**

1. **At root call** (line 1366):

   ```c
   paint_colr_paint_recursive(ft_data, root, &root_matrix, matrix_maps_font_units,
                             out->pixels, out_w, out_h,
                             xoff, yoff,  // Pass ClipBox origin
                             fg_r, fg_g, fg_b);
   ```

2. **In all gradient functions** (lines 787-788, 898-899, 965-966):

   ```c
   // Change from:
   double py = (double)(y + dst_y_off);

   // To:
   double py = dst_y_off - (double)(y);  // Y-flip for Y-up to Y-down conversion
   ```

3. **In PaintGlyph** (line 1220):

   ```c
   // Render child paint: mask's top-left in FreeType is (left, top)
   // In destination buffer Y-down space, this maps to (left - dst_x_off, dst_y_off - top)
   paint_colr_paint_recursive(ft_data, child, matrix, matrix_maps_font_units,
                             tmp, mw, mh,
                             left, top,  // Pass mask's FreeType coordinates
                             fg_r, fg_g, fg_b);

   // When compositing (lines 1227-1228):
   for (int y = 0; y < mh; y++) {
       for (int x = 0; x < mw; x++) {
           // Map mask buffer (x,y) to destination buffer
           // Mask's top-left in FreeType: (left, top)
           // Destination buffer (x,y) maps to FreeType: (x + dst_x_off, dst_y_off - y)
           // So mask pixel (x,y) at FreeType (left + x, top - y) maps to dest buffer:
           //   dest_x: left + x = dst_x_off + X → X = left + x - dst_x_off
           //   dest_y: top - y = dst_y_off - Y → Y = dst_y_off - (top - y) = dst_y_off - top + y
           int dst_x = left + x - dst_x_off;
           int dst_y = dst_y_off - top + y;  // Corrected Y mapping
           ...
       }
   }
   ```

**This is the critical fix needed to make layers appear at correct offsets and right-side-up.**

---

### ❌ Bug 6: PaintGlyph Offset Double-Application (HIGH)

**Location:** `src/font_ft.c:1220, 1227-1228`

**Problem:** As analyzed in Bug 5, the offset handling in PaintGlyph has issues:

1. Child paint recursive call adds `dst_x_off, dst_y_off` to mask coordinates
2. Compositing loop subtracts `dst_x_off, dst_y_off` again

**This is addressed by the Bug 5 fix above.**

---

### ❌ Bug 7: Affine Transform Y-Flip (HIGH)

**Location:** Affine transform application throughout `paint_colr_paint_recursive`

**Problem:** Affine transforms from FreeType (PaintTranslate, PaintScale, PaintRotate, etc.) use Y-up coordinates, but we're applying them directly to Y-down pixel buffer coordinates.

**Fix:** Need to ensure transforms account for Y-flip. Specifically:

**For PaintTranslate** (lines 1115-1116):

```c
double dx = ft_fixed_to_double(pt->dx) * (matrix_maps_font_units ? 1.0 : ft_data->scale);
double dy = ft_fixed_to_double(pt->dy) * (matrix_maps_font_units ? 1.0 : ft_data->scale);
// dy is in Y-up coordinate, no change needed if we fix gradient Y-flip
```

**For PaintScale center** (lines 1128-1129):

```c
double cx = ft_fixed_to_double(ps->center_x) * scale_factor;
double cy = ft_fixed_to_double(ps->center_y) * scale_factor;
// cy is in Y-up coordinate, will be flipped when used in gradients
```

Actually, if we fix the gradient Y-flip (Bug 5), the transforms should work correctly since they're all operating in the same (Y-up) paint coordinate space. The Y-flip happens only at the final gradient→pixel mapping.

**Conclusion:** Bug 7 is implicitly fixed by Bug 5's coordinate system correction.

---

## Remaining Work

### Immediate (Blockers for Correct Emoji Rendering)

1. **Fix Bug 5: Y-axis coordinate flip** ⚠️ CRITICAL
   - Modify gradient functions to flip Y: `py = dst_y_off - y`
   - Fix PaintGlyph offset calculation: `dst_y = dst_y_off - top + y`
   - Pass ClipBox origin to root paint call: `paint_colr_paint_recursive(..., xoff, yoff, ...)`
   - Test with emoji to verify layers render at correct positions and right-side-up

2. **Test emoji rendering extensively**
   - Verify `./examples/unicode/emoji.sh | timeout 2 ./build/src/vterm-sdl3 -v -` renders emoji correctly
   - Check that all layers are positioned correctly
   - Verify no upside-down rendering
   - Test emoji with gradients (e.g., colorful emoji with linear/radial gradients)

### Secondary (Correctness & Coverage)

3. **Implement remaining composite modes**
   - Currently implemented: SRC_OVER, PLUS, MULTIPLY
   - Missing: SCREEN, OVERLAY, DARKEN, LIGHTEN, COLOR_DODGE, COLOR_BURN, HARD_LIGHT, SOFT_LIGHT, DIFFERENCE, EXCLUSION, HUE, SATURATION, COLOR, LUMINOSITY, SRC, DEST, SRC_IN, SRC_OUT, DEST_IN, DEST_OUT, SRC_ATOP, DEST_ATOP, XOR
   - Refer to FreeType ftcolor.h `FT_Composite_Mode` enum (line 446-479)

4. **Implement PaintExtend modes**
   - Currently: Only PAD is implemented (clamp to [0,1])
   - Missing: REPEAT, REFLECT
   - Location: `eval_colorline()` should return extend mode; gradient functions should apply it
   - Apply to t-value calculation in gradients before color interpolation

5. **Optimize gradient evaluation**
   - Current: Per-pixel CPU gradient evaluation
   - Consider: GPU shader-based gradients or FreeType internal rasterization

### Nice-to-Have (Future Enhancements)

6. **Font fallback chain**
   - Current: Single emoji font selected via fontconfig "emoji" pattern
   - Improvement: Fallback to multiple fonts for broader Unicode coverage

7. **BiDi/RTL support**
   - Use HarfBuzz direction/script detection
   - Implement proper RTL shaping

8. **Texture atlas**
   - Current: Per-glyph SDL_Texture with LRU cache
   - Optimization: Pack glyphs into atlas texture for batch rendering

9. **Extended variable font axes**
   - Current: Weight (wght) supported
   - Add: Width (wdth), Slant (slnt), Italic (ital), Optical Size (opsz), Grade (GRAD)

---

## Technical Details

### Current Font Pipeline

```
Codepoint(s) → HarfBuzz shaping → Glyph IDs + positions
                                      ↓
                              FreeType rasterization
                              (COLR v1/v0 or grayscale)
                                      ↓
                              RGBA bitmap (GlyphBitmap)
                                      ↓
                              SDL_Texture upload (LRU cache)
                                      ↓
                              SDL_RenderTexture
```

### COLR v1 Paint Evaluation Pipeline

```
FT_Get_Color_Glyph_Paint(glyph_id, FT_COLOR_INCLUDE_ROOT_TRANSFORM)
         ↓
FT_Get_Paint(opaque_paint) → FT_COLR_Paint
         ↓
paint_colr_paint_recursive() - recursively evaluate:
  - PaintSolid → fill with color
  - PaintLinearGradient → per-pixel gradient evaluation (Y-flipped)
  - PaintRadialGradient → radial distance-based interpolation (Y-flipped)
  - PaintSweepGradient → angle-based interpolation (Y-flipped)
  - PaintGlyph → rasterize mask, apply child paint via mask (Y-flipped)
  - PaintColrGlyph → inline nested glyph's paint graph
  - PaintColrLayers → composite layers with FT_Get_Paint_Layers
  - PaintComposite → blend src/backdrop with composite operator
  - PaintTranslate/Scale/Rotate/Skew → apply affine transform to matrix
  - PaintTransform → apply general affine transform
         ↓
RGBA buffer (out_w × out_h) → GlyphBitmap
```

### Coordinate Spaces (CRITICAL UNDERSTANDING)

**FreeType COLR v1 has two coordinate space modes:**

1. **With `FT_COLOR_INCLUDE_ROOT_TRANSFORM`:**
   - Root paint contains upem→pixel transform (from `FT_Set_Char_Size`)
   - All subsequent paint coordinates are in **font units** (unscaled)
   - Client must apply root transform OR scale coordinates

2. **With `FT_COLOR_NO_ROOT_TRANSFORM`:**
   - No root transform returned
   - Paint coordinates are in **font units**
   - Client must scale all coordinates by (font_size / units_per_EM)

**FT_Get_Color_Glyph_ClipBox:**

- Always returns coordinates in **pixel space** (26.6 fixed-point)
- Already accounts for `FT_Set_Char_Size` scaling
- Should NOT be multiplied by `ft_data->scale` again

**Y-Axis Coordinate Systems:**

```
FreeType/COLR (Y-up):                    Pixel Buffer (Y-down):

      +Y ↑                                    0,0 ───────► +X (row 0 = top)
         │                                     │
         │                                     │
         │                                     ↓
    0 ───┼───────► +X                        +Y (increasing rows downward)
         │
       baseline

```

**Mapping Rules:**

- **X-axis:** Same in both systems (left-to-right)
- **Y-axis:** Inverted
  - FreeType Y=+10 (10 pixels above baseline) → Buffer row depends on glyph top
  - FreeType Y=-5 (5 pixels below baseline) → Buffer row depends on glyph top

**For a glyph with ClipBox (blx, bly) to (trx, try\_):**

- FreeType bounding box: left=blx, right=trx, bottom=bly (below baseline), top=try\_ (above baseline)
- Buffer dimensions: width = trx - blx, height = try\_ - bly
- Buffer pixel (x, y) maps to FreeType point:
  - FT_X = x + blx
  - FT*Y = (try* - y) ← Y-flip: buffer row 0 is FreeType's top (try\_)

**Implementation in code:**

```c
// Root paint call passes origin:
paint_colr_paint_recursive(..., xoff, yoff, ...);  // xoff=blx, yoff=try_

// Gradients map buffer pixel to paint space:
double px = (double)(x) + dst_x_off;        // X: simple offset
double py = dst_y_off - (double)(y);        // Y: flip (top minus row)
```

---

## Known Issues Preventing Correct Emoji Rendering

### ✅ Issue 1: Emoji glyphs render as 1×1 tiny bitmaps (RESOLVED)

**Status:** Fixed by Bug 1 (ClipBox double-scaling) fix

---

### ⚠️ Issue 2: Emoji layers at wrong offsets / upside down (CURRENT)

**Symptom:** Emoji glyphs render but:

- Some layers are vertically flipped (upside down)
- Layers are offset incorrectly relative to each other
- Gradients may be inverted

**Root Cause:** Bug 5 (Y-axis coordinate system inversion)

**Effect:** Paint elements use Y-up FreeType coordinates without flipping to Y-down buffer space

**Fix:** Implement Bug 5 fixes (Y-coordinate flip in gradients and PaintGlyph)

---

### Issue 3: Some COLR v1 emoji fail to get paint graph

**Symptom:**

```
DEBUG: FT_Get_Color_Glyph_Paint with INCLUDE_ROOT_TRANSFORM failed for glyph 1, trying NO_ROOT_TRANSFORM
DEBUG: FT_Get_Color_Glyph_Paint failed for glyph 1 (both INCLUDE_ROOT_TRANSFORM and NO_ROOT_TRANSFORM)
```

**Possible Causes:**

- Glyph ID 1 may not have a COLR v1 paint graph (could be .notdef or a base glyph)
- Font may use COLR v0 for this glyph instead
- Need to check if FT_Get_Color_Glyph_Layer (v0 API) succeeds for these

**Status:** Low priority; most emoji render successfully

---

## TODO List (Priority Order)

### P0 - Critical (Emoji Rendering Correctness)

- [ ] **Fix Bug 5a:** Flip Y in gradient functions (lines 787-788, 898-899, 965-966)
  - Change `py = (double)(y + dst_y_off)` to `py = dst_y_off - (double)(y)`
- [ ] **Fix Bug 5b:** Pass ClipBox origin to root paint call (line 1366)
  - Change `paint_colr_paint_recursive(..., 0, 0, ...)` to `paint_colr_paint_recursive(..., xoff, yoff, ...)`
- [ ] **Fix Bug 5c:** Correct PaintGlyph compositing Y-offset (line 1228)
  - Change `dst_y = (top - mh) + y - dst_y_off` to `dst_y = dst_y_off - top + y`
- [ ] **Fix Bug 5d:** Correct PaintGlyph child paint call offsets (line 1220)
  - Change offsets to just pass mask's FreeType coordinates: `left, top`
- [ ] **Test:** Verify `./examples/unicode/emoji.sh | timeout 2 ./build/src/vterm-sdl3 -v -` renders emoji correctly (no upside-down, correct alignment)

### P1 - High (Coverage)

- [ ] **Handle COLR v0 fallback:** When v1 paint graph not found, ensure `FT_Get_Color_Glyph_Layer` path works
- [ ] **Emoji ZWJ sequences:** Ensure HarfBuzz shaping handles multi-codepoint emoji (family, flag sequences)
- [ ] **Add logging:** Log paint coordinate transformations for debugging Y-flip

### P2 - Medium (Features)

- [ ] **Implement PaintExtend:** REPEAT and REFLECT modes in gradients
- [ ] **Implement more composite modes:** Start with SCREEN, XOR, SRC_IN, DEST_IN
- [ ] **Optimize gradient evaluation:** Consider caching or GPU-based rendering

### P3 - Low (Optimization & Polish)

- [ ] **Glyph cache optimization:** Tune LRU cache size, consider different eviction policies
- [ ] **Texture atlas:** Batch multiple glyphs into single texture
- [ ] **GPU upload optimization:** Consider SDL3 GPU API for faster uploads
- [ ] **Variable font UI:** Expose axis control to user (future)

---

## Key Code Locations

**Font Backend:** `src/font_ft.c`

- Lines 19-59: `FtFontData` structure (FreeType + HarfBuzz state)
- Lines 113-146: `cache_mm_var()` - Variable font axis caching
- Lines 226-239: `check_colr_table()` - COLR table detection
- Lines 241-264: `load_colr_palette()` - COLR palette loading
- Lines 266-459: `render_colr_glyph()` - COLR v0 layer compositing
- Lines 570-650: `rasterize_glyph_mask()` - Glyph mask for PaintGlyph
- Lines 719-743: `eval_colorline()` - Color stop extraction
- Lines 745-834: `paint_linear_gradient()` - Linear gradient evaluation **← FIX Y-FLIP HERE**
- Lines 871-940: Radial gradient **← FIX Y-FLIP HERE**
- Lines 942-1019: Sweep gradient **← FIX Y-FLIP HERE**
- Lines 838-1276: `paint_colr_paint_recursive()` - **CORE PAINT EVALUATOR**
- Lines 1206-1254: PaintGlyph case **← FIX OFFSETS HERE**
- Lines 1279-1369: `render_colr_paint_glyph()` - COLR v1 entry point **← FIX ROOT CALL HERE**
- Lines 1774-1948: `rasterize_glyph_index()` - Glyph rasterization dispatcher
- Lines 1951-2016: `ft_render_shaped()` - HarfBuzz shaping + rasterization

**Renderer:** `src/renderer.c`

- Lines 116-129: `renderer_get_cached_texture()` - Glyph cache lookup
- Lines 131-171: `renderer_cache_texture()` - LRU cache insertion
- Lines 342-578: `renderer_draw_terminal()` - **MAIN RENDER LOOP**
- Lines 453-459: Font selection logic (emoji vs normal vs bold)
- Lines 464-504: Shaped run rendering path

**Font API:** `src/font.c`, `src/font.h`

- `ShapedGlyphs` structure exposed for HarfBuzz-shaped runs
- `font_render_shaped_text()` - Public API for shaped rendering
- `font_set_variation_axis()` / `font_set_variation_axes()` - Variable font control
- `font_style_has_colr()` - Check if loaded font has COLR table

---

## Testing Strategy

### Manual Tests (Current)

```bash
# Emoji rendering test (currently shows offset/flip issues)
./examples/unicode/emoji.sh | timeout 2 ./build/src/vterm-sdl3 -v -

# Verbose logging shows:
# - Font resolution (Noto Color Emoji found)
# - COLR table detection (has_colr=1)
# - Paint rendering attempts
# - Bounding box dimensions (should be reasonable, e.g., 24x40)
```

### After Bug 5 Fixes

Expected behavior:

- Emoji glyphs should render at correct size (~24-40 pixel dimensions)
- All layers should be right-side-up (no vertical flipping)
- Layers should align correctly (no offset errors)
- Gradients should render with correct orientation
- COLR v1 paint path should produce correctly positioned, colorful emoji bitmaps

### Automated Tests (Future)

- Golden image comparison for emoji rendering
- Unit tests for Y-coordinate flip (FreeType Y-up → buffer Y-down)
- Regression tests for COLR v1 paint types

---

## API Reference

### FreeType COLR v1 APIs

```c
FT_Get_Color_Glyph_Paint()       // Get root paint (with/without root transform)
FT_Get_Paint()                   // Evaluate FT_OpaquePaint → FT_COLR_Paint
FT_Get_Paint_Layers()            // Iterate PaintColrLayers
FT_Get_Colorline_Stops()         // Extract gradient color stops
FT_Get_Color_Glyph_ClipBox()     // Get bounding box in pixels (26.6 fixed, Y-up)
FT_Palette_Select()              // Load COLR palette
FT_Palette_Data_Get()            // Get palette metadata
```

**Important:** All FreeType COLR paint coordinates use **Y-up coordinate system** (positive Y is upward from baseline). Must flip Y when mapping to pixel buffers (Y-down).

### HarfBuzz APIs

```c
hb_ft_font_create_referenced()   // Create HB font from FT_Face
hb_ft_font_changed()             // Sync after FT_Set_Var_Design_Coordinates
hb_buffer_add_utf32()            // Add codepoints to buffer
hb_shape()                       // Shape text
hb_buffer_get_glyph_infos()      // Get shaped glyph IDs
hb_buffer_get_glyph_positions()  // Get positions (26.6 fixed-point)
```

---

## Architecture Decisions

### 1. COLR Rendering Strategy

**Decision:** Use `FT_COLOR_INCLUDE_ROOT_TRANSFORM`

- FreeType provides upem→pixel transform
- Paint coordinates are in font units (scaled by root transform)
- `matrix_maps_font_units = true` when root transform is present

**Y-Coordinate Handling:** All paint coordinates are Y-up; flip Y when mapping to pixel buffer

### 2. Texture Upload

**Decision:** SDL_Renderer + per-glyph SDL_Texture (Option A)

- Simple, compatible with all SDL3 backends
- Works well with LRU cache
- Terminal emulator use case doesn't need GPU API complexity

**Future:** SDL3 GPU API for batched uploads (documented in FUTURE_ENHANCEMENTS.md)

### 3. Shaping Strategy

**Decision:** Shape when `cp_count > 1` (multi-codepoint cells)

- Fallback to simple rendering for single ASCII characters
- Emoji/ZWJ sequences automatically use shaped path
- Flag sequences (🇺🇸 = U+1F1FA U+1F1F8) handled via shaping

### 4. Cache Strategy

**Decision:** LRU cache with 1024 entries

- Key: (font_data pointer, glyph_id, color)
- Eviction: Least-recently-used
- Size: Reasonable for terminal use (covers most visible glyphs)

---

## Current Status Summary

### Working

✅ Build system (Cairo removed, HarfBuzz added)
✅ FreeType rasterization for regular fonts
✅ HarfBuzz shaping integration
✅ Variable font axis control
✅ COLR v0 layer compositing
✅ COLR v1 paint traversal infrastructure
✅ Basic paint types (Solid, gradients, transforms)
✅ Affine transforms (Translate, Scale, Rotate, Skew, Transform)
✅ Renderer integration with shaped runs
✅ Glyph texture caching
✅ Emoji glyphs render at correct size (visible)

### Broken (Blocking Correct Emoji)

❌ **COLR v1 emoji layers render upside down** (Bug 5: Y-axis inversion)
❌ **COLR v1 emoji layers at wrong offsets** (Bug 5: coordinate mapping)

### Incomplete (Need Work)

⚠️ PaintExtend modes (REPEAT, REFLECT)
⚠️ Many composite operators
⚠️ Some gradient edge cases

---

## Next Steps (Immediate)

1. **Fix Bug 5a-d** (Y-axis coordinate flip) - **CRITICAL**
   - Flip Y in all gradient functions: `py = dst_y_off - y`
   - Pass ClipBox origin to root paint call: `xoff, yoff`
   - Fix PaintGlyph offset calculations
2. **Test emoji:** `./examples/unicode/emoji.sh | timeout 2 ./build/src/vterm-sdl3 -v -`
3. **Verify correct rendering:** All layers right-side-up, properly aligned, gradients correct orientation
4. **Commit fix with detailed explanation of Y-coordinate flip**

After these fixes, emoji rendering should be fully correct and we can proceed to secondary improvements (extend modes, composite operators, optimizations).

---

**Document Version:** 4.0  
**Last Updated:** 2026-01-23  
**Status:** Implementation In Progress - Y-Axis Coordinate Bug Identified (Critical)
