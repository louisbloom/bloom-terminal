# Unified Glyph Rendering Redesign: FreeType + HarfBuzz + SDL3

## Executive Summary

This document tracks the redesign of the glyph rendering system to eliminate Cairo dependency and leverage FreeType, HarfBuzz, and SDL3 for high-performance, feature-rich text rendering with support for variable fonts, complex text shaping, and COLR v1 color emoji.

## System Versions

**Installed Library Versions:**

- FreeType: 2.13+ (with COLR v1 APIs)
- HarfBuzz: 11.5.1
- SDL3: 3.2.24

## Remaining Work

### Immediate (Next Phase - Post Y-Axis Fix)

1. **Verify COLR v0 fallback** In-Progress
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

### Issue: Some COLR v1 emoji fail to get paint graph

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
