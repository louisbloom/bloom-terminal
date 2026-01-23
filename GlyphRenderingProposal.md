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

1. **Fix ZWJ and multi-codepoint emoji rendering** 🔴 BROKEN - **ROOT CAUSE IDENTIFIED**
   - **Issue**: Skin tone modifiers render as separate glyphs (👋 + 🏻 instead of 👋🏻)
   - **Issue**: Flag emoji render as separate regional indicators (🇺 + 🇸 instead of 🇺🇸)
   - **Issue**: Family emoji render as individual people instead of combined group
   - **Root cause**: The renderer currently only collects codepoints from a single cell. The emoji combining infrastructure (helpers + a combine function) was removed during a git restore and does NOT exist in the current tree; it must be implemented and integrated into the render loop.
   - **Current location where changes are needed**: `src/renderer.c` - see "What Needs to Be Added" in LINE_NUMBER_CORRECTIONS.md for exact insertion points
   - **Solution designed**: See "Multi-Codepoint Emoji Fix Design" section below
   - **Affected**: Skin tones, flags, ZWJ sequences (family, professions, etc.)
   - **Status**: Fix designed and ready to implement (requires adding helpers and combine function)

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

## Architectural Decision: Where to Fix Emoji Sequences?

### Question: Why Fix at Renderer Level Instead of libvterm Level?

**TL;DR:** libvterm is designed to be minimal and Unicode-semantics-agnostic. Emoji combining is a **rendering concern**, not a terminal emulation concern. The fix belongs in the renderer.

### How libvterm Handles Unicode (Research Findings)

**libvterm's Cell Model:**

- **VTERM_MAX_CHARS_PER_CELL = 6**: Fixed array in each cell (ABI-locked)
- **Combining character detection**: Uses Unicode categories (Mn, Me, Cf)
  - Base character + up to 5 combining marks CAN fit in one cell
  - Combining marks include: diacritics, ZWJ (U+200D), variation selectors
- **Width calculation**: Based on Unicode East Asian Width property
  - 0-width: combining characters, ZWJ, variation selectors
  - 1-width: most characters (Latin, emoji base)
  - 2-width: East Asian Wide, CJK ideographs

**How Codepoints Are Assigned to Cells (`src/state.c` in libvterm):**

```c
// Simplified libvterm logic:
for (each codepoint) {
  if (vterm_unicode_is_combining(cp)) {
    // Add to current cell (up to 6 total)
    append_to_current_cell(cp);
  } else {
    // Start new cell
    advance_to_next_cell();
    start_new_cell(cp);
  }
}
```

**What This Means for Emoji:**

1. **Skin tone modifiers (U+1F3FB-U+1F3FF):**
   - These are NOT in libvterm's combining character table
   - libvterm treats them as **base characters** (width 2)
   - Storage: 👋 (cell 0) + 🏻 (cell 1) - **separate cells**

2. **ZWJ (U+200D - Zero Width Joiner):**
   - IS in combining table (U+200B-U+200F range)
   - Width: 0
   - Storage when following emoji: 👨 (cell 0, includes ZWJ as combining) + 👩 (cell 1) + ...
   - **Still splits** because each emoji base starts a new cell

3. **Regional Indicators (U+1F1E6-U+1F1FF):**
   - NOT in combining table
   - Each RI is a base character (width 2)
   - Storage: 🇺 (cell 0) + 🇸 (cell 1) - **separate cells**

**Key Insight:** libvterm has **no semantic understanding of emoji**. It only knows:

- Is this a combining mark? (Unicode category)
- How wide is this character? (East Asian Width)

Emoji modifiers and ZWJ emoji are **application-level semantics**, not terminal-level semantics.

### Why NOT Fix at libvterm Level?

**Reason 1: Not libvterm's Job**

Terminal emulators faithfully represent character streams as cells. They don't interpret high-level Unicode semantics like:

- Emoji ZWJ sequences
- Grapheme clusters
- Ligatures
- Complex text shaping

This is intentional - libvterm is a **terminal emulator library**, not a **text rendering library**.

**Reason 2: ABI Constraints**

```c
#define VTERM_MAX_CHARS_PER_CELL 6  // Cannot change without breaking ABI
```

Even if we wanted to store long emoji sequences (👨‍👩‍👧‍👦 = 7 codepoints), we'd hit the 6-codepoint limit. We'd need to:

- Increase the constant (breaks ABI with all existing libvterm users)
- Or implement dynamic storage (major libvterm change, breaks API)

**Reason 3: libvterm is External Dependency**

We don't control libvterm. Even if we forked it:

- Maintenance burden (sync with upstream)
- Incompatibility with system libvterm
- Other terminal emulators don't have this issue (they fix at rendering layer)

**Reason 4: Separation of Concerns**

```
Terminal Layer (libvterm):          Rendering Layer (our code):
├─ Parse escape sequences           ├─ Shape text (HarfBuzz)
├─ Maintain cell grid                ├─ Combine emoji sequences
├─ Handle cursor movement            ├─ Render glyphs (FreeType)
└─ Basic Unicode width               └─ Position and composite
   (East Asian Width only)              (with font metrics)
```

**Emoji sequence understanding belongs in the renderer**, alongside:

- HarfBuzz shaping (ligatures, complex scripts)
- Font substitution
- COLR paint evaluation

### Why Fix at Renderer Level? ✅

**Reason 1: We Already Have the Infrastructure**

- The rendering pipeline and HarfBuzz shaping path already exist
- HarfBuzz shaping will handle combined codepoint arrays once provided
- We need to add emoji detection helpers and a cell combining function, then call it from the render loop

**Reason 2: Rendering is the Right Layer**

Renderers MUST lookahead for many reasons:

- **Font shaping**: "fi" ligature requires looking at adjacent characters
- **BiDi**: Hebrew/Arabic text requires line-level reordering
- **Grapheme clusters**: "é" (e + combining acute) must be treated as unit
- **Emoji sequences**: Same category as above - rendering semantics

**Reason 3: Precedent from Other Terminal Emulators**

- **Alacritty**: Handles emoji combining at renderer level
- **Kitty**: Handles emoji combining at renderer level
- **WezTerm**: Handles emoji combining at renderer level
- **Standard practice**: Terminal layer stores cells, renderer combines for display

**Reason 4: Minimal Change at Correct Layer**

Integration requires adding helpers and a combining function, and wiring them into the existing render loop and shaping path.

### Conclusion: Renderer-Level Fix is Correct

The split happens at libvterm level (by design), but the fix belongs at renderer level (by architecture).

This is not a workaround - it's the proper separation of concerns:

- **libvterm**: Faithful character cell representation
- **Renderer**: Semantic understanding and presentation

Our fix follows industry best practices and requires adding implementation and wiring in `src/renderer.c`.

---

## Multi-Codepoint Emoji Fix Design

### Research Findings (2026-01-23)

**Discovery:** During recent research the renderer file was restored to a clean state and the emoji combining code that previously existed was removed. The current codebase does NOT contain emoji detection helpers or a `combine_cells_for_emoji()` implementation; these must be added.

**Evidence:**

- `src/renderer.c` is currently ~599 lines long and lacks emoji-specific logic
- Lines 439-444 currently collect codepoints from a single cell only (bug location)
- Line 465 contains the shaped rendering check (`if (cp_count > 1 && rend->font && rend->font->render_shaped)`), which never triggers for emoji sequences because `cp_count` is always 1 for emoji stored across multiple cells

**What the new code must do:**

- Add emoji detection helpers
- Add a `combine_cells_for_emoji()` function that looks ahead across cells on the same row and produces a combined UTF-32 codepoint array
- Integrate skip-tracking into the render loop so combined cells are not re-rendered
- Use existing shaped rendering path by passing combined arrays to HarfBuzz

### Solution Design

**Approach:** Implement emoji helpers and `combine_cells_for_emoji()` and integrate them into the main rendering loop with skip tracking.

**Required Changes:**

**File: `src/renderer.c`**

**Change 1: Add emoji detection helpers (after header/initial helpers)**

```c
// Emoji helper functions (to be added near top of renderer.c)
static bool is_emoji_presentation(uint32_t cp);
static bool is_regional_indicator(uint32_t cp);
static bool is_zwj(uint32_t cp);
static bool is_skin_tone_modifier(uint32_t cp);
```

**Change 2: Add cell combining function (after helpers, before renderer internals)**

```c
static int combine_cells_for_emoji(Terminal *term, int row, int col,
                                   uint32_t *combined_codepoints,
                                   int max_combined);
// Full implementation ~100 lines
```

**Change 3: Add skip tracking in row loop (line ~375)**

```c
for (int row = 0; row < display_rows; row++) {
    int last_combined_col = -1;  // Track cells consumed by emoji combining
    for (int col = 0; col < display_cols; col++) {
        if (col <= last_combined_col) continue; // skip cells already consumed
        VTermScreenCell cell;
        ...
```

**Change 4: Replace single-cell codepoint collection (lines 439-444)**

- Detect if first codepoint looks emoji-like
- If so, call `combine_cells_for_emoji()` and obtain a combined array and count
- Update `last_combined_col` based on actual cells consumed (or conservative heuristic `col + cp_count - 1`)
- Otherwise use existing single-cell collection

**Change 5: Keep shaped rendering path (lines 465-506)**

- No change needed; once `cp_count > 1` is produced, HarfBuzz path will work

### Implementation Notes

**Cell Width Considerations:**

- vterm stores emoji pieces across cells with varying widths
- `combine_cells_for_emoji()` should limit lookahead to same row and available display columns
- The function should return actual codepoint count; integration will compute cells consumed by checking neighboring cell contents or using a conservative heuristic

**Skip Logic:**

Use `last_combined_col` to avoid re-rendering cells that were consumed by the combining function.

### Testing Strategy

**Test Cases:**

1. **Skin tone modifiers:**

   ```bash
   printf "\xf0\x9f\x91\x8b\xf0\x9f\x8f\xbb" | ./build/src/vterm-sdl3 -v -
   # U+1F44B (👋) + U+1F3FB (🏻 light skin)
   # Expected: Single glyph with light skin tone
   ```

2. **Flags:**

   ```bash
   printf "\xf0\x9f\x87\xba\xf0\x9f\x87\xb8" | ./build/src/vterm-sdl3 -v -
   # U+1F1FA + U+1F1F8 (🇺🇸)
   # Expected: US flag emoji
   ```

3. **ZWJ sequences:**

   ```bash
   printf "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa6" | ./build/src/vterm-sdl3 -v -
   # U+1F468 + U+200D + U+1F469 + U+200D + U+1F467 + U+200D + U+1F466 (👨‍👩‍👧‍👦)
   # Expected: Family emoji
   ```

4. **Existing emoji.sh script:**
   ```bash
   ./examples/unicode/emoji.sh | timeout 2 ./build/src/vterm-sdl3 -v -
   # Should render emoji sequences correctly once combining is added
   ```

**Verification:**

- Verbose logging (`-v`) should show combination events
- Check that skin tones, flags, and families render as single glyphs
- Verify cell alignment remains correct (no gaps or overlaps)

### Risks and Edge Cases

**Risk 1: Double-rendering**

- If skip logic is wrong, might render modifier twice
- **Mitigation**: Strict `col <= last_combined_col` check

**Risk 2: Cell width misalignment**

- If combining consumes wrong number of cells, cursor may desync
- **Mitigation**: Test with varied sequences and compute consumed cells conservatively

**Risk 3: Non-emoji false positives**

- Some Unicode ranges might be incorrectly identified as emoji
- **Mitigation**: Use precise emoji detection (implement helpers carefully)

**Edge Case 1: Partial sequences at row end**

- Flag RI-RI split across line boundary
- **Solution**: `combine_cells_for_emoji()` must limit to current row

**Edge Case 2: Mixed emoji and text**

- Text codepoint adjacent to emoji
- **Solution**: `combine_cells_for_emoji()` stops at non-combinable cells

**Edge Case 3: Multiple modifiers**

- Invalid sequences like 👋🏻🏿 (two skin tones)
- **Solution**: HarfBuzz will shape as-is; font renderer handles

### Implementation Checklist

- [ ] Implement emoji detection helper functions (`is_emoji_presentation`, `is_regional_indicator`, `is_zwj`, `is_skin_tone_modifier`)
- [ ] Implement `combine_cells_for_emoji()` function (lookahead across cells, same-row only)
- [ ] Add `last_combined_col` tracking variable and skip check in row/column loops
- [ ] Replace single-cell codepoint collection with conditional combining logic (use conservative heuristic for consumed cells)
- [ ] Add verbose logging for combined emoji sequences
- [ ] Test with skin tone modifiers, flags, and ZWJ sequences
- [ ] Verify no gaps or overlaps in rendering
- [ ] Run full emoji.sh test suite and iterate

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

### P0 - Critical (Emoji Broken - FIX DESIGNED)

- [ ] **Implement multi-codepoint cell combining fix** 🔴 **READY TO IMPLEMENT**
  - Implement emoji detection helper functions and `combine_cells_for_emoji()`
  - Add `last_combined_col` skip tracking to row loop
  - Replace single-cell codepoint collection with conditional combining logic
  - **Location**: `src/renderer.c` lines ~375-506 (see LINE_NUMBER_CORRECTIONS.md for exact insert points)
  - **Test**: 👋🏻 (hand + light skin), 🇺🇸 (flag), 👨‍👩‍👧‍👦 (family)
  - **Design**: See "Multi-Codepoint Emoji Fix Design" section above

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
- Lines 745-834: `paint_linear_gradient()` - Linear gradient evaluation
- Lines 871-940: Radial gradient
- Lines 942-1019: Sweep gradient
- Lines 838-1276: `paint_colr_paint_recursive()` - **CORE PAINT EVALUATOR**
- Lines 1206-1254: PaintGlyph case
- Lines 1279-1369: `render_colr_paint_glyph()` - COLR v1 entry point
- Lines 1774-1948: `rasterize_glyph_index()` - Glyph rasterization dispatcher
- Lines 1951-2016: `ft_render_shaped()` - HarfBuzz shaping + rasterization

**Renderer:** `src/renderer.c`

- **Lines ~57-70:** Emoji detection helpers and combining function need to be ADDED here
- **Line 374:** `renderer_draw_terminal()` function starts
- **Line 375:** Comment "// Render each cell"
- **Line 375:** `for (int row = 0; row < display_rows; row++)` - **row loop**
- **Line 376:** `for (int col = 0; col < display_cols; col++)` - **column loop**
- **Lines 377-380:** Get cell from terminal
- **Lines 389-397:** Color conversion
- **Lines 399-408:** Background rendering
- **Lines 410-413:** Skip empty cells
- **Lines 439-444:** Single-cell codepoint collection (BUG LOCATION - must be replaced)
- **Lines 446-459:** Font style selection (normal/bold/emoji)
- **Line 461:** Check if font has COLR
- **Line 465:** Shaped rendering check (works if `cp_count > 1`)
- **Lines 465-506:** HarfBuzz shaped rendering (correct, just needs `cp_count > 1`)
- **Lines 508-537:** Render single glyph (first codepoint only)
- **Line 553:** End of column loop `}`
- **Line 554:** End of row loop `}`

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

1. **Skin tone modifiers** (CRITICAL - FIX DESIGNED):
   - Input: 👋🏻 (U+1F44B + U+1F3FB)
   - Expected: Single yellow hand with light skin tone
   - Actual: Yellow hand + separate brown square
   - **Cause**: Renderer collects only from single cell; combining helpers are missing
2. **Flag emoji** (CRITICAL - FIX DESIGNED):
   - Input: 🇺🇸 (U+1F1FA + U+1F1F8)
   - Expected: US flag emoji
   - Actual: "US" as text (regional indicators render separately)
   - **Cause**: Renderer collects only from single cell; combining helpers are missing

3. **Family ZWJ sequences** (CRITICAL - FIX DESIGNED):
   - Input: 👨‍👩‍👧‍👦 (man + ZWJ + woman + ZWJ + girl + ZWJ + boy)
   - Expected: Single family emoji
   - Actual: Individual person emoji rendered separately
   - **Cause**: Renderer collects only from single cell; combining helpers are missing
4. **Vehicle emoji** (HIGH):
   - Input: 🚗🚕🚙 (cars)
   - Expected: Colorful vehicle emoji
   - Actual: Gray/white boxes
   - Cause: Unknown - possibly missing composite modes or PaintExtend

**Root Cause Analysis:**

The renderer currently only gathers codepoints from the current cell (lines 439-444). During a git restore the emoji helpers and combining function were removed; these need to be implemented and integrated so that the shaped rendering path can receive multi-codepoint arrays.

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

### 5. Emoji Combining Strategy (NEW)

**Decision:** Implement emoji detection helpers and `combine_cells_for_emoji()` and call it from the render loop

- **Rationale**: The git-restored renderer dropped prior code; re-implementing the helpers and combining function is the correct fix
- **Integration point**: Main rendering loop at lines ~375-506
- **Skip mechanism**: Track `last_combined_col` per row to avoid double-rendering
- **Trigger**: Detect emoji range and call combining function
- **HarfBuzz integration**: Pass combined codepoints to existing shaped path

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
✅ **COLR v1 emoji layers render right-side-up** (Y-axis inversion FIXED)
✅ **COLR v1 emoji layers at correct offsets** (coordinate mapping FIXED)
✅ **COLR v0 fallback verified** (Three-level fallback: COLR v1 → COLR v0 → Grayscale)
✅ **Grayscale fallback for glyphs without COLR data** (Handles all edge cases)
✅ **HarfBuzz shaping infrastructure** (Ready to accept multi-codepoint arrays once provided)

### Broken (Root Cause Identified - Fix Designed)

🔴 **Multi-codepoint emoji sequences** (skin tones, flags, ZWJ sequences)

- **Root cause**: Renderer collects only from current cell; emoji helpers and combining function were removed in a git restore
- **Evidence**: `src/renderer.c` contains single-cell collection at lines 439-444 and shaped path at 465; `cp_count` is always 1 for cross-cell emoji
- **Fix designed**: Implement helpers and `combine_cells_for_emoji()` and integrate into render loop
- **Status**: Ready to implement - requires adding code (~150 lines) and wiring

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
✅ **Phase 6:** Root cause analysis for emoji sequences - **COMPLETE** (removed code identified; fix designed requiring reimplementation)

**Current State:**

**Working:**

- ✅ Simple emoji (single codepoint): 😀😃😄😁😆😅😂🤣 render perfectly
- ✅ COLR v1 paint graphs with gradients and transforms
- ✅ COLR v0 layer compositing with proper fallback
- ✅ Grayscale rendering for non-color glyphs
- ✅ Y-axis coordinate mapping (layers positioned correctly)
- ✅ HarfBuzz shaping infrastructure (ready for multi-codepoint)

**Broken (Fix Ready):**

- 🔴 Skin tone modifiers: 👋🏻 → renders as 👋 + 🏻 (renderer lacks combining helpers)
- 🔴 Flag emoji: 🇺🇸 → renders as U + S (renderer lacks combining helpers)
- 🔴 ZWJ sequences: 👨‍👩‍👧‍👦 → individual people (renderer lacks combining helpers)
- 🟡 Complex vehicle emoji: 🚗🚕🚙 → gray/white boxes (composite mode issue?)

## Next Steps (Priority Order)

### Critical (Fix Designed and Ready)

1. **Implement multi-codepoint cell combining integration** 🔴 **READY**
   - Implement emoji helpers and `combine_cells_for_emoji()` (~150 lines total) and wire into the render loop
   - Add skip tracking to row loop (1 line)
   - Replace single-cell collection with conditional combining logic (~30 lines)
   - **Location**: `src/renderer.c` lines ~57 (helpers) and ~375-506 (integration)
   - **Test cases**: Prepared and ready
   - **Expected result**: All multi-codepoint emoji sequences render correctly

2. **Debug complex COLR v1 rendering** 🟡
   - Test vehicles with verbose logging to identify missing composite modes
   - Check if PaintExtend REPEAT/REFLECT are needed
   - Verify gradient evaluations are correct

### Secondary (Correctness)

3. **Implement missing composite modes** (COLOR_DODGE, HARD_LIGHT, SOFT_LIGHT, etc.)
4. **Implement PaintExtend REPEAT/REFLECT**
5. **Performance optimization** (GPU gradients, texture atlas)

---

**Document Version:** 7.1
**Last Updated:** 2026-01-23
**Status:** Corrections applied — proposal updated to reflect that emoji combining code was removed and must be implemented; line numbers and scope have been corrected per LINE_NUMBER_CORRECTIONS.md
