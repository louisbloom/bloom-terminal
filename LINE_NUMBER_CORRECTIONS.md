# Line Number Corrections for GlyphRenderingProposal.md

## Status: renderer.c was git restored - major corrections needed

### What Changed

The `renderer.c` file was restored to a clean state during research. This means:

**REMOVED (no longer exists):**

- ❌ Lines 69-91: Emoji detection helpers (is_emoji_presentation, is_regional_indicator, etc.)
- ❌ Lines 107-216: `combine_cells_for_emoji()` function
- ❌ All emoji-specific code that was referenced in the proposal

**Current actual state:**

- ✅ File is 599 lines long (not 700+ as proposal claimed)
- ✅ Clean, minimal renderer with no emoji-specific logic

### Corrected Line Numbers for Current renderer.c

**Main Rendering Loop:**

- Line 342: `renderer_draw_terminal()` function starts
- Line 374: Comment "// Render each cell"
- Line 375: `for (int row = 0; row < display_rows; row++)` - **row loop**
- Line 376: `for (int col = 0; col < display_cols; col++)` - **column loop**

**Cell Processing:**

- Lines 377-380: Get cell from terminal
- Lines 389-397: Color conversion
- Lines 399-408: Background rendering
- Lines 410-413: Skip empty cells

**Codepoint Collection (CURRENT - NEEDS REPLACEMENT):**

- Lines 439-444: **Single-cell codepoint collection**
  ```c
  uint32_t cps[VTERM_MAX_CHARS_PER_CELL];
  int cp_count = 0;
  for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0; i++) {
      cps[cp_count++] = cell.chars[i];
  }
  ```
  **This is the bug location - only collects from single cell**

**Font Selection:**

- Lines 446-459: Font style selection (normal/bold/emoji)
- Line 461: Check if font has COLR

**Shaped Rendering Path (WORKS BUT NEVER TRIGGERED):**

- Line 464: Comment "// If multiple codepoints, try shaped rendering"
- Line 465: `if (cp_count > 1 && rend->font && rend->font->render_shaped)`
- Lines 466-506: HarfBuzz shaped rendering (correct, just needs cp_count > 1)

**Single Glyph Fallback:**

- Lines 508-537: Render single glyph (first codepoint only)

**Cell Loop End:**

- Line 553: End of column loop `}`
- Line 554: End of row loop `}`

### What Needs to Be Added

Since the emoji combining code was removed, the implementation needs to ADD:

1. **Emoji detection helpers** (after line 57, before glyph cache):
   - `is_emoji_presentation()`
   - `is_regional_indicator()`
   - `is_zwj()`
   - `is_skin_tone_modifier()`

2. **Cell combining function** (after emoji helpers, before glyph cache):
   - `combine_cells_for_emoji()` - full implementation

3. **Skip tracking in row loop** (line 375):
   - Add `int last_combined_col = -1;` after row loop starts

4. **Skip check in column loop** (after line 376):
   - Add skip check to avoid re-rendering combined cells

5. **Replace codepoint collection logic** (lines 439-444):
   - Add emoji range detection
   - Call `combine_cells_for_emoji()` for emoji
   - Keep single-cell collection for non-emoji

### Corrected Proposal Structure

**Section: "Multi-Codepoint Emoji Fix Design"**

Should say:

> **Discovery:** Emoji combining code DOES NOT EXIST - needs to be implemented!
>
> **Current State:**
>
> - `src/renderer.c` has NO emoji-specific logic (git restored to clean state)
> - Lines 439-444: Only collect codepoints from single cell
> - Line 465: Shaped rendering path exists but `cp_count` is always 1 for emoji
>
> **Solution:** Add emoji combining infrastructure and integrate into render loop

**Required Changes:**

**Change 1: Add emoji detection helpers (after line 57)**

```c
// Emoji helper functions (to be added after line 57)
static bool is_emoji_presentation(uint32_t cp);
static bool is_regional_indicator(uint32_t cp);
static bool is_zwj(uint32_t cp);
static bool is_skin_tone_modifier(uint32_t cp);
```

**Change 2: Add cell combining function (after helpers, before line 59)**

```c
static int combine_cells_for_emoji(Terminal *term, int row, int col,
                                   uint32_t *combined_codepoints,
                                   int max_combined);
// Full implementation ~100 lines
```

**Change 3: Add skip tracking (line 375)**

```c
for (int row = 0; row < display_rows; row++) {
    int last_combined_col = -1;  // ADD THIS LINE

    for (int col = 0; col < display_cols; col++) {
```

**Change 4: Add skip check (after line 376)**

```c
    for (int col = 0; col < display_cols; col++) {
        // ADD THESE LINES:
        if (col <= last_combined_col) {
            continue;
        }

        VTermScreenCell cell;
```

**Change 5: Replace codepoint collection (lines 439-444)**

Replace the existing 6-line single-cell collection with ~30 lines that:

- Detect emoji range
- Call `combine_cells_for_emoji()` for emoji
- Update `last_combined_col`
- Keep single-cell collection for non-emoji

### Summary of Corrections Needed

**GlyphRenderingProposal.md needs updates in these sections:**

1. **"Remaining Work" section (line 19):**
   - Change: "combine_cells_for_emoji() exists but is NEVER CALLED"
   - To: "Emoji combining logic DOES NOT EXIST - needs to be implemented"

2. **"Multi-Codepoint Emoji Fix Design" section (line 268+):**
   - Remove claims about existing code at lines 107-216
   - Update "Location of dead code" to "Location where code needs to be added"
   - Update all line numbers:
     - 598-603 → 439-444 (codepoint collection)
     - 624 → 465 (shaped rendering check)
     - 375-554 → 375-554 (cell loop correct)

3. **"Key Code Locations" section (line 621+):**
   - Remove "Lines 69-91: Emoji detection helpers"
   - Remove "Lines 107-216: combine_cells_for_emoji() - DEAD CODE"
   - Update "Lines 598-603" → "Lines 439-444"
   - Update "Lines 624-664" → "Lines 465-506"
   - Change description from "never called" to "needs to be implemented"

4. **"Implementation Checklist" (line 437+):**
   - Add: "Implement emoji detection helper functions"
   - Add: "Implement combine_cells_for_emoji() function"
   - Change: "Call combine_cells_for_emoji()" to "Add and call combine_cells_for_emoji()"

### Total Implementation Scope

**Original estimate:** ~25 lines (just wiring)
**Corrected estimate:** ~150 lines (implementation + wiring)

- Emoji helpers: ~40 lines
- combine_cells_for_emoji(): ~100 lines
- Integration/wiring: ~10 lines

This is still manageable and the approach is correct, just more code needs to be written than originally thought.

---

**Document Version:** 1.0  
**Date:** 2026-01-23  
**Status:** Corrections identified - proposal needs update before implementation
