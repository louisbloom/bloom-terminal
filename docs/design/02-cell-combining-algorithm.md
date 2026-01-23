# Cell Combining Algorithm Design

## Document Purpose

This document specifies the exact algorithm for combining cells that contain emoji sequences, accounting for cell widths and proper skip tracking.

## Problem Statement

From document 01, we know:

- Emoji sequences are split across multiple cells by libvterm
- Each emoji typically has width=2
- We need to combine codepoints for HarfBuzz while tracking cells consumed

Current implementation issues:

1. ❌ Returns codepoint count instead of cells consumed
2. ❌ Doesn't account for cell width
3. ❌ Skip logic uses wrong value (codepoint count instead of cells)
4. ❌ No continuation cell detection

## Algorithm Specification

### Function Signature

```c
/**
 * Combine cells for emoji sequences
 *
 * @param term          Terminal instance
 * @param row           Current row
 * @param col           Starting column
 * @param combined_cps  Output buffer for combined codepoints
 * @param max_combined  Size of output buffer
 * @param cells_read    Output: number of cells consumed (for skip logic)
 *
 * @return Number of codepoints in combined_cps (for HarfBuzz)
 */
static int combine_cells_for_emoji(Terminal *term, int row, int col,
                                   uint32_t *combined_cps,
                                   int max_combined,
                                   int *cells_read);
```

### Algorithm Steps

#### Step 1: Read First Cell

```c
VTermScreenCell cell;
terminal_get_cell(term, row, col, &cell);

// Collect all codepoints from first cell
int cp_count = 0;
int cells_consumed = 0;

for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0; i++) {
    combined_cps[cp_count++] = cell.chars[i];
}

cells_consumed++;  // We read this cell

int width_of_first = cell.width;  // Save for continuation cell check
```

#### Step 2: Check if Combining is Needed

```c
// Check first codepoint - is it emoji?
if (!is_emoji_presentation(combined_cps[0])) {
    *cells_read = cells_consumed;
    return cp_count;  // Not emoji, done
}

// Check last codepoint of first cell - is it ZWJ?
bool has_zwj = (cp_count > 1 && is_zwj(combined_cps[cp_count - 1]));
```

#### Step 3: Look Ahead for Continuations

```c
int lookahead_col = col + width_of_first;  // Skip continuation cells
int max_lookahead = 10;  // Reasonable limit for emoji sequences

while (lookahead_col < term_cols && lookahead_col < (col + max_lookahead)) {
    VTermScreenCell next_cell;
    if (terminal_get_cell(term, row, lookahead_col, &next_cell) < 0) {
        break;
    }

    // Empty cell = end of sequence
    if (next_cell.chars[0] == 0) {
        break;
    }

    // Check if this cell should be combined
    uint32_t first_cp = next_cell.chars[0];
    bool should_combine = false;

    if (has_zwj && is_emoji_presentation(first_cp)) {
        // ZWJ + emoji = combine
        should_combine = true;
    } else if (is_skin_tone_modifier(first_cp)) {
        // Previous emoji + skin tone = combine
        should_combine = true;
    } else if (is_regional_indicator(first_cp) &&
               is_regional_indicator(combined_cps[0])) {
        // RI + RI = flag, combine
        should_combine = true;
    }

    if (!should_combine) {
        break;  // Not part of sequence
    }

    // Add this cell's codepoints
    for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && next_cell.chars[i] != 0; i++) {
        if (cp_count >= max_combined - 1) {
            break;
        }
        combined_cps[cp_count++] = next_cell.chars[i];
    }

    cells_consumed += next_cell.width;  // Track cells consumed by width
    lookahead_col += next_cell.width;   // Advance by width

    // Update ZWJ flag for next iteration
    has_zwj = (next_cell.chars[VTERM_MAX_CHARS_PER_CELL-1] == 0) ? false :
              is_zwj(next_cell.chars[next_cell.width - 1]);
}
```

#### Step 4: Return Results

```c
*cells_read = cells_consumed;
return cp_count;
```

## Usage in Renderer

### Integration Pattern

```c
// In renderer_draw_terminal()
for (int row = 0; row < display_rows; row++) {
    int last_combined_col = -1;

    for (int col = 0; col < display_cols; col++) {
        // Skip cells consumed by previous combining
        if (col <= last_combined_col) {
            continue;
        }

        VTermScreenCell cell;
        terminal_get_cell(term, row, col, &cell);

        // ... existing checks ...

        // Check if we should try combining
        if (cell.chars[0] != 0 && is_emoji_presentation(cell.chars[0])) {
            uint32_t combined_cps[32];
            int cells_consumed = 0;

            int cp_count = combine_cells_for_emoji(term, row, col,
                                                   combined_cps, 32,
                                                   &cells_consumed);

            if (cp_count > 1) {
                // Use shaped rendering with combined codepoints
                // ...

                // CRITICAL: Update skip based on cells consumed, not codepoints
                last_combined_col = col + cells_consumed - 1;
                continue;
            }
        }

        // Fall back to single cell rendering
```

## Edge Cases

### Edge Case 1: Emoji at End of Line

```c
Input: ... 👋🏻 (at columns 78-79 in 80-column terminal)

Behavior:
- Cell[78]: U+1F44B (hand), width=2
- Cell[79]: (continuation)
- Cell[80]: Would be U+1F3FB but doesn't exist

Solution: lookahead_col < term_cols check will stop at column 80
```

### Edge Case 2: Skin Tone Without Base

```c
Input: 🏻 (skin tone alone)

Behavior:
- Cell[0]: U+1F3FB, width=2
- is_emoji_presentation(U+1F3FB) = false (not in emoji range)
- Returns immediately without combining

Solution: Only emoji base triggers combining
```

### Edge Case 3: Multiple Modifiers

```c
Input: 👋🏻🏿 (hand + light skin + dark skin - invalid)

Behavior:
- Cell[0]: U+1F44B (hand), width=2
- Cell[2]: U+1F3FB (light), width=2
- Cell[4]: U+1F3FF (dark), width=2

Algorithm:
- Combine hand + light (cells_consumed = 4)
- Dark skin will be separate (starts at col=4)

Solution: Let HarfBuzz/font handle invalid sequences
```

### Edge Case 4: ZWJ at End of Cell Array

```c
Cell content: [U+1F468, U+200D, 0, 0, 0, 0]  (man + ZWJ)

Check: Need to look at chars[cp_count-1] where cp_count=2
Result: is_zwj(chars[1]) = true, correct
```

## Width Calculation Verification

### Test Cases

1. **Single emoji (😀)**
   - Cell[0]: U+1F600, width=2
   - cells_consumed = 1 (NOT 2!)
   - Why: width=2 is display columns, but it's ONE cell

2. **Skin tone (👋🏻)**
   - Cell[0]: U+1F44B, width=2
   - Cell[2]: U+1F3FB, width=2
   - cells_consumed = cells_consumed_first + next_cell.width = 1 + 2 = 3
   - Wait, this is WRONG!

### CORRECTION: Cell Count vs Column Offset

**Critical realization**: We need to track two different things:

1. **Cells consumed** (for loop increment): Number of cell structures read
2. **Column offset** (for skip logic): How many columns to skip

```c
// CORRECTED algorithm:
int cells_read_count = 1;      // Number of VTermScreenCell structures read
int column_offset = cell.width; // Columns consumed (for next position)

while (...) {
    cells_read_count++;
    column_offset += next_cell.width;
}

// For skip logic:
last_combined_col = col + column_offset - 1;

// But we return:
*cells_read = cells_read_count;  // For other purposes
```

Actually, let's simplify: **cells_read should be columns consumed**, since that's what skip logic needs!

### FINAL Algorithm Correction

```c
int combine_cells_for_emoji(..., int *columns_consumed) {
    // ...

    int cp_count = 0;
    int col_offset = cell.width;  // Start with first cell's width

    // Look ahead
    int lookahead_col = col + cell.width;
    while (...) {
        // Combine
        col_offset += next_cell.width;
        lookahead_col += next_cell.width;
    }

    *columns_consumed = col_offset;
    return cp_count;
}

// Usage:
int columns_consumed = 0;
int cp_count = combine_cells_for_emoji(term, row, col, cps, max, &columns_consumed);
last_combined_col = col + columns_consumed - 1;
```

## Revised Function Signature

```c
/**
 * Combine cells for emoji sequences
 *
 * @param term              Terminal instance
 * @param row               Current row
 * @param col               Starting column
 * @param combined_cps      Output buffer for combined codepoints
 * @param max_combined      Size of output buffer
 * @param columns_consumed  Output: display columns consumed (for skip logic)
 *
 * @return Number of codepoints in combined_cps (for HarfBuzz)
 */
static int combine_cells_for_emoji(Terminal *term, int row, int col,
                                   uint32_t *combined_cps,
                                   int max_combined,
                                   int *columns_consumed);
```

## Next Steps

This algorithm needs to be implemented and tested with:

1. Simple emoji (width tracking)
2. Skin tones (column offset calculation)
3. Flags (two RIs)
4. ZWJ sequences (multi-cell)

---

**Document Status**: Ready for implementation
**Last Updated**: 2025-01-23  
**Next Document**: 03-width-handling-and-black-boxes.md
