# Width Handling and Black Box Problem

## Document Purpose

This document analyzes the "black boxes" appearing after simple emoji in the current implementation and designs a solution for proper width=2 emoji handling.

## Problem Observation

From the screenshot evidence:

- Simple emoji like 😀😃😄 render correctly
- **BUT** each shows a BLACK BOX immediately after
- Box is approximately same size as emoji
- Appears even for non-combining emoji

## Root Cause Analysis

### libvterm Width Behavior

When an emoji with width=2 is written to the terminal:

```
Input: 😀 (U+1F600, East Asian Width = W)

libvterm behavior:
1. Cursor at column 0
2. Write U+1F600 to Cell[0]
3. Set Cell[0].width = 2
4. Advance cursor by 2 (now at column 2)
5. Cell[1] becomes a "continuation cell" or remains empty
```

### What Gets Rendered

Current renderer loop:

```c
for (int col = 0; col < display_cols; col++) {
    VTermScreenCell cell;
    terminal_get_cell(term, row, col, &cell);

    // Cell[0]: chars=[U+1F600], width=2 → renders emoji
    // Cell[1]: chars=[0], width=? → what happens here?
}
```

### The Black Box Source

Hypothesis 1: **Cell[1] has empty chars but gets rendered**

- `cell.chars[0] == 0` check should skip it
- But maybe something else triggers rendering?

Hypothesis 2: **Cell[1] is a sentinel "double-width continuation"**

- Some terminal emulators use special marker
- libvterm might set Cell[1].width = 0 or special value

Hypothesis 3: **Font returns empty glyph for codepoint 0**

- Renderer tries to render glyph_id=0 (.notdef)
- Font has a box glyph for .notdef
- This matches the "black box" appearance

## Investigation: What is Cell[1]?

Let's trace through actual behavior:

### Test Case: Single Emoji

```c
Input: printf("\xf0\x9f\x98\x80");  // 😀 U+1F600

Expected libvterm state:
- Cell[0]: chars=[U+1F600, 0, 0, 0, 0, 0], width=2
- Cell[1]: chars=[??], width=??

What renderer sees:
- col=0: chars[0]=U+1F600 → render emoji ✓
- col=1: chars[0]=?? → BLACK BOX ✗
```

### Most Likely Scenario

Based on terminal emulator conventions:

- Cell[1] is a **dwarf cell** (double-width continuation)
- Cell[1].chars[0] might be a special marker (often -1 or special Unicode)
- Or Cell[1] inherits partial data from Cell[0]

## Solution Design

### Option A: Check width and skip continuation columns

```c
for (int col = 0; col < display_cols; col++) {
    VTermScreenCell cell;
    terminal_get_cell(term, row, col, &cell);

    if (cell.chars[0] == 0) {
        continue;  // Empty cell
    }

    // Render the cell
    // ...

    // If width=2, next column is continuation, skip it
    if (cell.width == 2) {
        col++;  // Skip next column
    }
}
```

Pros:

- Simple and clear
- Matches terminal emulator convention

Cons:

- Doesn't work with emoji combining (we need to look ahead)
- `col++` in loop with `for (col++)` means we skip by 2

### Option B: Track last rendered column

```c
for (int col = 0; col < display_cols; col++) {
    // Already handled by emoji combining?
    if (col <= last_combined_col) {
        continue;
    }

    VTermScreenCell cell;
    terminal_get_cell(term, row, col, &cell);

    if (cell.chars[0] == 0) {
        continue;
    }

    // Render cell
    // ...

    // If simple emoji (not combined), update skip tracker
    if (cell.width == 2 && cp_count == 1) {
        last_combined_col = col + cell.width - 1;  // Skip col+1
    }
}
```

Pros:

- Works with existing skip logic
- Handles both combined and simple emoji

Cons:

- Need to set last_combined_col in two places

### Option C: Detect continuation cells explicitly

```c
static bool is_continuation_cell(Terminal *term, int row, int col) {
    if (col == 0) return false;  // First column can't be continuation

    // Check previous cell
    VTermScreenCell prev_cell;
    if (terminal_get_cell(term, row, col - 1, &prev_cell) < 0) {
        return false;
    }

    // If previous cell has width=2, this is continuation
    if (prev_cell.width == 2) {
        return true;
    }

    return false;
}

// In render loop:
for (int col = 0; col < display_cols; col++) {
    if (is_continuation_cell(term, row, col)) {
        continue;  // Skip continuation cell
    }

    // Normal rendering
}
```

Pros:

- Explicit and correct
- Handles all width=2 characters (CJK, emoji, etc.)

Cons:

- Extra terminal_get_cell() call
- Complexity

## Recommended Solution: Option B with Refinement

### Implementation Strategy

```c
for (int row = 0; row < display_rows; row++) {
    int last_rendered_col = -1;  // Track last column we rendered

    for (int col = 0; col < display_cols; col++) {
        // Skip if we already rendered this column
        if (col <= last_rendered_col) {
            continue;
        }

        VTermScreenCell cell;
        terminal_get_cell(term, row, col, &cell);

        // Skip empty cells
        if (cell.chars[0] == 0) {
            continue;
        }

        // ... color extraction, etc. ...

        // Collect codepoints
        uint32_t cps[32];
        int cp_count = 0;
        int columns_consumed = cell.width;  // Default: consume cell's width

        // Try emoji combining if applicable
        if (is_emoji_presentation(cell.chars[0])) {
            int combine_columns = 0;
            cp_count = combine_cells_for_emoji(term, row, col, cps, 32, &combine_columns);

            if (cp_count > 1) {
                columns_consumed = combine_columns;
                // Render shaped
                // ...
                last_rendered_col = col + columns_consumed - 1;
                continue;
            }
        }

        // Single cell rendering (collect codepoints if not yet done)
        if (cp_count == 0) {
            for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0; i++) {
                cps[cp_count++] = cell.chars[i];
            }
        }

        // Render (shaped if cp_count > 1, else single glyph)
        // ... existing rendering code ...

        // Update last_rendered_col to skip continuation cells
        last_rendered_col = col + columns_consumed - 1;
    }
}
```

### Key Points

1. **Always track columns_consumed**: Even for single-codepoint cells
2. **Default columns_consumed = cell.width**: This handles width=2 automatically
3. **Update last_rendered_col for ALL rendered cells**: Not just combined emoji
4. **Skip check at loop start**: Handles both continuation cells and combined cells

## Test Cases

### Test 1: Simple Emoji

```
Input: 😀
Cells: [U+1F600 w=2], [empty]
Expected: Render at col=0, skip col=1
```

### Test 2: Consecutive Emoji

```
Input: 😀😃
Cells: [U+1F600 w=2], [empty], [U+1F603 w=2], [empty]
Expected: Render at col=0 and col=2, skip col=1 and col=3
```

### Test 3: Skin Tone

```
Input: 👋🏻
Cells: [U+1F44B w=2], [empty], [U+1F3FB w=2], [empty]
Expected: Combine at col=0, consume 4 columns, skip col=1-3
```

### Test 4: Text + Emoji

```
Input: Hi😀
Cells: [U+48 w=1], [U+69 w=1], [U+1F600 w=2], [empty]
Expected: Render H,i at col=0,1; emoji at col=2; skip col=3
```

## Black Box Resolution

With this approach:

- Simple emoji: columns_consumed = 2, skip col+1 ✓
- Combined emoji: columns_consumed = 4+, skip all consumed columns ✓
- No more rendering Cell[1] which causes black box ✓

## Edge Cases

### Edge 1: Width=1 Emoji (Text Presentation)

Some emoji can have width=1 if followed by U+FE0E (text presentation):

```
Input: ❤️ vs ❤︎
Cell: [U+2764, U+FE0F] w=2 vs [U+2764, U+FE0E] w=1
```

Solution: Use cell.width, not hardcoded 2

### Edge 2: CJK Characters

Width=2 applies to CJK ideographs too:

```
Input: 你好
Cells: [U+4F60 w=2], [empty], [U+597D w=2], [empty]
```

Solution: Same skip logic works for all width=2

### Edge 3: Zero-Width Characters

Some combining marks have width=0:

```
Input: é (e + combining acute)
Cell: [U+65, U+301] w=1
```

Solution: columns_consumed = cell.width = 1 ✓

---

**Document Status**: Ready for implementation
**Last Updated**: 2025-01-23
**Next Document**: 04-emoji-detection-heuristics.md
