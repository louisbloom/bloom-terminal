# Emoji Sequence Fix - Executive Summary

## Your Question

**"Why do the code points go into different vterm cells in the first place? Shouldn't we prevent it at that level?"**

## Short Answer

**No - this is by design, and fixing at the renderer level is correct.**

libvterm is a **terminal emulator library** that faithfully represents character streams as cells. It doesn't understand emoji semantics - that's the **renderer's job**.

## Why libvterm Splits Emoji Sequences

### libvterm's Cell Assignment Logic

```c
// Simplified from libvterm src/state.c:
for (each codepoint) {
  if (vterm_unicode_is_combining(cp)) {
    append_to_current_cell(cp);  // Max 6 total per cell
  } else {
    advance_to_next_cell();
    start_new_cell(cp);
  }
}
```

**Combining character definition:** Based on Unicode categories (Mn, Me, Cf)

- ✅ Diacritics: U+0300-U+036F (é = e + ́)
- ✅ ZWJ: U+200D (zero-width joiner)
- ✅ Variation selectors: U+FE00-U+FE0F
- ❌ Emoji modifiers: U+1F3FB-U+1F3FF (skin tones) - **NOT combining**
- ❌ Regional indicators: U+1F1E6-U+1F1FF (flags) - **NOT combining**

### Why Emoji Modifiers Aren't "Combining"

Unicode defines two separate concepts:

1. **Combining Characters** (Unicode categories Mn/Me/Cf):
   - Modify the previous character
   - Examples: accents, diacritics, ZWJ
   - Terminal width: 0
   - Cell storage: Append to previous cell

2. **Emoji Modifiers** (not combining in Unicode sense):
   - Separate characters that form **grapheme clusters** with emoji
   - Examples: skin tones, gender signs
   - Terminal width: 2 (they're emoji themselves)
   - Cell storage: **New cell** (libvterm treats as base characters)

**libvterm follows Unicode categorization**, not emoji semantics.

## Why NOT Fix at libvterm Level

### 1. **Not libvterm's Responsibility**

Terminal emulators handle:

- ✅ Escape sequence parsing
- ✅ Cell grid management
- ✅ Cursor positioning
- ✅ Basic width calculation (East Asian Width)

Terminal emulators do NOT handle:

- ❌ Emoji sequence interpretation
- ❌ Text shaping (ligatures, complex scripts)
- ❌ Font selection
- ❌ Grapheme cluster understanding

**Emoji combining is rendering logic**, not terminal emulation logic.

### 2. **ABI/API Constraints**

```c
#define VTERM_MAX_CHARS_PER_CELL 6  // From vterm.h

typedef struct {
  uint32_t chars[VTERM_MAX_CHARS_PER_CELL];  // Fixed size!
  char     width;
  VTermScreenCellAttrs attrs;
  VTermColor fg, bg;
} VTermScreenCell;
```

**Problems:**

- 6 codepoint limit is **ABI-locked** (changing breaks all libvterm users)
- Family emoji: 👨‍👩‍👧‍👦 = 7 codepoints → **doesn't fit**
- Would need major libvterm API redesign

### 3. **External Dependency**

We don't control libvterm:

- Forking = maintenance burden
- Incompatible with system libvterm
- No other terminal emulator does this

### 4. **Separation of Concerns**

```
┌─────────────────────────────┬──────────────────────────────┐
│   Terminal Layer            │   Rendering Layer            │
│   (libvterm)                │   (our code)                 │
├─────────────────────────────┼──────────────────────────────┤
│ • Parse VT sequences        │ • Text shaping (HarfBuzz)    │
│ • Maintain cell grid        │ • Emoji sequence combining   │
│ • Cursor positioning        │ • Font selection             │
│ • Basic width (EAW)         │ • Glyph rasterization        │
│ • Character storage         │ • COLR paint evaluation      │
└─────────────────────────────┴──────────────────────────────┘
```

## Why Fix at Renderer Level ✅

### 1. **Industry Standard Practice**

How other terminal emulators handle this:

| Terminal  | Approach                                      |
| --------- | --------------------------------------------- |
| Alacritty | Renderer-level combining                      |
| Kitty     | Renderer-level combining                      |
| WezTerm   | Renderer-level combining                      |
| iTerm2    | Renderer-level combining                      |
| **Us**    | **Same approach** (just needs to be enabled!) |

### 2. **We Already Have the Code**

The fix already exists at `src/renderer.c:107-216`:

```c
static int combine_cells_for_emoji(Terminal *term, int row, int col,
                                   uint32_t *combined_codepoints,
                                   int max_combined)
{
    // Fully implemented:
    // ✅ Skin tone modifier detection
    // ✅ ZWJ chain collection
    // ✅ Regional indicator pairing
    // ✅ Cell lookahead logic

    // Just never called!
}
```

**It's not broken - it's just disconnected.**

### 3. **Minimal Change Required**

Total fix: **~25 lines** in `renderer.c`:

```c
// Add skip tracking:
int last_combined_col = -1;

// Before processing each cell:
if (col <= last_combined_col) continue;  // Skip already-combined cells

// When rendering emoji:
if (is_emoji_range) {
    cp_count = combine_cells_for_emoji(term, row, col, cps, MAX_COMBINED);
    last_combined_col = col + cp_count - 1;
}
```

That's it. No libvterm changes, no external dependencies.

### 4. **Consistency with Other Rendering Concerns**

Renderer already handles multi-cell semantics:

| Feature          | Cell Storage           | Renderer Action              |
| ---------------- | ---------------------- | ---------------------------- |
| Wide chars (CJK) | Cell 0: 好, Cell 1: -1 | Skip placeholder cells       |
| **Emoji seqs**   | **Separate cells**     | **Combine before shaping**   |
| Ligatures (fi)   | Cell 0: f, Cell 1: i   | Shape together (HarfBuzz)    |
| BiDi text        | LTR cell order         | Reorder for RTL presentation |

**Emoji combining is the same pattern** as existing code.

## Recommendation

**Fix at renderer level** by:

1. Calling existing `combine_cells_for_emoji()` function
2. Adding skip tracking to avoid double-rendering
3. ~25 line change in `src/renderer.c`

This is:

- ✅ Industry best practice
- ✅ Minimal code change
- ✅ Proper separation of concerns
- ✅ No dependency changes needed
- ✅ Matches how we handle other multi-cell semantics

## References

Updated detailed design in: **`GlyphRenderingProposal.md`**

- Section: "Architectural Decision: Where to Fix Emoji Sequences?"
- Section: "Multi-Codepoint Emoji Fix Design"

---

**Document Version:** 1.0  
**Date:** 2026-01-23  
**Author:** Research and design phase
