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

### ✅ Phase 4: Bug Fixes & Coordinate Systems (COMPLETED)

**Goal:** Fix coordinate system mismatches preventing correct emoji rendering

**Status:** Y-axis coordinate system bug FIXED - emoji layers now render correctly positioned and right-side-up

**All critical bugs FIXED:**

- ✅ Bug 1: ClipBox double-scaling fixed
- ✅ Bug 2: PaintGlyph case label corrected
- ✅ Bug 3: Root transform coordinate logic clarified
- ✅ Bug 4: PaintTransform case implemented
- ✅ **Bug 5: Y-axis coordinate system inversion** (FIXED)
- ✅ **Bug 6: PaintGlyph offset confusion** (FIXED)
- ✅ **Bug 7: Gradient coordinates Y-flip** (FIXED)

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

### ✅ Bug 5: Y-Axis Coordinate System Inversion (FIXED)

**Location:** Multiple locations in `src/font_ft.c`

**Problem (now resolved):**

FreeType uses a **Y-up coordinate system** (positive Y goes upward from baseline), but pixel buffers use a **Y-down coordinate system** (positive Y goes downward from top-left). The code now correctly flips Y coordinates, ensuring:

1. **Right-side-up rendering** for all layers
2. **Correct vertical offsets** for paint elements
3. **Correct gradient directions**

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

**Fixes Applied:**

**5a) Gradient Y-flip (Lines 788, 899, 966)**

```c
// FIXED: Linear, Radial, Sweep gradients
for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
        double px = (double)(x + dst_x_off);
        double py = (double)(dst_y_off - y);  // Y-flip: convert from pixel Y-down to FreeType Y-up
        // Now gradients correctly map pixel rows to FreeType coordinates
    }
}
```

**5b) Root paint origin (Line 1366)**

```c
// FIXED: Pass ClipBox origin instead of (0, 0)
paint_colr_paint_recursive(ft_data, root, &root_matrix, matrix_maps_font_units,
                          out->pixels, out_w, out_h,
                          xoff, yoff,  // Use actual ClipBox coordinates
                          fg_r, fg_g, fg_b);
```

**5c) PaintGlyph child paint call (Line 1220)**

```c
// FIXED: Pass mask's FreeType coordinates directly
FT_OpaquePaint child = pg->paint;
paint_colr_paint_recursive(ft_data, child, matrix, matrix_maps_font_units,
                          tmp, mw, mh,
                          left, top,  // FreeType coordinates of mask
                          fg_r, fg_g, fg_b);
```

**5d) PaintGlyph compositing Y-offset (Line 1228)**

```c
// FIXED: Correct Y-flip conversion when compositing back to output buffer
int dst_x = left + x - dst_x_off;
int dst_y = dst_y_off - top + y;  // Y-flip: convert from FreeType Y-up to pixel Y-down
```

---

### ✅ Bug 6: PaintGlyph Offset Double-Application (FIXED)

**Location:** `src/font_ft.c:1220, 1227-1228`

**Problem (resolved):** The offset handling in PaintGlyph was confused, causing mask pixels to be composited at wrong buffer positions.

**Solution Applied:** Simplified offset passing and corrected Y-coordinate mapping as part of Bug 5 fixes:

- Child paint call: Pass mask's FreeType coordinates `(left, top)` directly
- Compositing: Map mask pixel `(x,y)` to buffer using `dst_y = dst_y_off - top + y`
- Offsets now consistently represent FreeType coordinate system origin

---

### ✅ Bug 7: Affine Transform Y-Flip (FIXED)

**Location:** Affine transform application throughout `paint_colr_paint_recursive`

**Problem (resolved):** Affine transforms from FreeType use Y-up coordinates. The coordinate system fix ensures transforms work correctly within FreeType's Y-up paint space, with Y-flip applied only at gradient evaluation.

**Status:** Fixed implicitly by Bug 5's coordinate system correction. All transforms operate in Y-up paint space; Y-flip happens at pixel mapping stage.

---

## Remaining Work

### Immediate (Next Phase - Post Y-Axis Fix)

1. **Verify COLR v0 fallback** ✅ In-Progress
   - Some emoji glyphs don't have v1 paint graphs
   - Fallback to COLR v0 layer rendering for these
   - Handle edge case where glyph IDs 1-5 fail to get paint data

2. **Extended emoji testing**
   - Test ZWJ sequences (family emoji, flags) with HarfBuzz shaping
   - Verify skin tone modifiers work correctly
   - Test complex color gradients in emoji

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

### ✅ Issue 2: Emoji layers at wrong offsets / upside down (RESOLVED)

**Symptom (fixed):** Previously, emoji glyphs rendered with:

- Some layers vertically flipped (upside down) ✗
- Layers offset incorrectly relative to each other ✗
- Gradients inverted ✗

**Root Cause:** Bug 5 (Y-axis coordinate system inversion)

**Resolution:** Implemented Bug 5 fixes:

- Y-coordinate flip in all gradient functions: `py = dst_y_off - y`
- Correct PaintGlyph compositing: `dst_y = dst_y_off - top + y`
- Pass ClipBox origin to root call: `xoff, yoff` instead of `0, 0`
- Pass mask coordinates correctly to child paint

**Current Status:** All emoji layers now render at correct positions and right-side-up ✓

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

### P0 - Critical (Emoji Rendering Correctness) ✅ COMPLETED

- [x] **Fix Bug 5a:** Flip Y in gradient functions (lines 787-788, 898-899, 965-966)
  - ✅ Changed `py = (double)(y + dst_y_off)` to `py = dst_y_off - (double)(y)`
- [x] **Fix Bug 5b:** Pass ClipBox origin to root paint call (line 1366)
  - ✅ Changed `paint_colr_paint_recursive(..., 0, 0, ...)` to `paint_colr_paint_recursive(..., xoff, yoff, ...)`
- [x] **Fix Bug 5c:** Correct PaintGlyph compositing Y-offset (line 1228)
  - ✅ Changed `dst_y = (top - mh) + y - dst_y_off` to `dst_y = dst_y_off - top + y`
- [x] **Fix Bug 5d:** Correct PaintGlyph child paint call offsets (line 1220)
  - ✅ Changed offsets to pass mask's FreeType coordinates: `left, top`
- [x] **Test:** Verified emoji rendering with `./examples/unicode/emoji.sh | timeout 2 ./build/src/vterm-sdl3 -v -` - Emoji render correctly positioned and right-side-up ✅

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
✅ **Emoji glyphs render at correct size with correct positioning** (all Y-axis bugs fixed)
✅ **COLR v1 emoji layers render right-side-up** (Bug 5: Y-axis inversion FIXED)
✅ **COLR v1 emoji layers at correct offsets** (Bug 5: coordinate mapping FIXED)

### Incomplete (Need Work)

⚠️ COLR v0 fallback for fonts without v1 paint graphs
⚠️ Handle glyph IDs that don't have COLR data (multi-layer composites)
⚠️ PaintExtend modes (REPEAT, REFLECT)
⚠️ Many composite operators
⚠️ Some gradient edge cases

---

## Completed Milestones

✅ **Phase 1:** Cairo removal - Pure FreeType rasterization
✅ **Phase 2:** HarfBuzz integration - Text shaping support  
✅ **Phase 3:** Renderer integration - Shaped runs rendering
✅ **Phase 4:** Critical coordinate system bug (Y-axis flip) - **FIXED**

**Result:** COLR v1 emoji rendering now works correctly with:

- Layers positioned accurately
- Right-side-up rendering (no vertical flips)
- Correct gradient orientation
- Proper affine transforms
- All layers composited in correct order

## Next Steps (Secondary Improvements)

1. **Extend coverage:** Implement COLR v0 fallback and missing composite modes
2. **Performance:** GPU-based gradient evaluation, texture atlasing
3. **Features:** Implement PaintExtend REPEAT/REFLECT modes
4. **Testing:** Comprehensive emoji test suite with golden images

---

**Document Version:** 5.0  
**Last Updated:** 2026-01-23  
**Status:** Phase 4 Complete - COLR v1 Emoji Rendering FIXED - Y-Axis Coordinate Bug Resolved
