# Unified Glyph Rendering Redesign: FreeType + HarfBuzz + SDL3

## Executive Summary

This document tracks the redesign of the glyph rendering system to eliminate Cairo dependency and leverage FreeType, HarfBuzz, and SDL3 for high-performance, feature-rich text rendering with support for variable fonts, complex text shaping, and COLR v1 color emoji.

## System Versions

**Installed Library Versions:**

- FreeType: 2.13+ (with COLR v1 APIs)
- HarfBuzz: 11.5.1
- SDL3: 3.2.24

## Remaining Work

### Immediate (Current Issues - Critical)

1. **Fix ZWJ and multi-codepoint emoji rendering** 🔴 BROKEN
   - **Issue**: Skin tone modifiers render as separate glyphs (👋 + 🏻 instead of 👋🏻)
   - **Issue**: Flag emoji render as separate regional indicators (🇺 + 🇸 instead of 🇺🇸)
   - **Issue**: Family emoji render as individual people instead of combined group
   - **Root cause**: libvterm stores multi-codepoint sequences in separate cells
   - **Solution needed**: Cell combining logic or lookahead to group related codepoints
   - **Affected**: Skin tones, flags, ZWJ sequences (family, professions, etc.)
   - **Status**: Shaping code works when cp_count > 1, but cells arrive individually

2. **Debug complex COLR v1 rendering issues** 🟡 PARTIAL
   - **Issue**: Some vehicle emoji (🚗🚕🚙) render as gray/white boxes
   - **Issue**: Some composite modes may not be rendering correctly
   - **Possible causes**: Missing composite operators, PaintExtend modes, or gradient issues
   - **Test case**: `./examples/unicode/emoji.sh | ./build/src/vterm-sdl3 -v -`
   - **Status**: Simple emoji work, complex gradients/composites may have issues

3. **Verify COLR v0 fallback** ✅ COMPLETE
   - Some emoji glyphs don't have v1 paint graphs → Falls back to COLR v0
   - COLR v0 layer rendering handles all fonts with v0 data → Returns NULL if no layers
   - Edge case where glyph IDs 1-5 fail to get paint data → Falls through to grayscale
   - Fallback chain verified: COLR v1 → COLR v0 → Grayscale rasterization
   - Added explicit logging for each fallback level

### Secondary (Correctness & Coverage)

4. **Implement remaining composite modes**
   - Currently implemented: SRC_OVER, PLUS, MULTIPLY, SCREEN, OVERLAY, DARKEN, LIGHTEN
   - Missing: COLOR_DODGE, COLOR_BURN, HARD_LIGHT, SOFT_LIGHT, DIFFERENCE, EXCLUSION, HUE, SATURATION, COLOR, LUMINOSITY, SRC, DEST, SRC_IN, SRC_OUT, DEST_IN, DEST_OUT, SRC_ATOP, DEST_ATOP, XOR
   - Refer to FreeType ftcolor.h `FT_Composite_Mode` enum (line 446-479)
   - **Priority**: May fix vehicle emoji rendering issues

5. **Implement PaintExtend modes**
   - Currently: Only PAD is implemented (clamp to [0,1])
   - Missing: REPEAT, REFLECT
   - Location: `eval_colorline()` should return extend mode; gradient functions should apply it
   - Apply to t-value calculation in gradients before color interpolation

6. **Optimize gradient evaluation**
   - Current: Per-pixel CPU gradient evaluation
   - Consider: GPU shader-based gradients or FreeType internal rasterization

### Nice-to-Have (Future Enhancements)

7. **Font fallback chain**
   - Current: Single emoji font selected via fontconfig "emoji" pattern
   - Improvement: Fallback to multiple fonts for broader Unicode coverage

8. **BiDi/RTL support**
   - Use HarfBuzz direction/script detection
   - Implement proper RTL shaping

9. **Texture atlas**
   - Current: Per-glyph SDL_Texture with LRU cache
   - Optimization: Pack glyphs into atlas texture for batch rendering

10. **Extended variable font axes**
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

### P0 - Critical (Emoji Broken)

- [ ] **Fix multi-codepoint cell combining** 🔴
  - Skin tone modifiers stored in separate cells by libvterm
  - Flag regional indicators stored in separate cells
  - ZWJ sequences (family, professions) stored in separate cells
  - **Solution**: Implement cell lookahead in renderer to combine before shaping
  - **Location**: `src/renderer.c` line ~441-466 (cp_count gathering)
  - **Test**: 👋🏻 (hand + light skin), 🇺🇸 (flag), 👨‍👩‍👧‍👦 (family)

- [ ] **Debug vehicle emoji rendering** 🟡
  - Cars (🚗🚕🚙) render as gray/white boxes
  - May be missing composite modes or PaintExtend issues
  - **Debug**: Add verbose logging to identify which paint operations fail
  - **Test**: ./examples/unicode/emoji.sh | ./build/src/vterm-sdl3 -v -

### P1 - High (Already Complete)

- [x] **Handle COLR v0 fallback:** ✅ COMPLETE - Three-level fallback working
- [x] **Add logging:** ✅ COMPLETE - Fallback transitions logged
- [x] **Y-axis coordinate fix:** ✅ COMPLETE - Layers render correctly

### P2 - Medium (Features)

- [ ] **Implement PaintExtend:** REPEAT and REFLECT modes in gradients
- [ ] **Implement more composite modes:** COLOR_DODGE, HARD_LIGHT, SOFT_LIGHT, etc.
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

### Test Results (2026-01-23)

**Test Command:**

```bash
./examples/unicode/emoji.sh | ./build/src/vterm-sdl3 -v -
```

**What Works:** ✅

- Simple emoji (single codepoint): 😀😃😄😁😆😅😂🤣😊😇 → Perfect
- Animals: 🐶🐱🐭🐹🐰🦊🐻🐼🐨🐯 → Render correctly
- Food: 🍎🍐🍊🍋🍌🍉🍇🍓🍈🍒 → Render correctly
- Most objects: ⚡💻📱 → Render correctly
- Y-axis coordinate mapping: All simple emoji right-side-up and positioned correctly
- COLR v1 paint traversal: Gradients and transforms work
- COLR v0 fallback: Verified working when needed

**What's Broken:** 🔴

1. **Skin tone modifiers** (CRITICAL):
   - Input: 👋🏻 (U+1F44B + U+1F3FB)
   - Expected: Single yellow hand with light skin tone
   - Actual: Yellow hand + separate brown square
   - Cause: libvterm stores modifier in separate cell
2. **Flag emoji** (CRITICAL):
   - Input: 🇺🇸 (U+1F1FA + U+1F1F8)
   - Expected: US flag emoji
   - Actual: "US" as text (regional indicators render separately)
   - Cause: Regional indicators stored in separate cells

3. **Family ZWJ sequences** (CRITICAL):
   - Input: 👨‍👩‍👧‍👦 (man + ZWJ + woman + ZWJ + girl + ZWJ + boy)
   - Expected: Single family emoji
   - Actual: Individual person emoji rendered separately
   - Cause: ZWJ sequence broken into separate cells

4. **Vehicle emoji** (HIGH):
   - Input: 🚗🚕🚙 (cars)
   - Expected: Colorful vehicle emoji
   - Actual: Gray/white boxes
   - Cause: Unknown - possibly missing composite modes or PaintExtend

**Root Cause Analysis:**

The HarfBuzz shaping code is correct and ready, but libvterm's cell model stores
multi-codepoint sequences in separate cells. The renderer receives:

- Cell 1: U+1F44B (👋)
- Cell 2: U+1F3FB (🏻 modifier)

Instead of the combined sequence for shaping. The shaping path (cp_count > 1) is
never triggered because each cell has cp_count=1.

**Recommended Fixes:**

**Option 1: Cell Lookahead in Renderer** (Preferred)

- Location: `src/renderer.c` lines 440-466
- Implementation:
  1. When rendering a cell with emoji codepoint, check next cell
  2. If next cell is skin tone modifier (U+1F3FB-U+1F3FF), combine
  3. If next cell is ZWJ (U+200D), lookahead and collect entire sequence
  4. If cell is regional indicator (U+1F1E6-U+1F1FF), combine with next RI
  5. Pass combined array to HarfBuzz shaping
- Pros: No libvterm changes needed, direct control
- Cons: Need to handle cell width properly (modifier takes 0-1 cells)

**Option 2: libvterm Configuration**

- Check if libvterm has option to keep modifier sequences in single cell
- May require libvterm version check or fork
- Pros: Cleaner separation of concerns
- Cons: May not be possible, requires external dependency change

**Option 3: Post-Processing Pass**

- Add pre-render pass to merge cells before main render loop
- Build combined codepoint arrays for each visual glyph
- Pros: Clean separation from render loop
- Cons: Additional complexity, performance overhead

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
✅ **Simple emoji render correctly** (single-codepoint emoji like 😀😃😄 work perfectly)
✅ **COLR v1 emoji layers render right-side-up** (Bug 5: Y-axis inversion FIXED)
✅ **COLR v1 emoji layers at correct offsets** (Bug 5: coordinate mapping FIXED)
✅ **COLR v0 fallback verified** (Three-level fallback: COLR v1 → COLR v0 → Grayscale)
✅ **Grayscale fallback for glyphs without COLR data** (Handles all edge cases including glyph IDs 1-5)
✅ **HarfBuzz shaping infrastructure** (Works when multi-codepoint cells are passed)

### Broken (Need Immediate Fix)

🔴 **Multi-codepoint emoji sequences** (skin tones, flags, ZWJ sequences)

- Skin tone modifiers: 👋🏻 renders as yellow hand + brown square
- Flags: 🇺🇸 renders as "US" instead of flag
- Family emoji: 👨‍👩‍👧‍👦 renders as individual people
- **Root cause**: libvterm stores each codepoint in separate cell
- **Fix needed**: Cell lookahead/combining logic before rendering

🟡 **Complex vehicle emoji** (🚗🚕🚙 render as gray boxes)

- Likely missing composite modes or PaintExtend issues
- Simple emoji work, complex paint graphs may have problems

### Incomplete (Lower Priority)

⚠️ PaintExtend modes (REPEAT, REFLECT) - currently only PAD (clamp) is implemented
⚠️ Additional composite operators (may fix vehicle rendering)
⚠️ Gradient edge cases and performance optimization

---

## Completed Milestones

✅ **Phase 1:** Cairo removal - Pure FreeType rasterization
✅ **Phase 2:** HarfBuzz integration - Text shaping support  
✅ **Phase 3:** Renderer integration - Shaped runs rendering
✅ **Phase 4:** Critical coordinate system bug (Y-axis flip) - **FIXED**
✅ **Phase 5:** COLR v0 fallback verification - **VERIFIED**

**Current State:**

**Working:**

- ✅ Simple emoji (single codepoint): 😀😃😄😁😆😅😂🤣 render perfectly
- ✅ COLR v1 paint graphs with gradients and transforms
- ✅ COLR v0 layer compositing with proper fallback
- ✅ Grayscale rendering for non-color glyphs
- ✅ Y-axis coordinate mapping (layers positioned correctly)
- ✅ HarfBuzz shaping infrastructure (ready for multi-codepoint)

**Broken:**

- 🔴 Skin tone modifiers: 👋🏻 → renders as 👋 + 🏻 (separate)
- 🔴 Flag emoji: 🇺🇸 → renders as U + S (regional indicators separate)
- 🔴 ZWJ sequences: 👨‍👩‍👧‍👦 → individual people instead of family
- 🟡 Complex vehicle emoji: 🚗🚕🚙 → gray/white boxes (composite mode issue?)

## Next Steps (Priority Order)

### Critical (Blocks emoji rendering)

1. **Fix multi-codepoint cell handling** 🔴
   - Problem: libvterm stores modifier sequences in separate cells
   - Solution options:
     a. Cell lookahead in renderer to combine related codepoints
     b. libvterm configuration to keep sequences together
     c. Post-processing to merge modifier cells
   - Affects: All skin tones, all flags, all ZWJ sequences

2. **Debug complex COLR v1 rendering** 🟡
   - Test vehicles with verbose logging to identify missing composite modes
   - Check if PaintExtend REPEAT/REFLECT are needed
   - Verify gradient evaluations are correct

### Secondary (Correctness)

3. **Implement missing composite modes** (COLOR_DODGE, HARD_LIGHT, etc.)
4. **Implement PaintExtend REPEAT/REFLECT**
5. **Performance optimization** (GPU gradients, texture atlas)

---

**Document Version:** 6.0  
**Last Updated:** 2026-01-23  
**Status:** Phase 5 Complete - Multi-Codepoint Emoji Broken - Needs Cell Combining Logic
