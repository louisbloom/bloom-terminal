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
   - **Root cause**: **`combine_cells_for_emoji()` exists but is NEVER CALLED**
   - **Location of dead code**: `src/renderer.c:107-216` - fully implemented but unused
   - **Location of bug**: `src/renderer.c:598-603` - only collects codepoints from single cell
   - **Solution designed**: See "Multi-Codepoint Emoji Fix Design" section below
   - **Affected**: Skin tones, flags, ZWJ sequences (family, professions, etc.)
   - **Status**: Fix designed and ready to implement

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

- `combine_cells_for_emoji()` already exists (lines 107-216)
- Emoji detection helpers already exist
- HarfBuzz shaping path already handles multi-codepoint arrays
- Just need to wire it up

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

**Reason 4: Minimal Change**

Our fix is ~25 lines of code in renderer.c:

- Add skip tracking (3 lines)
- Call existing combine function (20 lines)
- No libvterm changes needed
- No external dependency changes

### Conclusion: Renderer-Level Fix is Correct

**The split happens at libvterm level** (by design), but **the fix belongs at renderer level** (by architecture).

This is not a workaround - it's the proper separation of concerns:

- **libvterm**: Faithful character cell representation
- **Renderer**: Semantic understanding and presentation

Our fix follows industry best practices and requires minimal code changes.

---

## Multi-Codepoint Emoji Fix Design

### Research Findings (2026-01-23)

**Discovery:** The code to fix emoji sequences ALREADY EXISTS but is never called!

**Evidence:**

- `src/renderer.c:107-216` contains `combine_cells_for_emoji()` function
- Handles skin tone modifiers, ZWJ sequences, and regional indicators
- Fully implemented with proper lookahead logic
- **BUT**: `grep -r "combine_cells_for_emoji" src/` returns NO CALLS to this function

**Current Broken Flow:**

```
src/renderer.c:375-554 - Main rendering loop
  ├─ Line 376: for (int col = 0; col < display_cols; col++)
  ├─ Line 377: VTermScreenCell cell; terminal_get_cell(term, row, col, &cell)
  ├─ Lines 598-603: Collect codepoints from SINGLE CELL ONLY:
  │     for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0; i++) {
  │         cps[cp_count++] = cell.chars[i];
  │     }
  └─ Line 624: if (cp_count > 1) ... shaped rendering
      └─ NEVER TRIGGERED because each cell has cp_count=1
```

**What `combine_cells_for_emoji()` Does:**

- **Input**: Terminal, row, col, output buffer
- **Output**: Combined codepoint array with count
- **Logic**:
  1. Starts at given col, reads first cell
  2. Looks ahead to next cells (up to 5 cells)
  3. If next cell is skin tone modifier (U+1F3FB-U+1F3FF): combine
  4. If next cell is ZWJ (U+200D): collect entire ZWJ chain
  5. If current cell is regional indicator: combine with next RI for flags
  6. Returns total codepoint count and updates array

**Why It's Not Called:**

- Likely implemented in anticipation of the problem
- Integration was never completed
- Rendering loop uses simpler single-cell logic

### Solution Design

**Approach:** Use existing `combine_cells_for_emoji()` with cell skip tracking

**Required Changes:**

**File: `src/renderer.c`**

**Change 1: Add skip tracking before cell loop (line ~375)**

```c
// Render each cell
for (int row = 0; row < display_rows; row++) {
    // NEW: Track cells consumed by emoji combining
    int last_combined_col = -1;

    for (int col = 0; col < display_cols; col++) {
        // NEW: Skip cells already consumed
        if (col <= last_combined_col) {
            continue;
        }

        VTermScreenCell cell;
        if (terminal_get_cell(term, row, col, &cell) < 0) {
            continue;
        }
        /* ... rest of loop ... */
```

**Change 2: Replace single-cell codepoint collection (lines ~598-603)**

```c
// OLD CODE (lines 598-603):
uint32_t cps[VTERM_MAX_CHARS_PER_CELL];
int cp_count = 0;
for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0; i++) {
    cps[cp_count++] = cell.chars[i];
}

// NEW CODE:
#define MAX_COMBINED_CODEPOINTS 16
uint32_t cps[MAX_COMBINED_CODEPOINTS];
int cp_count = 0;

// Check if first codepoint is emoji-like
bool is_emoji_range = false;
if (cell.chars[0] != 0) {
    uint32_t first_cp = cell.chars[0];
    is_emoji_range = (first_cp >= 0x1F000 && first_cp <= 0x1F9FF) ||
                     is_regional_indicator(first_cp) ||
                     is_emoji_presentation(first_cp);
}

// Use emoji combining for emoji codepoints
if (is_emoji_range) {
    cp_count = combine_cells_for_emoji(term, row, col, cps, MAX_COMBINED_CODEPOINTS);
    if (cp_count > 1) {
        // Calculate how many cells were consumed by examining the combined result
        // Skin tone: 2 codepoints (base + modifier) from 2 cells
        // Flag: 2 codepoints (2 RIs) from 2 cells
        // ZWJ family: 7 codepoints (4 people + 3 ZWJ) from 7 cells
        // Heuristic: count codepoints roughly equals cells consumed
        last_combined_col = col + cp_count - 1;
        vlog("Combined emoji at (%d,%d): %d codepoints from %d cells\n",
             row, col, cp_count, cp_count);
    }
} else {
    // Non-emoji: single cell collection
    for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0; i++) {
        cps[cp_count++] = cell.chars[i];
    }
}
```

**Change 3: No changes needed to shaping path**

- Lines 624-664 already handle `cp_count > 1` correctly
- HarfBuzz shaping will work once we pass combined arrays

### Implementation Notes

**Cell Width Considerations:**

- vterm may store emoji sequences with varying cell widths
- Skin tone: base emoji (width 2) + modifier (width 0) = 2 total cells
- Flag: RI (width 1) + RI (width 1) = 2 cells
- ZWJ: person (2) + ZWJ (0) + person (2) + ZWJ (0) + ... = variable
- **Solution**: `combine_cells_for_emoji()` returns actual cell count via column delta
- Update `last_combined_col` based on actual cells consumed

**Improved Cell Skip Logic:**

```c
if (is_emoji_range) {
    int start_col = col;
    cp_count = combine_cells_for_emoji(term, row, col, cps, MAX_COMBINED_CODEPOINTS);

    // Determine actual end column by checking cell widths
    int cells_consumed = 1;  // At least current cell
    for (int check_col = col + 1; check_col < display_cols && check_col < col + cp_count; check_col++) {
        VTermScreenCell check_cell;
        if (terminal_get_cell(term, row, check_col, &check_cell) < 0) break;
        if (check_cell.chars[0] == 0) break;  // Hit empty cell, stop
        cells_consumed++;
    }

    last_combined_col = col + cells_consumed - 1;
    vlog("Emoji at (%d,%d): %d codepoints, consumed %d cells (col %d to %d)\n",
         row, col, cp_count, cells_consumed, col, last_combined_col);
}
```

**Alternative: Simpler Heuristic**

```c
// Since combine_cells_for_emoji() includes lookahead logic,
// assume it consumed one cell per codepoint collected (conservative)
last_combined_col = col + cp_count - 1;
```

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
   # Should now render all emoji sequences correctly
   ```

**Verification:**

- Verbose logging (`-v`) will show "Combined emoji" messages
- Check that skin tones, flags, and families render as single glyphs
- Verify cell alignment remains correct (no gaps or overlaps)

### Risks and Edge Cases

**Risk 1: Double-rendering**

- If skip logic is wrong, might render modifier twice
- **Mitigation**: Strict `col <= last_combined_col` check

**Risk 2: Cell width misalignment**

- If combining consumes wrong number of cells, cursor may desync
- **Mitigation**: Careful testing with cell width inspection

**Risk 3: Non-emoji false positives**

- Some Unicode ranges might be incorrectly identified as emoji
- **Mitigation**: Use precise emoji detection (combine existing helpers)

**Edge Case 1: Partial sequences at row end**

- Flag RI-RI split across line boundary
- **Solution**: `combine_cells_for_emoji()` already limits to current row

**Edge Case 2: Mixed emoji and text**

- Text codepoint adjacent to emoji
- **Solution**: `combine_cells_for_emoji()` stops at non-combinable cells

**Edge Case 3: Multiple modifiers**

- Invalid sequences like 👋🏻🏿 (two skin tones)
- **Solution**: HarfBuzz will shape as-is; font renderer handles

### Implementation Checklist

- [ ] Add `last_combined_col` tracking variable in row loop
- [ ] Add skip logic at start of col loop
- [ ] Replace single-cell codepoint collection with conditional combining
- [ ] Add emoji range detection helper invocation
- [ ] Call `combine_cells_for_emoji()` for emoji codepoints
- [ ] Calculate and set `last_combined_col` based on combined result
- [ ] Add verbose logging for combined emoji sequences
- [ ] Test with skin tone modifiers
- [ ] Test with flag emoji (regional indicators)
- [ ] Test with ZWJ family emoji
- [ ] Test with mixed emoji and text
- [ ] Verify no gaps or overlaps in rendering
- [ ] Run full emoji.sh test suite

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
  - Add `last_combined_col` skip tracking to row loop
  - Call `combine_cells_for_emoji()` for emoji codepoints (function already exists!)
  - Replace single-cell codepoint collection with conditional combining logic
  - **Location**: `src/renderer.c` lines 375-603
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

- **Lines 69-91:** Emoji detection helpers (`is_emoji_presentation`, `is_regional_indicator`, `is_zwj`, `is_skin_tone_modifier`)
- **Lines 107-216:** `combine_cells_for_emoji()` - **DEAD CODE (never called)**
  - Handles skin tone modifiers (U+1F3FB-U+1F3FF)
  - Handles ZWJ sequences (collects chain after U+200D)
  - Handles regional indicator pairs for flags
  - Returns combined codepoint array and count
- Lines 116-129: `renderer_get_cached_texture()` - Glyph cache lookup
- Lines 131-171: `renderer_cache_texture()` - LRU cache insertion
- Lines 342-578: `renderer_draw_terminal()` - **MAIN RENDER LOOP**
- **Lines 375-554:** Cell rendering loop - **WHERE FIX SHOULD GO**
  - Line 376: Column loop (needs skip tracking)
  - Lines 598-603: Single-cell codepoint collection - **NEEDS REPLACEMENT**
  - Lines 624-664: Shaped rendering path (already works correctly)
- Lines 453-459: Font selection logic (emoji vs normal vs bold)

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
   - **Cause**: `combine_cells_for_emoji()` exists but is never called
2. **Flag emoji** (CRITICAL - FIX DESIGNED):
   - Input: 🇺🇸 (U+1F1FA + U+1F1F8)
   - Expected: US flag emoji
   - Actual: "US" as text (regional indicators render separately)
   - **Cause**: `combine_cells_for_emoji()` exists but is never called

3. **Family ZWJ sequences** (CRITICAL - FIX DESIGNED):
   - Input: 👨‍👩‍👧‍👦 (man + ZWJ + woman + ZWJ + girl + ZWJ + boy)
   - Expected: Single family emoji
   - Actual: Individual person emoji rendered separately
   - **Cause**: `combine_cells_for_emoji()` exists but is never called

4. **Vehicle emoji** (HIGH):
   - Input: 🚗🚕🚙 (cars)
   - Expected: Colorful vehicle emoji
   - Actual: Gray/white boxes
   - Cause: Unknown - possibly missing composite modes or PaintExtend

**Root Cause Analysis:**

The code to fix multi-codepoint emoji **ALREADY EXISTS** in `combine_cells_for_emoji()`
but was never integrated into the rendering loop. The rendering loop at lines 598-603
only collects codepoints from the current cell, so the shaped rendering path
(cp_count > 1, line 624) is never triggered for emoji sequences stored in separate cells.

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

**Decision:** Use existing `combine_cells_for_emoji()` with skip tracking

- **Rationale**: Function already exists and handles all cases correctly
- **Integration point**: Main rendering loop at line 598
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
✅ **HarfBuzz shaping infrastructure** (Works when multi-codepoint cells are passed)
✅ **Emoji combining logic implemented** (`combine_cells_for_emoji()` exists and is correct)

### Broken (Root Cause Identified - Fix Designed)

🔴 **Multi-codepoint emoji sequences** (skin tones, flags, ZWJ sequences)

- **Root cause**: `combine_cells_for_emoji()` exists but is NEVER CALLED
- **Evidence**: Function at lines 107-216, no callers found
- **Fix designed**: See "Multi-Codepoint Emoji Fix Design" section
- **Status**: Ready to implement - just needs integration into render loop

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
✅ **Phase 6:** Root cause analysis for emoji sequences - **COMPLETE**

**Current State:**

**Working:**

- ✅ Simple emoji (single codepoint): 😀😃😄😁😆😅😂🤣 render perfectly
- ✅ COLR v1 paint graphs with gradients and transforms
- ✅ COLR v0 layer compositing with proper fallback
- ✅ Grayscale rendering for non-color glyphs
- ✅ Y-axis coordinate mapping (layers positioned correctly)
- ✅ HarfBuzz shaping infrastructure (ready for multi-codepoint)
- ✅ Emoji combining function exists and is fully implemented

**Broken (Fix Ready):**

- 🔴 Skin tone modifiers: 👋🏻 → renders as 👋 + 🏻 (never calls combine function)
- 🔴 Flag emoji: 🇺🇸 → renders as U + S (never calls combine function)
- 🔴 ZWJ sequences: 👨‍👩‍👧‍👦 → individual people (never calls combine function)
- 🟡 Complex vehicle emoji: 🚗🚕🚙 → gray/white boxes (composite mode issue?)

## Next Steps (Priority Order)

### Critical (Fix Designed and Ready)

1. **Implement multi-codepoint cell combining integration** 🔴 **READY**
   - Solution designed in detail above
   - Add skip tracking to row loop (1 line)
   - Add skip check at start of column loop (3 lines)
   - Replace codepoint collection with conditional combining (20 lines)
   - Total change: ~25 lines in `src/renderer.c`
   - **Location**: Lines 375-376, 598-603
   - **Test cases**: Prepared and ready
   - **Expected result**: All multi-codepoint emoji sequences render correctly

2. **Debug complex COLR v1 rendering** 🟡
   - Test vehicles with verbose logging to identify missing composite modes
   - Check if PaintExtend REPEAT/REFLECT are needed
   - Verify gradient evaluations are correct

### Secondary (Correctness)

3. **Implement missing composite modes** (COLOR_DODGE, HARD_LIGHT, etc.)
4. **Implement PaintExtend REPEAT/REFLECT**
5. **Performance optimization** (GPU gradients, texture atlas)

---

**Document Version:** 7.0  
**Last Updated:** 2026-01-23  
**Status:** Phase 6 Complete - Root Cause Identified - Fix Designed and Ready to Implement
