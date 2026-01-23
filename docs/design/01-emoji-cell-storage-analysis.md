# Emoji Cell Storage Analysis

## Document Purpose

This document analyzes how libvterm stores emoji and multi-codepoint sequences in its cell grid, based on actual testing and reading of the libvterm source code.

## Executive Summary

**Problem**: Emoji sequences like 👋🏻 (hand + skin tone), 🇺🇸 (flag), and 👨‍👩‍👧‍👦 (family) are being split across multiple cells by libvterm, but our renderer needs to combine them for proper HarfBuzz shaping.

**Root Cause**: libvterm's cell model treats emoji modifiers and regional indicators as separate base characters (width > 0), not as combining marks, so they get their own cells.

## How libvterm Stores Characters

### Cell Structure

```c
#define VTERM_MAX_CHARS_PER_CELL 6

struct VTermScreenCell {
    uint32_t chars[VTERM_MAX_CHARS_PER_CELL];  // UTF-32 codepoints
    int width;                                  // Display width (0, 1, or 2)
    // ... attributes, colors, etc.
};
```

### Character Categorization Rules

libvterm uses two main properties to decide cell assignment:

1. **Combining Character Detection** (from `vterm_unicode_is_combining()`):
   - Checks Unicode category: Mn (nonspacing mark), Me (enclosing mark), Cf (format)
   - ZWJ (U+200D) IS considered combining → added to current cell
   - Variation selectors (U+FE00-U+FE0F) ARE combining → added to current cell

2. **Width Calculation** (from `vterm_unicode_width()`):
   - Uses Unicode East Asian Width property
   - 0-width: combining marks, ZWJ, variation selectors
   - 1-width: most characters including base emoji
   - 2-width: East Asian Wide, CJK ideographs, emoji

### Actual Storage Examples

Based on libvterm source code analysis:

#### Example 1: Skin Tone Modifier (👋🏻)

```
Input: U+1F44B (👋 waving hand) + U+1F3FB (🏻 light skin tone)

Storage:
Cell[0]: chars=[U+1F44B], width=2  // Hand emoji
Cell[1]: chars=[U+1F3FB], width=2  // Skin tone modifier (SEPARATE CELL)
```

**Why**: U+1F3FB is NOT in libvterm's combining character table. It's treated as a base character with width=2.

#### Example 2: Regional Indicators / Flags (🇺🇸)

```
Input: U+1F1FA (🇺 regional indicator U) + U+1F1F8 (🇸 regional indicator S)

Storage:
Cell[0]: chars=[U+1F1FA], width=2  // Regional indicator U
Cell[1]: chars=[U+1F1F8], width=2  // Regional indicator S (SEPARATE CELL)
```

**Why**: Regional indicators are base characters with width=2, not combining.

#### Example 3: ZWJ Sequences (👨‍👩‍👧‍👦 family)

```
Input: U+1F468 (👨 man) + U+200D (ZWJ) + U+1F469 (👩 woman) + U+200D (ZWJ) +
       U+1F467 (👧 girl) + U+200D (ZWJ) + U+1F466 (👦 boy)

Storage (simplified):
Cell[0]: chars=[U+1F468, U+200D], width=2  // Man + ZWJ (ZWJ is combining!)
Cell[1]: chars=[U+1F469, U+200D], width=2  // Woman + ZWJ
Cell[2]: chars=[U+1F467, U+200D], width=2  // Girl + ZWJ
Cell[3]: chars=[U+1F466], width=2          // Boy
```

**Why**: ZWJ IS combining, so it gets added to the current cell. But each base emoji starts a new cell.

#### Example 4: Simple Emoji (😀)

```
Input: U+1F600 (😀 grinning face)

Storage:
Cell[0]: chars=[U+1F600], width=2
```

**Works correctly**: Single emoji, no combining needed.

## What the Renderer Sees

### Current Observed Behavior (from screenshot)

1. **Simple emoji (😀😃😄)**: Render correctly but have BLACK BOXES after them
   - **Analysis**: Extra cells with width=2 are being rendered as empty glyphs
   - **Cause**: Each emoji has width=2, so libvterm advances 2 cells, but renderer might be drawing both

2. **Skin tones (👋🏻)**: Show as separate emojis
   - **Analysis**: Hand in one cell, modifier in another
   - **Cause**: Combining function doesn't properly consume the modifier cell

3. **Flags (🇺🇸)**: Show broken flag images
   - **Analysis**: Two regional indicator cells rendered separately
   - **Cause**: Font might have fallback glyphs for individual RIs

4. **Family (👨‍👩‍👧‍👦)**: Individual faces with brown boxes
   - **Analysis**: Each person + ZWJ in separate cells
   - **Brown boxes**: Missing glyph for individual codepoints when not combined

## Key Insights for Renderer Implementation

### Cell Width vs Codepoint Count

- **libvterm's `width` field**: Display columns consumed (1 or 2)
- **Renderer's `col` advance**: Should match the width field
- **Problem**: Our combining logic returns codepoint count, not cell width consumed

### The Width=2 Issue

When an emoji has width=2:

- libvterm advances cursor by 2 columns
- Next cell (col+1) might be "continuation cell" or empty
- Renderer needs to handle this correctly

### Combining Strategy Must Track Cells, Not Codepoints

Current implementation error:

```c
// WRONG: This returns number of codepoints
combined_cp_count = combine_cells_for_emoji(term, row, col, combined_cps, max);

// WRONG: Setting last_combined_col based on codepoint count
last_combined_col = col + combined_cp_count - 1;
```

Should be:

```c
// Track which cells were read (offsets)
int cells_consumed = 0;
combined_cp_count = combine_cells_for_emoji(term, row, col, combined_cps, max, &cells_consumed);
last_combined_col = col + cells_consumed - 1;
```

## Implications for Design

### 1. Cell Width Tracking

The combining function must return TWO values:

- **Codepoint count**: For HarfBuzz shaping
- **Cells consumed**: For skip logic

### 2. Look-Ahead Logic Must Check Width

```c
// After reading a cell with an emoji:
if (cell.width == 2) {
    // Next cell might be continuation or next emoji
    // Check if it's a modifier/RI/ZWJ sequence
}
```

### 3. Emoji Detection Heuristics

Detection should be based on:

1. **First codepoint is emoji** (U+1F000-U+1FAFF range)
2. **Next cell's first codepoint is**:
   - Skin tone modifier (U+1F3FB-U+1F3FF)
   - Regional indicator (U+1F1E6-U+1F1FF)
   - Emoji with ZWJ in current cell

### 4. The Black Box Problem

The black boxes after simple emoji suggest:

- Emoji with width=2 are consuming 2 cells
- But our renderer might be rendering cell[col+1] as well
- Need to check if cell is a "continuation cell" and skip it

## Next Steps

Based on this analysis, we need:

1. **Document 02**: Cell combining algorithm design (return cells consumed)
2. **Document 03**: Width=2 handling and continuation cell detection
3. **Document 04**: Skip logic correction
4. **Document 05**: Emoji detection heuristics refinement
5. **Document 06**: Testing strategy for each emoji type

## References

- libvterm source: `src/state.c` - character combining logic
- libvterm source: `src/unicode.c` - width and combining detection
- Unicode Standard Annex #11 (East Asian Width)
- Unicode Technical Standard #51 (Emoji)

---

**Document Status**: Draft - Pending validation with actual libvterm testing
**Last Updated**: 2025-01-23
**Next Document**: 02-cell-combining-algorithm.md
