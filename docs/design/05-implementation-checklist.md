# Implementation Checklist and Testing Strategy

## Document Purpose

This document provides a step-by-step implementation plan and comprehensive testing strategy for the emoji rendering fix.

## Implementation Steps

### Phase 1: Fix Helper Functions

**File**: `src/renderer.c`

#### Task 1.1: Replace emoji detection helpers

**Current code** (lines ~59-89):

```c
static bool is_emoji_presentation(uint32_t cp) {
    return (cp >= 0x2000 && cp <= 0x27FF) ||  // TOO BROAD
           (cp >= 0x1F000 && cp <= 0x1F9FF) ||
           // ...
}
```

**Replace with**: Refined functions from document 04

- `is_emoji_base_range()`
- `is_ambiguous_emoji()`
- `is_emoji_keycap_base()`
- Keep: `is_skin_tone_modifier()`, `is_regional_indicator()`, `is_zwj()`, `is_variation_selector()`
- Add: `should_try_emoji_combining()`
- Add: `should_combine_next_cell()`

**Validation**:

- ✓ Compile without errors
- ✓ No warnings about unused functions

---

### Phase 2: Fix Combining Function

**File**: `src/renderer.c`

#### Task 2.1: Update function signature

**Current** (line ~92):

```c
static int combine_cells_for_emoji(Terminal *term, int row, int col,
                                   uint32_t *combined_codepoints,
                                   int max_combined)
```

**Change to**:

```c
static int combine_cells_for_emoji(Terminal *term, int row, int col,
                                   uint32_t *combined_codepoints,
                                   int max_combined,
                                   int *columns_consumed)
```

#### Task 2.2: Rewrite combining logic

**Current issues**:

1. Returns codepoint count instead of columns consumed
2. Doesn't track cell widths properly
3. Combining decision logic is simplistic

**New algorithm** (from document 02):

```c
static int combine_cells_for_emoji(Terminal *term, int row, int col,
                                   uint32_t *combined_codepoints,
                                   int max_combined,
                                   int *columns_consumed)
{
    int term_cols;
    terminal_get_dimensions(term, NULL, &term_cols);

    // Read first cell
    VTermScreenCell cell;
    if (terminal_get_cell(term, row, col, &cell) < 0 || cell.chars[0] == 0) {
        *columns_consumed = 0;
        return 0;
    }

    // Collect first cell's codepoints
    int cp_count = 0;
    for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0; i++) {
        combined_codepoints[cp_count++] = cell.chars[i];
    }

    int col_offset = cell.width;  // Track column offset
    int lookahead_col = col + cell.width;

    // Look ahead for combinable cells
    const int MAX_LOOKAHEAD = 10;
    while (lookahead_col < term_cols && lookahead_col < (col + MAX_LOOKAHEAD)) {
        VTermScreenCell next_cell;
        if (terminal_get_cell(term, row, lookahead_col, &next_cell) < 0) {
            break;
        }

        if (next_cell.chars[0] == 0) {
            break;  // End of content
        }

        // Should we combine this cell?
        if (!should_combine_next_cell(combined_codepoints, cp_count, next_cell.chars[0])) {
            break;
        }

        // Add next cell's codepoints
        for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && next_cell.chars[i] != 0; i++) {
            if (cp_count >= max_combined - 1) {
                break;
            }
            combined_codepoints[cp_count++] = next_cell.chars[i];
        }

        col_offset += next_cell.width;
        lookahead_col += next_cell.width;
    }

    *columns_consumed = col_offset;
    return cp_count;
}
```

**Validation**:

- ✓ Compile without errors
- ✓ Returns correct values for test cases (see Phase 4)

---

### Phase 3: Fix Render Loop

**File**: `src/renderer.c` (lines ~485-660)

#### Task 3.1: Initialize skip tracking

**Find** (line ~486):

```c
for (int row = 0; row < display_rows; row++) {
    int last_combined_col = -1;  // Already exists
```

**Change variable name** for clarity:

```c
for (int row = 0; row < display_rows; row++) {
    int last_rendered_col = -1;  // More accurate name
```

#### Task 3.2: Add skip check at loop start

**Find** (line ~488):

```c
for (int col = 0; col < display_cols; col++) {
    if (col <= last_combined_col) {  // Already exists
        continue;
    }
```

**Keep as is**, but update variable name:

```c
for (int col = 0; col < display_cols; col++) {
    if (col <= last_rendered_col) {
        continue;
    }
```

#### Task 3.3: Update codepoint collection logic

**Find** (lines ~530-560, the section that collects codepoints):

**Current approach**:

```c
// Check if we have an emoji that might need combining
uint32_t cps[VTERM_MAX_CHARS_PER_CELL];
int cp_count = 0;
uint32_t combined_cps[VTERM_MAX_CHARS_PER_CELL * 2];
int combined_cp_count = 0;

if (cell.chars[0] != 0 && (is_emoji_presentation(cell.chars[0]) || ...)) {
    combined_cp_count = combine_cells_for_emoji(term, row, col, combined_cps, ...);
    // ...
}
```

**Replace with** (simplified approach):

```c
// Collect codepoints - try combining if emoji
uint32_t cps[32];
int cp_count = 0;
int columns_consumed = cell.width;  // Default: this cell's width

if (should_try_emoji_combining(cell.chars[0])) {
    // Try emoji combining
    int combine_columns = 0;
    cp_count = combine_cells_for_emoji(term, row, col, cps, 32, &combine_columns);

    if (cp_count > 0) {
        columns_consumed = combine_columns;
    }
} else {
    // Regular codepoint collection
    for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0; i++) {
        cps[cp_count++] = cell.chars[i];
    }
}

// Log combining decision
if (cp_count > 1) {
    vlog("Combined %d codepoints across %d columns at (%d,%d)\n",
         cp_count, columns_consumed, row, col);
}
```

#### Task 3.4: Update last_rendered_col for ALL cells

**Critical**: Must update skip tracker for both combined and single-cell emoji

**Find** several places where rendering happens:

1. After shaped rendering (line ~590):

```c
// Free shaped arrays
free(shaped);
continue;  // done with this cell
```

**Add before `continue`**:

```c
last_rendered_col = col + columns_consumed - 1;
```

2. After single glyph rendering (line ~650):

```c
rend->font->free_glyph_bitmap(rend->font, glyph_bitmap);
```

**Add after**:

```c
last_rendered_col = col + columns_consumed - 1;
```

**Validation**:

- ✓ No cell is rendered twice
- ✓ No continuation cells are rendered
- ✓ All combined cells are skipped

---

### Phase 4: Unit Testing

Create test file: `/tmp/test_emoji_combining.c`

```c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// Copy helper functions from renderer.c
// (is_emoji_base_range, is_skin_tone_modifier, etc.)

void test_emoji_detection() {
    printf("Test: Emoji Detection\n");

    // Test emoji base range
    assert(is_emoji_base_range(0x1F600) == true);   // 😀
    assert(is_emoji_base_range(0x1F44B) == true);   // 👋
    assert(is_emoji_base_range(0x1F004) == false);  // 🀄 (Mahjong)

    // Test regional indicators
    assert(is_regional_indicator(0x1F1FA) == true); // 🇺
    assert(is_regional_indicator(0x1F1F8) == true); // 🇸
    assert(is_regional_indicator(0x1F600) == false);

    // Test skin tone modifiers
    assert(is_skin_tone_modifier(0x1F3FB) == true); // 🏻
    assert(is_skin_tone_modifier(0x1F44B) == false);

    printf("  ✓ All emoji detection tests passed\n");
}

void test_combining_decision() {
    printf("Test: Combining Decisions\n");

    uint32_t seq1[] = {0x1F44B};  // 👋
    assert(should_combine_next_cell(seq1, 1, 0x1F3FB) == true);  // + 🏻 Yes
    assert(should_combine_next_cell(seq1, 1, 0x1F600) == false); // + 😀 No

    uint32_t seq2[] = {0x1F1FA};  // 🇺
    assert(should_combine_next_cell(seq2, 1, 0x1F1F8) == true);  // + 🇸 Yes (flag)

    uint32_t seq3[] = {0x1F468, 0x200D};  // 👨 + ZWJ
    assert(should_combine_next_cell(seq3, 2, 0x1F469) == true);  // + 👩 Yes

    printf("  ✓ All combining decision tests passed\n");
}

int main() {
    test_emoji_detection();
    test_combining_decision();
    printf("\n✅ All unit tests passed\n");
    return 0;
}
```

**Run**:

```bash
gcc -o /tmp/test_emoji_combining /tmp/test_emoji_combining.c
/tmp/test_emoji_combining
```

---

### Phase 5: Integration Testing

#### Test 5.1: Simple Emoji (Black Box Fix)

**Input**:

```bash
printf "😀" | ./build/src/vterm-sdl3 -v -
```

**Expected**:

- Single emoji rendered
- NO black box after it
- Log: `columns_consumed = 2`
- Cell[0] rendered, Cell[1] skipped

#### Test 5.2: Multiple Simple Emoji

**Input**:

```bash
printf "😀😃😄" | ./build/src/vterm-sdl3 -v -
```

**Expected**:

- Three emoji rendered without gaps
- No black boxes
- Log shows columns_consumed = 2 for each

#### Test 5.3: Skin Tone Modifier

**Input**:

```bash
printf "👋🏻" | ./build/src/vterm-sdl3 -v -
```

**Expected**:

- Single glyph with light skin tone
- Log: `Combined 2 codepoints across 4 columns`
- No separate hand and modifier

#### Test 5.4: Flag Emoji

**Input**:

```bash
printf "🇺🇸" | ./build/src/vterm-sdl3 -v -
```

**Expected**:

- US flag emoji (single glyph)
- Log: `Combined 2 codepoints across 4 columns`
- Not broken regional indicators

#### Test 5.5: ZWJ Sequence

**Input**:

```bash
printf "👨‍👩‍👧‍👦" | ./build/src/vterm-sdl3 -v -
```

**Expected**:

- Family emoji (single glyph)
- Log: `Combined 7 codepoints across X columns`
- Not individual faces

#### Test 5.6: Full emoji.sh Script

**Input**:

```bash
./examples/unicode/emoji.sh | timeout 2 ./build/src/vterm-sdl3 -v -
```

**Expected**:

- All emoji categories render correctly
- No black boxes
- Skin tones work
- Flags work
- Family emoji work
- Check vehicle emoji (known issue with COLR v1)

---

### Phase 6: Regression Testing

Ensure we didn't break existing functionality:

#### Test 6.1: ASCII Text

```bash
echo "Hello, World!" | ./build/src/vterm-sdl3 -
```

Expected: Normal text rendering

#### Test 6.2: CJK Characters

```bash
printf "你好世界" | ./build/src/vterm-sdl3 -
```

Expected: Width=2 characters render correctly

#### Test 6.3: Combining Diacritics

```bash
printf "café" | ./build/src/vterm-sdl3 -
```

Expected: Accented characters render correctly

#### Test 6.4: ANSI Colors

```bash
printf "\x1b[31mRed\x1b[0m" | ./build/src/vterm-sdl3 -
```

Expected: Colored text works

---

## Success Criteria

### Must Pass ✅

1. ✅ Simple emoji render without black boxes
2. ✅ Skin tone modifiers combine correctly
3. ✅ Flag emoji combine correctly
4. ✅ ZWJ family sequences combine correctly
5. ✅ No double-rendering of cells
6. ✅ ASCII and CJK text still works

### Known Limitations ⚠️

1. ⚠️ Vehicle emoji (🚗🚕🚙) may still have COLR v1 rendering issues (separate issue)
2. ⚠️ Very complex emoji sequences (>16 codepoints) may be truncated

### Out of Scope ❌

1. ❌ COLR v1 gradient/composite mode fixes (document 06)
2. ❌ BiDi/RTL support
3. ❌ Font fallback chains

---

## Rollback Plan

If implementation causes regressions:

1. **Isolate the problem**: Run unit tests to identify failing component
2. **Git stash changes**: `git stash`
3. **Verify clean build works**: `./build.sh`
4. **Re-apply changes incrementally**: Phase by phase
5. **Add debug logging**: Track which cells are combined and skipped

---

## Next Steps After Implementation

1. Update GlyphRenderingProposal.md with completion status
2. Document any edge cases discovered
3. Create document 06 for COLR v1 vehicle emoji issue
4. Consider performance optimizations (texture atlas, etc.)

---

**Document Status**: Ready for implementation
**Last Updated**: 2025-01-23
**Implementation Order**: Phases 1→2→3→4→5→6
