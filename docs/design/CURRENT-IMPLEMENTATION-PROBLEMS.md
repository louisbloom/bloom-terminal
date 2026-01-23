# Current Implementation Problems - What Went Wrong

## Document Purpose

This document analyzes the current (broken) implementation to explain exactly what's wrong and how it differs from the correct design in documents 01-05.

## Screenshot Evidence

From the test run, we observe:

```
Emoji test:
Faces: 😀▮  😃▮  😄▮  😆▮  🤗▮  😉▮
```

(**▮** = black boxes)

```
Emoji with modifiers:
Skin tones: 👋▮ 👋🏻▮ ▮ ▮ ▮ 👋 👋🏻▮ ▮ ▮
```

(Split modifiers, black boxes, duplicates)

```
Family: 👦▮ 🏿▮ 👦🏿▮ 👦▮ 👦🏿▮ 🏿▮ 👦🏿▮
```

(Individual emojis, brown boxes for missing glyphs)

## Problem 1: Black Boxes After Simple Emoji ❌

### Current Behavior

```
Input: 😀 (U+1F600)
Cells: [U+1F600 w=2], [empty w=?]
Rendered: 😀▮ (emoji + black box)
```

### What the Code Does

**In renderer.c, line ~530**:

```c
// After rendering emoji at col=0:
// ... render emoji ...
// NO skip tracking update!
// Loop continues to col=1

// col=1: continuation cell
// cell.chars[0] might be 0, but something still renders
```

### Why It's Wrong

1. **No `last_rendered_col` update for simple emoji**
   - Only updated in combining path
   - Simple emoji (width=2) should also update it

2. **Continuation cell gets processed**
   - col=1 is continuation of col=0
   - Should be skipped but isn't

### Correct Fix (from Doc 03)

```c
// After rendering ANY cell:
last_rendered_col = col + columns_consumed - 1;

// For simple emoji:
columns_consumed = cell.width;  // = 2 for emoji
last_rendered_col = 0 + 2 - 1 = 1;

// Next iteration:
for (col = 1; ...) {
    if (col <= last_rendered_col) {  // 1 <= 1 → skip!
        continue;
    }
}
```

---

## Problem 2: Skin Tones Don't Combine ❌

### Current Behavior

```
Input: 👋🏻 (U+1F44B + U+1F3FB)
Cells: [U+1F44B w=2], [empty], [U+1F3FB w=2], [empty]
Rendered: 👋▮ 🏻▮ (separate hand and modifier)
```

### What the Code Does

**In combine_cells_for_emoji(), line ~159**:

```c
return current_cp_count;  // Returns number of codepoints
// Does NOT return columns consumed!
```

**In renderer, line ~560**:

```c
if (cp_count > 1) {
    columns_consumed = combine_columns;
    // ... render ...
    last_combined_col = col + combine_columns - 1;
}
```

Wait, the code DOES have `combine_columns`... but where does it come from?

**In renderer, line ~540**:

```c
int combine_columns = 0;
cp_count = combine_cells_for_emoji(term, row, col, cps, 32, &combine_columns);
```

Ah! The function signature already has `int *columns_consumed`, but...

**Check combine_cells_for_emoji() again**:

```c
static int combine_cells_for_emoji(Terminal *term, int row, int col,
                                   uint32_t *combined_codepoints,
                                   int max_combined)
{
    // ...
    return current_cp_count;
}
```

### The Bug!

**Function signature doesn't match call site!**

Declaration (current):

```c
static int combine_cells_for_emoji(..., int max_combined)
// NO columns_consumed parameter!
```

Call site:

```c
combine_cells_for_emoji(term, row, col, cps, 32, &combine_columns);
// Passing &combine_columns but function doesn't accept it!
```

This is a **compilation error** or passing wrong argument!

Let me check the actual current code again...

Actually, looking at my earlier implementation (the one I created), the signature was:

```c
static int combine_cells_for_emoji(Terminal *term, int row, int col,
                                   uint32_t *combined_codepoints,
                                   int max_combined)
```

And it doesn't have the `columns_consumed` out-parameter! So the caller must be doing something else.

### Re-analyzing Actual Current Code

**From my implementation, line ~559**:

```c
if (cp_count > 1) {
    // Update the last_combined_col to skip the cells we consumed
    last_combined_col = col + combined_cp_count - 1;  // BUG!
```

There it is! Using `combined_cp_count` (codepoint count) instead of columns consumed.

### Why It's Wrong

For 👋🏻:

```
Cell[0]: U+1F44B, width=2
Cell[2]: U+1F3FB, width=2

combined_cp_count = 2 (two codepoints)
Correct columns_consumed = 2 + 2 = 4

Current code:
last_combined_col = 0 + 2 - 1 = 1

Correct:
last_combined_col = 0 + 4 - 1 = 3
```

Result:

- Renders 👋🏻 at col=0 ✓
- col=1 skipped ✓
- **col=2 NOT skipped** ❌ → renders 🏻 again!
- col=3 skipped ✓

This explains the duplicate modifiers in the screenshot!

### Correct Fix (from Doc 02)

```c
static int combine_cells_for_emoji(..., int *columns_consumed) {
    int col_offset = cell.width;

    // Look ahead
    while (...) {
        col_offset += next_cell.width;  // Accumulate width!
    }

    *columns_consumed = col_offset;
    return cp_count;
}
```

---

## Problem 3: Combining Logic is Too Simplistic ❌

### Current Behavior

**In combine_cells_for_emoji(), lines ~143-158**:

```c
// If it's a ZWJ, we need to continue looking ahead for more emoji parts
if (current_cp_count > 0 && is_zwj(combined_codepoints[current_cp_count - 1])) {
    continue;
}

// If it's a skin tone modifier...
if (current_cp_count > 0 && is_skin_tone_modifier(combined_codepoints[current_cp_count - 1])) {
    continue;
}

// If it's a regional indicator...
if (current_cp_count > 0 && is_regional_indicator(combined_codepoints[current_cp_count - 1])) {
    continue;
}
```

### Why It's Wrong

This checks if the LAST codepoint we collected is special, then continues.

But the logic should check if the NEXT cell contains something we should combine!

Example with flag:

```
Cell[0]: U+1F1FA (🇺)
Cell[2]: U+1F1F8 (🇸)

Loop iteration 0:
- Collect U+1F1FA from Cell[0]
- Check: is last codepoint (U+1F1FA) an RI? Yes
- Continue to next cell

Loop iteration 1:
- Collect U+1F1F8 from Cell[2]
- Check: is last codepoint (U+1F1F8) an RI? Yes
- Continue to next cell

Loop iteration 2:
- Next cell is empty
- Break

Result: Collected [U+1F1FA, U+1F1F8] ✓
```

Actually, this might work! But let's trace through skin tone:

```
Cell[0]: U+1F44B (👋)
Cell[2]: U+1F3FB (🏻)

Loop iteration 0:
- Collect U+1F44B from Cell[0]
- Check: is last codepoint (U+1F44B) a skin tone? No
- Check: is last codepoint (U+1F44B) an RI? No
- Check: is last codepoint (U+1F44B) ZWJ? No
- Break! ❌

Result: Only collected [U+1F44B], didn't get modifier!
```

### The Real Bug

The loop only continues if the codepoint it JUST ADDED is a combiner (ZWJ, RI, skin tone).

But skin tone modifier comes in the NEXT cell, and the base emoji (👋) is not a skin tone!

### Correct Logic (from Doc 04)

```c
// Check NEXT cell's first codepoint
if (should_combine_next_cell(current_cps, cp_count, next_cell.chars[0])) {
    // Add next cell
} else {
    break;
}

// where should_combine_next_cell() checks:
// - If current has ZWJ, and next is emoji → combine
// - If next is skin tone modifier → combine
// - If current starts with RI, and next is RI → combine
```

---

## Problem 4: Emoji Detection Too Broad ❌

**In is_emoji_presentation(), line ~61**:

```c
return (cp >= 0x2000 && cp <= 0x27FF) ||  // TOO BROAD
```

This includes:

- U+2192: → (arrow - text character!)
- U+2600: ☀ (sun - text without U+FE0F)
- U+2764: ❤ (heart - text without U+FE0F)

These shouldn't trigger combining unless they have variation selectors.

### Correct Fix (from Doc 04)

```c
static bool is_emoji_base_range(uint32_t cp) {
    return (cp >= 0x1F300 && cp <= 0x1F5FF) ||
           (cp >= 0x1F600 && cp <= 0x1F64F) ||
           (cp >= 0x1F680 && cp <= 0x1F6FF) ||
           (cp >= 0x1F900 && cp <= 0x1F9FF) ||
           (cp >= 0x1FA70 && cp <= 0x1FAFF);
}

static bool is_ambiguous_emoji(uint32_t cp) {
    // Characters that MIGHT be emoji with U+FE0F
    return (cp >= 0x2600 && cp <= 0x27BF) ||
           // ... etc
}
```

---

## Summary of Bugs

| Bug                            | Current Code                           | Impact              | Fix Document |
| ------------------------------ | -------------------------------------- | ------------------- | ------------ |
| No skip for simple emoji       | Missing `last_rendered_col` update     | Black boxes         | Doc 03       |
| Wrong skip value               | Uses `cp_count` not `columns_consumed` | Duplicate modifiers | Doc 02       |
| Missing columns_consumed param | Function doesn't return it             | Can't track width   | Doc 02       |
| Wrong combining check          | Checks last codepoint, not next        | Skin tones fail     | Doc 04       |
| Emoji detection too broad      | U+2000-27FF too wide                   | False positives     | Doc 04       |
| No continuation cell check     | Treats all cells equally               | Renders empty cells | Doc 03       |

## The Fix Path

Follow **Document 05** (Implementation Checklist) which addresses all these bugs:

1. **Phase 1**: Fix emoji detection (Doc 04) → Correct triggers
2. **Phase 2**: Fix combining function (Doc 02) → Return columns_consumed
3. **Phase 3**: Fix render loop (Doc 03) → Skip tracking for all cells
4. **Phases 4-6**: Test everything

---

**Document Created**: 2025-01-23
**Purpose**: Explain what's broken in current implementation
**Next Step**: Start Document 05, Phase 1
