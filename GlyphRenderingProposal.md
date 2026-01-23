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
  - Paint types implemented: PaintSolid, PaintLinearGradient, PaintRadialGradient, PaintSweepGradient, PaintTranslate, PaintScale, PaintRotate, PaintSkew, PaintColrGlyph, PaintColrLayers, PaintComposite (SRC_OVER, PLUS, MULTIPLY), PaintGlyph (mask-based)
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

### ❌ Phase 4: Bug Fixes & Correctness (CURRENT - BLOCKED)

**Goal:** Fix critical bugs preventing emoji rendering

**Status:** Emoji font loads with COLR table detected, but glyphs render as empty/1x1 bitmaps

---

## Critical Bugs Discovered (MUST FIX)

### Bug 1: FT_Get_Color_Glyph_ClipBox Double-Scaling ⚠️ CRITICAL

**Location:** `src/font_ft.c:1298-1306` (in `render_colr_paint_glyph`)

**Problem:**

```c
// CURRENT (WRONG):
double blx = ft_pos_to_double(clip.bottom_left.x);  // Converts 26.6 → pixels
double bly = ft_pos_to_double(clip.bottom_left.y);
double trx = ft_pos_to_double(clip.top_right.x);
double try_ = ft_pos_to_double(clip.top_right.y);
xoff = (int)floor(blx * ft_data->scale);        // ❌ DOUBLE SCALING
yoff = (int)ceil(try_ * ft_data->scale);        // ❌ DOUBLE SCALING
out_w = (int)ceil((trx - blx) * ft_data->scale); // ❌ DOUBLE SCALING
out_h = (int)ceil((try_ - bly) * ft_data->scale); // ❌ DOUBLE SCALING
```

**FreeType Documentation:**

> The clip box is computed taking scale and transformations configured on the @FT_Face into account. @FT_ClipBox contains @FT_Vector values in 26.6 format.

**Root Cause:**

- `FT_Get_Color_Glyph_ClipBox` returns coordinates **already in pixel space** (26.6 fixed-point)
- `ft_pos_to_double()` correctly converts 26.6 → pixels (divide by 64)
- Code then **multiplies by `ft_data->scale` again** (font_size / units_per_EM)
- Result: Bounding box shrinks by ~1/64 to 1/1024, collapsing to 1x1

**Fix Required:**

```c
// CORRECT:
xoff = (int)floor(blx);              // Remove * ft_data->scale
yoff = (int)ceil(try_);              // Remove * ft_data->scale
out_w = (int)ceil(trx - blx);        // Remove * ft_data->scale
out_h = (int)ceil(try_ - bly);       // Remove * ft_data->scale
```

**Impact:** This bug causes ALL COLR v1 emoji to render as 1x1 or empty bitmaps.

---

### Bug 2: Wrong Case Label for PaintGlyph ⚠️ CRITICAL

**Location:** `src/font_ft.c:1205`

**Problem:**

```c
case FT_COLR_PAINTFORMAT_TRANSFORM:  // ❌ WRONG! Should be PAINTFORMAT_GLYPH
{
    FT_PaintGlyph *pg = &paint.u.glyph;  // Code handles PaintGlyph, not PaintTransform
    ...
}
```

**FreeType ftcolor.h enum values:**

- `FT_COLR_PAINTFORMAT_GLYPH = 10`
- `FT_COLR_PAINTFORMAT_TRANSFORM = 12`

**Root Cause:**

- Case label is `PAINTFORMAT_TRANSFORM` (enum value 12)
- Code body uses `paint.u.glyph` which is for `PAINTFORMAT_GLYPH` (enum value 10)
- Result: PaintGlyph (format 10) falls through to default case and returns transparent

**Fix Required:**

```c
case FT_COLR_PAINTFORMAT_GLYPH:  // Correct enum value
{
    FT_PaintGlyph *pg = &paint.u.glyph;
    ...
}
```

**Additionally:** Need to implement actual `FT_COLR_PAINTFORMAT_TRANSFORM` case which uses `paint.u.transform.affine` and `paint.u.transform.paint`.

**Impact:** PaintGlyph nodes (critical for many emoji) are not being handled, causing rendering failures.

---

### Bug 3: Root Transform Coordinate Space Confusion ⚠️ MODERATE

**Location:** `src/font_ft.c:1277, 1348-1350` (in `render_colr_paint_glyph`)

**Problem:**

```c
int got = FT_Get_Color_Glyph_Paint(ft_data->ft_face, glyph_index,
                                   FT_COLOR_INCLUDE_ROOT_TRANSFORM, &root);
...
if (root_paint.format == FT_COLR_PAINTFORMAT_TRANSFORM) {
    affine_from_FT_Affine23(&root_matrix, &root_paint.u.transform.affine);
    matrix_maps_font_units = true;  // ❌ Incorrect assumption
}
```

**FreeType Documentation:**

> When this function returns an initially computed root transform, at the time of executing the @FT_PaintGlyph operation, the contours should be retrieved using @FT_Load_Glyph at unscaled, untransformed size. This is because the root transform applied to the graphics context will take care of correct scaling.
>
> Subsequent @FT_COLR_Paint structures contain unscaled and untransformed values.

**Root Cause:**

- When `FT_COLOR_INCLUDE_ROOT_TRANSFORM` is used, FreeType returns paint coordinates in **font units** (unscaled)
- The root transform contains the **upem→pixel scaling** (from `FT_Set_Char_Size`)
- Code sets `matrix_maps_font_units = true` only if root paint is a TRANSFORM node
- If root paint is NOT a transform (e.g., direct PaintColrLayers), `matrix_maps_font_units` stays false and gradient coordinates get **double-scaled**

**Fix Required:**

1. Always set `matrix_maps_font_units = true` when using `FT_COLOR_INCLUDE_ROOT_TRANSFORM`
2. OR: Use `FT_COLOR_NO_ROOT_TRANSFORM` and manually scale all paint coordinates

---

### Bug 4: Missing PaintTransform Case

**Location:** `src/font_ft.c` - missing case in switch statement

**Problem:**

- `FT_COLR_PAINTFORMAT_TRANSFORM` (enum value 12) has no case handler
- This is different from PaintTranslate/PaintScale/PaintRotate/PaintSkew which are specific transform types
- PaintTransform is a general affine transform wrapper

**Fix Required:**
Implement:

```c
case FT_COLR_PAINTFORMAT_TRANSFORM:
{
    FT_PaintTransform *pt = &paint.u.transform;
    Affine local;
    affine_from_FT_Affine23(&local, &pt->affine);
    Affine next;
    affine_mul(&next, matrix, &local);
    return paint_colr_paint_recursive(ft_data, pt->paint, &next,
                                      matrix_maps_font_units, buf, w, h,
                                      dst_x_off, dst_y_off, fg_r, fg_g, fg_b);
}
```

---

## Remaining Work

### Immediate (Blockers for Emoji Rendering)

1. **Fix ClipBox double-scaling** (Bug 1)
   - Remove `* ft_data->scale` from lines 1303-1306
   - Test with emoji to verify correct bounding box dimensions

2. **Fix PaintGlyph case label** (Bug 2)
   - Change line 1205 from `PAINTFORMAT_TRANSFORM` to `PAINTFORMAT_GLYPH`
   - Add actual `PAINTFORMAT_TRANSFORM` case

3. **Fix root transform coordinate logic** (Bug 3)
   - Set `matrix_maps_font_units = true` when using `FT_COLOR_INCLUDE_ROOT_TRANSFORM` regardless of root paint type
   - OR switch to `FT_COLOR_NO_ROOT_TRANSFORM` and apply scaling in paint evaluation

4. **Test emoji rendering**
   - Verify `./examples/unicode/emoji.sh | timeout 2 ./build/src/vterm-sdl3 -v -` renders emoji correctly
   - Check that COLR v1 paint path produces non-empty bitmaps

### Secondary (Correctness & Coverage)

5. **Implement remaining composite modes**
   - Currently implemented: SRC_OVER, PLUS, MULTIPLY
   - Missing: SCREEN, OVERLAY, DARKEN, LIGHTEN, COLOR_DODGE, COLOR_BURN, HARD_LIGHT, SOFT_LIGHT, DIFFERENCE, EXCLUSION, HUE, SATURATION, COLOR, LUMINOSITY, SRC, DEST, SRC_IN, SRC_OUT, DEST_IN, DEST_OUT, SRC_ATOP, DEST_ATOP, XOR
   - Refer to FreeType ftcolor.h `FT_Composite_Mode` enum (line 446-479)

6. **Implement PaintExtend modes**
   - Currently: Only PAD is implemented (clamp to [0,1])
   - Missing: REPEAT, REFLECT
   - Location: `eval_colorline()` should return extend mode; gradient functions should apply it
   - Apply to t-value calculation in gradients before color interpolation

7. **PaintGlyph offset handling**
   - Review lines 1220, 1227-1228 for potential double-offset bug
   - Child paint recursive call passes `left + dst_x_off` but compositing subtracts `dst_x_off` again

8. **Optimize gradient evaluation**
   - Current: Per-pixel CPU gradient evaluation
   - Consider: GPU shader-based gradients or FreeType internal rasterization

### Nice-to-Have (Future Enhancements)

9. **Font fallback chain**
   - Current: Single emoji font selected via fontconfig "emoji" pattern
   - Improvement: Fallback to multiple fonts for broader Unicode coverage

10. **BiDi/RTL support**
    - Use HarfBuzz direction/script detection
    - Implement proper RTL shaping

11. **Texture atlas**
    - Current: Per-glyph SDL_Texture with LRU cache
    - Optimization: Pack glyphs into atlas texture for batch rendering

12. **Extended variable font axes**
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
  - PaintLinearGradient → per-pixel gradient evaluation
  - PaintRadialGradient → radial distance-based interpolation
  - PaintSweepGradient → angle-based interpolation
  - PaintGlyph → rasterize mask, apply child paint via mask
  - PaintColrGlyph → inline nested glyph's paint graph
  - PaintColrLayers → composite layers with FT_Get_Paint_Layers
  - PaintComposite → blend src/backdrop with composite operator
  - PaintTranslate/Scale/Rotate/Skew → apply affine transform to matrix
         ↓
RGBA buffer (out_w × out_h) → GlyphBitmap
```

### Coordinate Spaces (IMPORTANT)

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

---

## Known Issues Preventing Emoji Rendering

### Issue 1: Emoji glyphs render as 1×1 tiny bitmaps

**Symptom:**

```
DEBUG:   Selected font: emoji (style=2) has_colr=1
DEBUG: Rendering glyph U+1F600 with direct FreeType (style=2)
DEBUG: rasterize_glyph_index: COLR v1 paint rendered for glyph 1780: 1x1
DEBUG: create_texture_from_glyph_bitmap: empty glyph bitmap (0x0)
```

**Root Cause:** Bug 1 (ClipBox double-scaling)

**Effect:** Emoji bounding boxes collapse to 1×1 or 0×0, no visible rendering

---

### Issue 2: Some COLR v1 emoji fail to get paint graph

**Symptom:**

```
DEBUG: FT_Get_Color_Glyph_Paint with INCLUDE_ROOT_TRANSFORM failed for glyph 1, trying NO_ROOT_TRANSFORM
DEBUG: FT_Get_Color_Glyph_Paint failed for glyph 1 (both INCLUDE_ROOT_TRANSFORM and NO_ROOT_TRANSFORM)
```

**Possible Causes:**

- Glyph ID 1 may not have a COLR v1 paint graph (could be .notdef or a base glyph)
- Font may use COLR v0 for this glyph instead
- Need to check if FT_Get_Color_Glyph_Layer (v0 API) succeeds for these

---

### Issue 3: PaintGlyph nodes not handled

**Root Cause:** Bug 2 (wrong case label)

**Effect:** Any emoji using PaintGlyph in its paint graph will fail to render

---

### Issue 4: Coordinate space mismatch for gradients

**Root Cause:** Bug 3 (matrix_maps_font_units logic)

**Effect:** Gradient coordinates may be scaled incorrectly, causing gradients to render at wrong positions/sizes

---

## TODO List (Priority Order)

### P0 - Critical (Emoji Rendering Blockers)

- [ ] **Fix Bug 1:** Remove `* ft_data->scale` from ClipBox conversion (lines 1303-1306)
- [ ] **Fix Bug 2:** Change case label from `PAINTFORMAT_TRANSFORM` to `PAINTFORMAT_GLYPH` (line 1205)
- [ ] **Implement Bug 4:** Add actual `FT_COLR_PAINTFORMAT_TRANSFORM` case handler
- [ ] **Test:** Verify `./examples/unicode/emoji.sh | timeout 2 ./build/src/vterm-sdl3 -v -` renders emoji

### P1 - High (Correctness)

- [ ] **Fix Bug 3:** Clarify root transform coordinate handling
  - Option A: Always set `matrix_maps_font_units = true` with `FT_COLOR_INCLUDE_ROOT_TRANSFORM`
  - Option B: Switch to `FT_COLOR_NO_ROOT_TRANSFORM` and manually scale paint coords
- [ ] **Review PaintGlyph offsets:** Check lines 1220, 1227-1228 for double-offset bug
- [ ] **Add logging:** Log bbox dimensions, paint formats encountered, coordinate values for debugging

### P2 - Medium (Coverage)

- [ ] **Implement PaintExtend:** REPEAT and REFLECT modes in gradients
- [ ] **Implement more composite modes:** Start with SCREEN, XOR, SRC_IN, DEST_IN
- [ ] **Handle COLR v0 fallback:** When v1 paint graph not found, try `FT_Get_Color_Glyph_Layer`
- [ ] **Emoji ZWJ sequences:** Ensure HarfBuzz shaping handles multi-codepoint emoji (family, flag sequences)

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
- Lines 745-834: `paint_linear_gradient()` - Linear gradient evaluation
- Lines 838-1266: `paint_colr_paint_recursive()` - **CORE PAINT EVALUATOR**
- Lines 1269-1347: `render_colr_paint_glyph()` - COLR v1 entry point
- Lines 1815-1985: `rasterize_glyph_index()` - Glyph rasterization dispatcher
- Lines 1987-2053: `ft_render_shaped()` - HarfBuzz shaping + rasterization

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
# Emoji rendering test (currently produces 1x1 bitmaps)
./examples/unicode/emoji.sh | timeout 2 ./build/src/vterm-sdl3 -v -

# Verbose logging shows:
# - Font resolution (Noto Color Emoji found)
# - COLR table detection (has_colr=1)
# - Paint rendering attempts
# - Bounding box dimensions (currently 1x1 - BUG)
```

### After Bug Fixes

Expected behavior:

- Emoji glyphs should render at ~24-40 pixel dimensions (based on font_size=24)
- COLR v1 paint path should produce colorful emoji bitmaps
- "rasterize_glyph_index: COLR v1 paint rendered for glyph X: WxH" should show W,H > 1

### Automated Tests (Future)

- Golden image comparison for emoji rendering
- Unit tests for coordinate conversion (26.6 → pixels)
- Regression tests for COLR v1 paint types

---

## API Reference

### FreeType COLR v1 APIs

```c
FT_Get_Color_Glyph_Paint()       // Get root paint (with/without root transform)
FT_Get_Paint()                   // Evaluate FT_OpaquePaint → FT_COLR_Paint
FT_Get_Paint_Layers()            // Iterate PaintColrLayers
FT_Get_Colorline_Stops()         // Extract gradient color stops
FT_Get_Color_Glyph_ClipBox()     // Get bounding box (ALREADY IN PIXELS)
FT_Palette_Select()              // Load COLR palette
FT_Palette_Data_Get()            // Get palette metadata
```

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
- Paint coordinates are in font units
- Requires careful handling of `matrix_maps_font_units` flag

**Alternative:** Use `FT_COLOR_NO_ROOT_TRANSFORM` and manually scale all paint coords

- Simpler mental model
- More explicit control
- May reconsider after root transform bugs are fixed

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
✅ Basic paint types (Solid, gradients)
✅ Affine transforms (Translate, Scale, Rotate, Skew)
✅ Renderer integration with shaped runs
✅ Glyph texture caching

### Broken (Blocking Emoji)

❌ **COLR v1 emoji render as 1×1 bitmaps** (Bug 1: ClipBox double-scaling)
❌ **PaintGlyph not handled** (Bug 2: Wrong case label)
❌ **PaintTransform missing** (Bug 4)

### Incomplete (Need Work)

⚠️ Root transform coordinate handling (Bug 3)
⚠️ PaintExtend modes (REPEAT, REFLECT)
⚠️ Many composite operators
⚠️ Some paint types may have edge cases

---

## Next Steps (Immediate)

1. **Fix Bug 1** (ClipBox scaling) - `src/font_ft.c:1303-1306`
2. **Fix Bug 2** (PaintGlyph case) - `src/font_ft.c:1205`
3. **Implement Bug 4** (PaintTransform case) - add to switch statement
4. **Test emoji:** `./examples/unicode/emoji.sh | timeout 2 ./build/src/vterm-sdl3 -v -`
5. **Verify non-empty bitmaps** in logs: "COLR v1 paint rendered for glyph X: WxH" where W,H >> 1

After these fixes, emoji rendering should work and we can proceed to secondary improvements.

---

**Document Version:** 3.0  
**Last Updated:** 2026-01-22  
**Status:** Implementation In Progress - Critical Bugs Identified
