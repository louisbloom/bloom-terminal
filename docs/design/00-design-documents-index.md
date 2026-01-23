# Emoji Rendering Fix - Design Documents Index

## Overview

This directory contains a complete set of design documents that analyze and plan the fix for emoji rendering issues in vterm-sdl3. These documents were created based on observed rendering problems (black boxes, split emoji sequences) shown in testing.

## Document Structure

### Document 01: Cell Storage Analysis

**File**: `01-emoji-cell-storage-analysis.md`

**Purpose**: Understand how libvterm stores multi-codepoint emoji sequences

**Key Findings**:

- libvterm treats emoji modifiers as separate base characters (width=2)
- Skin tone modifiers get their own cells
- Regional indicators (flags) get separate cells
- ZWJ is combining, but base emoji still start new cells
- Root cause: Not a libvterm bug, this is correct terminal emulator behavior

**Implications**:

- Renderer must look ahead across cells
- Must track BOTH codepoint count (for HarfBuzz) AND columns consumed (for skip logic)
- Cell width != codepoint count

**Status**: ✅ Analysis complete

---

### Document 02: Combining Algorithm

**File**: `02-cell-combining-algorithm.md`

**Purpose**: Specify the exact algorithm for combining emoji cells

**Key Design**:

- Function signature includes `int *columns_consumed` output parameter
- Algorithm tracks column offsets, not cell count
- Look-ahead uses cell.width to advance properly
- Returns two values: codepoint count and columns consumed

**Critical Fix**:

```c
// WRONG (current):
last_combined_col = col + cp_count - 1;

// RIGHT (fixed):
last_combined_col = col + columns_consumed - 1;
```

**Algorithm Steps**:

1. Read first cell, collect codepoints
2. Check if combining needed (emoji detection)
3. Look ahead using cell.width offsets
4. Combine cells based on sequence rules
5. Return codepoint count and column offset

**Status**: ✅ Design complete, ready to implement

---

### Document 03: Width Handling and Black Boxes

**File**: `03-width-handling-and-black-boxes.md`

**Purpose**: Fix the black boxes appearing after simple emoji

**Root Cause**:

- Emoji with width=2 consume 2 display columns
- Renderer renders both column 0 (emoji) and column 1 (continuation cell)
- Continuation cell likely has empty glyph → black box

**Solution**:

- Track `last_rendered_col` for ALL cells (not just combined emoji)
- Set `columns_consumed = cell.width` by default
- Update skip tracker after every cell rendered
- Skip continuation cells automatically

**Key Insight**: This fixes BOTH black boxes AND emoji combining in one unified approach

**Status**: ✅ Design complete, ready to implement

---

### Document 04: Emoji Detection Heuristics

**File**: `04-emoji-detection-heuristics.md`

**Purpose**: Refine emoji detection to avoid false positives/negatives

**Current Problems**:

- `is_emoji_presentation()` too broad (includes non-emoji like arrows)
- Missing some emoji ranges
- No variation selector handling

**New Functions**:

- `is_emoji_base_range()`: Core emoji ranges only
- `is_ambiguous_emoji()`: Characters that need U+FE0F
- `is_emoji_keycap_base()`: 0-9, #, \* for keycap emoji
- `should_try_emoji_combining()`: Master detection function
- `should_combine_next_cell()`: Combining decision logic

**Special Patterns**:

- Keycap: digit + U+FE0F + U+20E3
- Flag: RI + RI (max 2)
- Skin tone: emoji base + modifier
- ZWJ: emoji + ZWJ + emoji + ...

**Status**: ✅ Design complete, ready to implement

---

### Document 05: Implementation Checklist

**File**: `05-implementation-checklist.md`

**Purpose**: Step-by-step plan for implementing the fixes

**Phases**:

1. **Phase 1**: Fix helper functions (emoji detection)
2. **Phase 2**: Fix combining function (algorithm + signature)
3. **Phase 3**: Fix render loop (skip tracking for all cells)
4. **Phase 4**: Unit testing (helper function tests)
5. **Phase 5**: Integration testing (emoji test cases)
6. **Phase 6**: Regression testing (ensure nothing broke)

**Test Cases**:

- Simple emoji (no black boxes)
- Skin tone modifiers (👋🏻)
- Flags (🇺🇸)
- ZWJ sequences (👨‍👩‍👧‍👦)
- Full emoji.sh script

**Success Criteria**:

- ✅ No black boxes
- ✅ Skin tones combine
- ✅ Flags combine
- ✅ Family emoji combine
- ✅ No regressions in text rendering

**Status**: ✅ Ready for implementation

---

### Document 06: COLR Rendering Issues

**File**: `06-colr-rendering-issues.md`

**Purpose**: Plan fix for vehicle emoji gray boxes (SEPARATE issue)

**Scope Separation**:

- Documents 01-05: Emoji combining problem
- Document 06: COLR v1 paint graph problem

**Problem**: Vehicle emoji (🚗🚕🚙) render as gray/white boxes

**Hypothesis**:

- Missing composite modes (COLOR_DODGE, HARD_LIGHT, etc.)
- Missing PaintExtend modes (REPEAT, REFLECT)
- Complex gradient configurations

**Approach**:

1. Add diagnostic logging (P0)
2. Test vehicle emoji to identify missing features
3. Implement missing composite modes (P1)
4. Implement PaintExtend modes (P2)
5. Fix gradient edge cases (P3)

**Status**: ⏸️ Investigation phase - implement docs 01-05 first

---

## Implementation Order

### Critical Path (Must Do First)

1. **Document 05, Phase 1**: Fix emoji helper functions
2. **Document 05, Phase 2**: Fix combining algorithm
3. **Document 05, Phase 3**: Fix render loop
4. **Document 05, Phase 4**: Unit tests
5. **Document 05, Phase 5**: Integration tests
6. **Document 05, Phase 6**: Regression tests

**Expected Result**: Black boxes gone, emoji combining works

### Follow-Up (After Critical Path)

7. **Document 06**: Investigate vehicle emoji
8. **Document 06**: Implement missing COLR features

**Expected Result**: All emoji render correctly

## Current Status Summary

### Problems Identified ✅

1. ✅ Black boxes after simple emoji → Width handling issue
2. ✅ Skin tones split (👋 + 🏻) → Combining issue + width tracking
3. ✅ Flags split (🇺 + 🇸) → Combining issue + width tracking
4. ✅ Family split → Combining issue + width tracking
5. ⏸️ Vehicle emoji gray boxes → COLR v1 issue (separate)

### Root Causes Identified ✅

1. ✅ Combining function returns codepoint count, not columns consumed
2. ✅ Skip logic uses wrong value (codepoints instead of columns)
3. ✅ No skip tracking for simple (non-combined) emoji
4. ✅ Continuation cells being rendered (width=2 handling)
5. ✅ Emoji detection too broad/narrow

### Solutions Designed ✅

All solutions designed and documented in docs 01-05.

### Ready for Implementation ✅

All design documents complete, implementation can begin following document 05.

---

## How to Use These Documents

### For Implementation:

1. Read documents 01-04 to understand the problem
2. Follow document 05 phase-by-phase
3. Refer back to 01-04 for details on each component
4. After emoji combining works, tackle document 06

### For Code Review:

1. Check that implementation matches document 02 (algorithm)
2. Verify all test cases from document 05 pass
3. Ensure helper functions match document 04 (detection)

### For Debugging:

1. Check which phase is failing (doc 05)
2. Review corresponding design doc (01-04)
3. Add logging as specified in document 05
4. Verify against test cases

---

## References

- **GlyphRenderingProposal.md**: Original design document (superseded by these docs)
- **LINE_NUMBER_CORRECTIONS.md**: Line number references (may be outdated)
- **AGENTS.md**: Project overview and architecture

---

**Index Last Updated**: 2025-01-23
**Total Documents**: 7 (including this index)
**Implementation Status**: Ready to begin (start with doc 05, phase 1)
