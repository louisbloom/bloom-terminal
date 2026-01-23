# Emoji Detection Heuristics

## Document Purpose

This document defines precise detection rules for determining when emoji combining should be attempted, and refines the helper functions for accurate detection.

## Problem Statement

Current emoji detection functions are too broad:

```c
static bool is_emoji_presentation(uint32_t cp) {
    return (cp >= 0x2000 && cp <= 0x27FF) ||  // TOO BROAD!
           (cp >= 0x1F000 && cp <= 0x1F9FF) ||
           // ...
}
```

Issues:

1. **0x2000-0x27FF includes many non-emoji**: Punctuation, arrows, math symbols
2. **Missing ranges**: Emoji exist outside checked ranges
3. **No variation selector check**: ☺ vs ☺️ (text vs emoji presentation)

## Unicode Emoji Properties

### Official Emoji Ranges (Unicode 15.0)

From Unicode Technical Standard #51:

```
Emoji Characters:
- U+203C..U+3299      (selected characters, not all)
- U+1F000..U+1F02B    (Mahjong Tiles, Domino Tiles - NOT emoji presentation)
- U+1F0A0..U+1F0F5    (Playing Cards - NOT emoji presentation)
- U+1F100..U+1F1FF    (Enclosed Alphanumeric Supplement + Regional Indicators)
- U+1F200..U+1F2FF    (Enclosed Ideographic Supplement)
- U+1F300..U+1F5FF    (Miscellaneous Symbols and Pictographs) ✓ MAIN RANGE
- U+1F600..U+1F64F    (Emoticons) ✓ MAIN RANGE
- U+1F680..U+1F6FF    (Transport and Map Symbols) ✓ MAIN RANGE
- U+1F700..U+1F77F    (Alchemical Symbols - NOT emoji)
- U+1F780..U+1F7FF    (Geometric Shapes Extended)
- U+1F800..U+1F8FF    (Supplemental Arrows-C)
- U+1F900..U+1F9FF    (Supplemental Symbols and Pictographs) ✓ MAIN RANGE
- U+1FA00..U+1FA6F    (Chess Symbols, extended)
- U+1FA70..U+1FAFF    (Symbols and Pictographs Extended-A) ✓ MAIN RANGE
```

### Emoji Components

#### Variation Selectors

- U+FE0E: Text presentation selector (☺︎)
- U+FE0F: Emoji presentation selector (☺️)

#### Modifiers

- U+1F3FB..U+1F3FF: Skin tone modifiers (🏻🏼🏽🏾🏿)

#### Combiners

- U+200D: Zero Width Joiner (ZWJ)
- U+20E3: Combining Enclosing Keycap (for keycap emoji)

#### Regional Indicators

- U+1F1E6..U+1F1FF: Regional indicator symbols (🇦-🇿)

## Detection Function Specifications

### Core Emoji Ranges

```c
static bool is_emoji_base_range(uint32_t cp) {
    // Core emoji ranges that typically have emoji presentation
    return (cp >= 0x1F300 && cp <= 0x1F5FF) ||  // Misc Symbols and Pictographs
           (cp >= 0x1F600 && cp <= 0x1F64F) ||  // Emoticons
           (cp >= 0x1F680 && cp <= 0x1F6FF) ||  // Transport and Map
           (cp >= 0x1F900 && cp <= 0x1F9FF) ||  // Supplemental Symbols
           (cp >= 0x1FA70 && cp <= 0x1FAFF);    // Extended-A
}
```

### Mixed Presentation Characters

Some characters default to text but can be emoji with U+FE0F:

```c
static bool is_ambiguous_emoji(uint32_t cp) {
    // Characters that need U+FE0F for emoji presentation
    return (cp >= 0x2600 && cp <= 0x27BF) ||    // Misc Symbols (weather, etc.)
           (cp >= 0x2300 && cp <= 0x23FF) ||    // Misc Technical (keyboard symbols)
           (cp == 0x2B50) ||                     // ⭐ white star
           (cp >= 0x2194 && cp <= 0x21AA) ||    // Arrows
           (cp >= 0x2139 && cp <= 0x2199) ||    // Info, arrows
           (cp >= 0x3030 && cp <= 0x303D);      // Wavy dash, etc.
}
```

### Component Detection (Precise)

```c
static bool is_skin_tone_modifier(uint32_t cp) {
    // Emoji Modifier Fitzpatrick Scale (exact range)
    return (cp >= 0x1F3FB && cp <= 0x1F3FF);
}

static bool is_regional_indicator(uint32_t cp) {
    // Regional Indicator Symbols A-Z
    return (cp >= 0x1F1E6 && cp <= 0x1F1FF);
}

static bool is_zwj(uint32_t cp) {
    // Zero Width Joiner (exact codepoint)
    return (cp == 0x200D);
}

static bool is_variation_selector(uint32_t cp) {
    // Variation Selectors for emoji
    return (cp == 0xFE0E || cp == 0xFE0F);
}

static bool is_emoji_keycap_base(uint32_t cp) {
    // Digits 0-9, #, * that can be keycap emoji with U+FE0F U+20E3
    return (cp >= 0x0030 && cp <= 0x0039) ||  // 0-9
           (cp == 0x0023) ||                   // #
           (cp == 0x002A);                     // *
}
```

### Master Detection Function

```c
/**
 * Check if codepoint should trigger emoji combining logic
 *
 * This checks if the codepoint is:
 * - An emoji base character
 * - A regional indicator (for flags)
 * - An ambiguous character that might be part of emoji sequence
 *
 * Note: Skin tone modifiers and ZWJ are handled during lookahead,
 * not as initial triggers.
 */
static bool should_try_emoji_combining(uint32_t cp) {
    // Core emoji ranges
    if (is_emoji_base_range(cp)) {
        return true;
    }

    // Regional indicators (flags)
    if (is_regional_indicator(cp)) {
        return true;
    }

    // Ambiguous emoji (might need variation selector check)
    if (is_ambiguous_emoji(cp)) {
        return true;
    }

    // Keycap base (0-9, #, *)
    if (is_emoji_keycap_base(cp)) {
        return true;
    }

    return false;
}
```

## Combining Decision Logic

### When to Combine Next Cell

```c
/**
 * Given current sequence state, should we combine the next cell?
 *
 * @param current_cps  Array of codepoints accumulated so far
 * @param cp_count     Number of codepoints in current_cps
 * @param next_first   First codepoint of next cell
 *
 * @return true if next cell should be combined
 */
static bool should_combine_next_cell(uint32_t *current_cps, int cp_count, uint32_t next_first) {
    if (cp_count == 0) {
        return false;  // No base yet
    }

    // Check if last codepoint of current sequence is ZWJ
    bool has_zwj = (cp_count > 0 && is_zwj(current_cps[cp_count - 1]));

    // Case 1: ZWJ followed by emoji base
    if (has_zwj && (is_emoji_base_range(next_first) || is_ambiguous_emoji(next_first))) {
        return true;  // 👨‍ + 👩 (man ZWJ woman)
    }

    // Case 2: Emoji base followed by skin tone modifier
    if (is_skin_tone_modifier(next_first)) {
        // Check if first codepoint is emoji that accepts skin tones
        uint32_t base = current_cps[0];
        if (is_emoji_base_range(base)) {
            return true;  // 👋 + 🏻 (hand + light skin)
        }
    }

    // Case 3: Regional Indicator followed by another Regional Indicator
    if (is_regional_indicator(current_cps[0]) && is_regional_indicator(next_first)) {
        // Only combine first two RIs (flag emoji)
        return (cp_count <= 2);  // Prevent 3+ RIs from combining
    }

    // Case 4: Keycap sequence (digit + FE0F + 20E3)
    if (is_emoji_keycap_base(current_cps[0])) {
        if (is_variation_selector(next_first) || next_first == 0x20E3) {
            return true;
        }
    }

    // Case 5: Variation selector continuation
    if (cp_count == 2 && is_variation_selector(current_cps[1])) {
        // Might be part of longer sequence
        if (next_first == 0x20E3 && is_emoji_keycap_base(current_cps[0])) {
            return true;  // digit + FE0F + 20E3
        }
    }

    return false;
}
```

## Special Sequence Patterns

### Pattern 1: Keycap Emoji

```
Structure: BASE + U+FE0F + U+20E3
Example: 1️⃣ = U+0031 + U+FE0F + U+20E3

Detection:
1. is_emoji_keycap_base(0x0031) → try combining
2. Next cell has U+FE0F → combine
3. Next cell has U+20E3 → combine
Result: [U+0031, U+FE0F, U+20E3] → single glyph
```

### Pattern 2: Flag Emoji

```
Structure: RI + RI
Example: 🇺🇸 = U+1F1FA + U+1F1F8

Detection:
1. is_regional_indicator(U+1F1FA) → try combining
2. Next cell: is_regional_indicator(U+1F1F8) → combine
3. Stop after 2 RIs
Result: [U+1F1FA, U+1F1F8] → flag glyph
```

### Pattern 3: Skin Tone

```
Structure: BASE + MODIFIER
Example: 👋🏻 = U+1F44B + U+1F3FB

Detection:
1. is_emoji_base_range(U+1F44B) → try combining
2. Next cell: is_skin_tone_modifier(U+1F3FB) → combine
Result: [U+1F44B, U+1F3FB] → single glyph with skin tone
```

### Pattern 4: ZWJ Sequence

```
Structure: BASE + ZWJ + BASE + ZWJ + ...
Example: 👨‍👩‍👧 = U+1F468 + U+200D + U+1F469 + U+200D + U+1F467

Detection:
1. is_emoji_base_range(U+1F468) → try combining
2. Current has ZWJ at end → look ahead
3. Next cell: is_emoji_base_range(U+1F469) → combine
4. Combined has ZWJ at end → continue
5. Next cell: is_emoji_base_range(U+1F467) → combine
Result: [man, ZWJ, woman, ZWJ, girl] → family glyph
```

## False Positive Prevention

### Characters That Look Like Emoji But Aren't

```c
// These should NOT trigger combining:
- U+1F000..U+1F02B  (Mahjong, Domino - rendered as symbols, not emoji)
- U+1F0A0..U+1F0F5  (Playing Cards - may render as text)
- U+1F700..U+1F77F  (Alchemical Symbols - definitely not emoji)

// Solution: is_emoji_base_range() excludes these
```

### Preventing Over-Combining

```c
// Don't combine more than reasonable sequence length
#define MAX_EMOJI_SEQUENCE_LENGTH 16

// In combining logic:
if (cp_count >= MAX_EMOJI_SEQUENCE_LENGTH) {
    break;  // Prevent runaway combining
}
```

## Testing Matrix

| Input | First CP | Trigger? | Combine? | Expected        |
| ----- | -------- | -------- | -------- | --------------- |
| 😀    | U+1F600  | Yes      | No       | Single emoji    |
| 👋🏻    | U+1F44B  | Yes      | Yes      | Hand + skin     |
| 🇺🇸    | U+1F1FA  | Yes      | Yes      | Flag            |
| ❤️    | U+2764   | Yes      | Maybe    | Depends on FE0F |
| →     | U+2192   | No       | No       | Text arrow      |
| 🀄    | U+1F004  | No       | No       | Mahjong tile    |
| 1️⃣    | U+0031   | Yes      | Yes      | Keycap          |
| 👨‍👩‍👧    | U+1F468  | Yes      | Yes      | Family          |

---

**Document Status**: Ready for implementation
**Last Updated**: 2025-01-23
**Next Document**: 05-implementation-checklist.md
