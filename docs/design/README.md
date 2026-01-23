# Emoji Rendering Fix - Design Documentation

## Quick Start

**Current Status**: Implementation is broken (see screenshot evidence)

- ❌ Black boxes after simple emoji
- ❌ Skin tone modifiers render separately
- ❌ Flag emoji render as broken indicators
- ❌ Family emoji render as individual faces

**What to do**: Follow the implementation checklist

1. **Read**: `CURRENT-IMPLEMENTATION-PROBLEMS.md` ← Start here to understand what's broken
2. **Implement**: Follow `05-implementation-checklist.md` phase by phase
3. **Reference**: Consult documents 01-04 for detailed design

## Document Overview

| Document                               | Purpose                          | Status            |
| -------------------------------------- | -------------------------------- | ----------------- |
| `00-design-documents-index.md`         | Overview of all documents        | ✅ Complete       |
| `CURRENT-IMPLEMENTATION-PROBLEMS.md`   | What's broken and why            | ✅ Analysis       |
| `01-emoji-cell-storage-analysis.md`    | How libvterm stores emoji        | ✅ Research       |
| `02-cell-combining-algorithm.md`       | Algorithm specification          | ✅ Design         |
| `03-width-handling-and-black-boxes.md` | Fix black boxes after emoji      | ✅ Design         |
| `04-emoji-detection-heuristics.md`     | Refined emoji detection          | ✅ Design         |
| `05-implementation-checklist.md`       | **Step-by-step implementation**  | 🔨 Ready          |
| `06-colr-rendering-issues.md`          | Vehicle emoji gray boxes (later) | ⏸️ Separate issue |

## Problem Categories

### Critical (P0) - Must Fix First

- Black boxes after simple emoji → **Doc 03**
- Skin tone modifiers split → **Doc 02 + 03**
- Flag emoji split → **Doc 02 + 03**
- Family emoji split → **Doc 02 + 03**

All caused by same root issues:

1. Width tracking wrong
2. Skip logic wrong
3. Combining logic wrong

### High (P1) - Fix After P0

- Vehicle emoji gray boxes → **Doc 06**
- Complex COLR v1 rendering

Separate root cause (paint graph evaluation).

## The Core Issues

From `CURRENT-IMPLEMENTATION-PROBLEMS.md`:

### Issue 1: No Skip Tracking for Simple Emoji

```c
// Current: Only updates skip tracker in combining path
// Result: Continuation cells get rendered → black boxes

// Fix: Update skip tracker for ALL cells
last_rendered_col = col + columns_consumed - 1;
```

### Issue 2: Wrong Skip Value

```c
// Current: Uses codepoint count
last_combined_col = col + cp_count - 1;  // ❌

// Fix: Use columns consumed (sum of cell widths)
last_rendered_col = col + columns_consumed - 1;  // ✅
```

### Issue 3: Missing Output Parameter

```c
// Current function:
static int combine_cells_for_emoji(..., int max_combined)

// Fixed function:
static int combine_cells_for_emoji(..., int max_combined, int *columns_consumed)
```

### Issue 4: Wrong Combining Logic

```c
// Current: Checks if last codepoint collected is special
if (is_skin_tone_modifier(combined_cps[cp_count - 1])) continue;
// Doesn't work for base + modifier!

// Fix: Check if NEXT cell should be combined
if (should_combine_next_cell(current_cps, cp_count, next_cell.chars[0])) {
    // combine
}
```

## Implementation Path

**Critical**: Follow in order!

1. ✅ **Read** `CURRENT-IMPLEMENTATION-PROBLEMS.md`
2. 🔨 **Phase 1**: Fix emoji detection helpers (Doc 04)
   - Replace `is_emoji_presentation()` with refined functions
   - Add `should_try_emoji_combining()`
   - Add `should_combine_next_cell()`
3. 🔨 **Phase 2**: Fix combining function (Doc 02)
   - Add `int *columns_consumed` parameter
   - Track column offset (sum of widths)
   - Use `should_combine_next_cell()` for decisions
4. 🔨 **Phase 3**: Fix render loop (Doc 03)
   - Initialize `columns_consumed = cell.width` for ALL cells
   - Update `last_rendered_col` for ALL rendered cells
   - Remove special cases
5. ✅ **Phase 4**: Unit tests
   - Test emoji detection functions
   - Test combining decision logic
6. ✅ **Phase 5**: Integration tests
   - Test simple emoji (no black boxes)
   - Test skin tones
   - Test flags
   - Test ZWJ sequences
7. ✅ **Phase 6**: Regression tests
   - Ensure text still works
   - Ensure CJK still works

## Testing Quick Reference

### Test 1: Black Boxes (Should Pass After Phase 3)

```bash
printf "😀" | ./build/src/vterm-sdl3 -
# Expected: Single emoji, NO black box after it
```

### Test 2: Skin Tone (Should Pass After Phase 3)

```bash
printf "👋🏻" | ./build/src/vterm-sdl3 -
# Expected: Hand with light skin tone, single glyph
```

### Test 3: Flag (Should Pass After Phase 3)

```bash
printf "🇺🇸" | ./build/src/vterm-sdl3 -
# Expected: US flag emoji, single glyph
```

### Test 4: Family (Should Pass After Phase 3)

```bash
printf "👨‍👩‍👧‍👦" | ./build/src/vterm-sdl3 -
# Expected: Family emoji, single glyph
```

### Test 5: Full Suite

```bash
./examples/unicode/emoji.sh | timeout 2 ./build/src/vterm-sdl3 -v -
# Expected: All emoji render correctly
```

## Key Concepts

### Width vs Codepoints

```
👋🏻 (hand + light skin tone)

Codepoints: 2 (U+1F44B, U+1F3FB)
Cells: 2 (Cell[0] and Cell[2])
Columns: 4 (each cell has width=2)

For skip logic: Use columns (4)
For HarfBuzz: Use codepoints (2)
```

### Continuation Cells

```
😀 (grinning face)

Cell[0]: chars=[U+1F600], width=2
Cell[1]: continuation cell (should not render)

Skip cell[1] by: last_rendered_col = 0 + 2 - 1 = 1
```

### Combining Decision

```
👋🏻

Cell[0]: U+1F44B (hand)
Cell[2]: U+1F3FB (skin tone)

Decision: Look at Cell[2].chars[0]
Is U+1F3FB a skin tone modifier? Yes
→ Combine!
```

## Success Criteria

After completing phases 1-6:

- ✅ No black boxes after simple emoji
- ✅ All skin tone combinations work
- ✅ All flag emoji work
- ✅ All ZWJ sequences work
- ✅ No rendering artifacts
- ✅ Text rendering unchanged
- ✅ CJK rendering unchanged

## Next Steps After Success

1. Update `GlyphRenderingProposal.md` to mark P0 issues as complete
2. Tackle Document 06 (COLR v1 vehicle emoji)
3. Consider performance optimizations (texture atlas, etc.)

## Questions?

- **What's broken?** → Read `CURRENT-IMPLEMENTATION-PROBLEMS.md`
- **How to fix?** → Follow `05-implementation-checklist.md`
- **Why this approach?** → Read docs 01-04 for rationale
- **What about vehicles?** → That's doc 06, separate issue

---

**Created**: 2025-01-23
**Status**: Ready for implementation
**Start Here**: `CURRENT-IMPLEMENTATION-PROBLEMS.md` → `05-implementation-checklist.md`
