# COLR Rendering Issues (Vehicle Emoji and Complex Gradients)

## Document Purpose

This document analyzes the remaining COLR v1 rendering issues observed with vehicle emoji (🚗🚕🚙) and other complex color emoji that render as gray/white boxes.

## Problem Observation

From screenshot and testing:

- Simple emoji (faces, food, basic symbols): Render correctly ✅
- Some vehicle emoji (🚗🚕🚙): Render as gray or white boxes ❌
- Some complex emoji with gradients: May have rendering issues ❌

## Scope Separation

**Important**: This is a SEPARATE issue from emoji combining.

- **Emoji combining issue** (documents 01-05): Codepoints split across cells
  - Affects: Skin tones, flags, ZWJ sequences
  - Impact: Wrong glyphs rendered
- **COLR rendering issue** (this document): Paint graph evaluation problems
  - Affects: Complex emoji with gradients/composites
  - Impact: Gray boxes or incorrect colors

## Root Cause Analysis

### COLR v1 Paint Graph Complexity

Vehicle emoji likely use advanced COLR v1 features:

1. **Radial/sweep gradients**: More complex than linear gradients
2. **Multiple composite layers**: src/dest blending operations
3. **PaintExtend modes**: REPEAT/REFLECT (not just PAD)
4. **Complex transform matrices**: Rotation, skew on gradients

### Current Implementation Status

From `src/font_ft.c`:

**Implemented** ✅:

- PaintSolid
- PaintLinearGradient (with PAD extend only)
- PaintRadialGradient (basic)
- PaintSweepGradient (basic)
- PaintGlyph (mask application)
- PaintColrGlyph (nested glyphs)
- PaintColrLayers (layer compositing)
- PaintComposite (limited modes)
- Affine transforms (translate, scale, rotate, skew)

**Composite modes implemented**:

- SRC_OVER (most common)
- PLUS, MULTIPLY, SCREEN, OVERLAY
- DARKEN, LIGHTEN

**Missing composite modes** ❌:

- COLOR_DODGE, COLOR_BURN
- HARD_LIGHT, SOFT_LIGHT
- DIFFERENCE, EXCLUSION
- HUE, SATURATION, COLOR, LUMINOSITY
- SRC, DEST, SRC_IN, SRC_OUT, DEST_IN, DEST_OUT
- SRC_ATOP, DEST_ATOP, XOR

**PaintExtend modes**:

- PAD (clamp): Implemented ✅
- REPEAT: Not implemented ❌
- REFLECT: Not implemented ❌

### Testing Hypothesis

Vehicle emoji likely fail because:

1. **Use unsupported composite modes** (e.g., COLOR_DODGE for highlights)
2. **Use REPEAT/REFLECT extend** (for repeating patterns)
3. **Complex gradient configurations** that expose evaluation bugs

## Investigation Steps

### Step 1: Verbose Logging for Failed Emoji

Add logging to identify which paint operations are used:

```c
// In paint_colr_paint_recursive()
vlog("PAINT: Processing paint type %d for glyph %d\n", paint.format, glyph_id);

switch (paint.format) {
    case FT_COLR_PAINTFORMAT_COMPOSITE:
        vlog("  COMPOSITE: mode=%d\n", paint.u.composite.composite_mode);
        if (paint.u.composite.composite_mode > FT_COLR_COMPOSITE_LIGHTEN) {
            vlog("  WARNING: Unsupported composite mode %d\n",
                 paint.u.composite.composite_mode);
        }
        break;

    case FT_COLR_PAINTFORMAT_LINEAR_GRADIENT:
    case FT_COLR_PAINTFORMAT_RADIAL_GRADIENT:
    case FT_COLR_PAINTFORMAT_SWEEP_GRADIENT:
        vlog("  GRADIENT: extend=%d\n", /* extract extend mode */);
        if (extend != FT_COLR_PAINT_EXTEND_PAD) {
            vlog("  WARNING: Unsupported extend mode %d\n", extend);
        }
        break;
}
```

### Step 2: Test Specific Vehicle Emoji

```bash
# Test car emoji individually with verbose output
printf "\xf0\x9f\x9a\x97" | ./build/src/vterm-sdl3 -v - 2>&1 | grep -A5 "PAINT:"
```

Compare working emoji (😀) vs broken emoji (🚗):

- Which paint types are used?
- Are there unsupported composite modes?
- Are there non-PAD extend modes?

### Step 3: Fallback Verification

Check if COLR v0 fallback is working:

```c
// In rasterize_glyph_index()
if (!colr_v1_success) {
    vlog("COLR v1 failed for glyph %d, trying v0 fallback\n", glyph_id);
    // Try COLR v0 (layer-based)
    result = render_colr_glyph(...);
    if (result) {
        vlog("COLR v0 fallback succeeded for glyph %d\n", glyph_id);
    } else {
        vlog("COLR v0 also failed, using grayscale for glyph %d\n", glyph_id);
    }
}
```

## Implementation Priority

### P0: Diagnostic Logging (Do First)

Add comprehensive logging to understand which paint operations fail:

- Paint type and parameters
- Composite modes used
- Extend modes used
- Success/failure of each operation

### P1: Implement Missing Composite Modes

Based on FreeType's ftcolor.h (lines 446-479):

```c
// Porter-Duff operators (most important)
case FT_COLR_COMPOSITE_SRC:
case FT_COLR_COMPOSITE_DEST:
case FT_COLR_COMPOSITE_SRC_IN:
case FT_COLR_COMPOSITE_DEST_IN:
case FT_COLR_COMPOSITE_SRC_OUT:
case FT_COLR_COMPOSITE_DEST_OUT:
case FT_COLR_COMPOSITE_SRC_ATOP:
case FT_COLR_COMPOSITE_DEST_ATOP:
case FT_COLR_COMPOSITE_XOR:
    // Implement based on Porter-Duff equations

// Separable blend modes
case FT_COLR_COMPOSITE_COLOR_DODGE:
case FT_COLR_COMPOSITE_COLOR_BURN:
case FT_COLR_COMPOSITE_HARD_LIGHT:
case FT_COLR_COMPOSITE_SOFT_LIGHT:
case FT_COLR_COMPOSITE_DIFFERENCE:
case FT_COLR_COMPOSITE_EXCLUSION:
    // Implement based on W3C compositing spec

// Non-separable blend modes (complex)
case FT_COLR_COMPOSITE_HUE:
case FT_COLR_COMPOSITE_SATURATION:
case FT_COLR_COMPOSITE_COLOR:
case FT_COLR_COMPOSITE_LUMINOSITY:
    // Implement based on HSL color space conversions
```

Reference: https://www.w3.org/TR/compositing-1/

### P2: Implement PaintExtend Modes

```c
// In eval_colorline() - currently only returns color array
// Should also return extend mode

// In gradient evaluation functions:
static void apply_extend_mode(double *t_value, FT_PaintExtend extend) {
    switch (extend) {
        case FT_COLR_PAINT_EXTEND_PAD:
            // Clamp to [0, 1] (already implemented)
            if (*t_value < 0.0) *t_value = 0.0;
            if (*t_value > 1.0) *t_value = 1.0;
            break;

        case FT_COLR_PAINT_EXTEND_REPEAT:
            // Repeat: t_value = fmod(t_value, 1.0)
            *t_value = fmod(*t_value, 1.0);
            if (*t_value < 0.0) *t_value += 1.0;
            break;

        case FT_COLR_PAINT_EXTEND_REFLECT:
            // Reflect: alternate direction each cycle
            *t_value = fmod(fabs(*t_value), 2.0);
            if (*t_value > 1.0) *t_value = 2.0 - *t_value;
            break;
    }
}
```

### P3: Gradient Edge Cases

Review gradient evaluation for edge cases:

- Division by zero in radial gradients
- Degenerate gradient lines (p0 == p1)
- Out-of-gamut colors
- Coordinate overflow

## Testing Strategy

### Test Set 1: Known-Working Emoji

Baseline that should continue to work:

- 😀 (grinning face)
- ❤️ (red heart)
- 🍎 (red apple)

### Test Set 2: Known-Broken Emoji

Should improve after fixes:

- 🚗 (automobile)
- 🚕 (taxi)
- 🚙 (sport utility vehicle)

### Test Set 3: Gradient Test Cases

Create minimal test emoji or inspect:

- Linear gradient with PAD
- Linear gradient with REPEAT
- Radial gradient with REFLECT
- Sweep gradient with complex center

### Test Set 4: Composite Mode Test

Emoji that likely use specific composites:

- Screen mode: Highlights/glows
- Multiply mode: Shadows/darkening
- Color dodge: Bright highlights
- Difference: Special effects

## Acceptance Criteria

### Minimum Success:

1. ✅ Identify which paint operations vehicle emoji use
2. ✅ Log warnings for unsupported features
3. ✅ Graceful fallback to COLR v0 or grayscale
4. ✅ No crashes or rendering corruption

### Full Success:

1. ✅ All composite modes implemented
2. ✅ All extend modes implemented
3. ✅ Vehicle emoji render with correct colors
4. ✅ No gray/white boxes for any emoji

## Alternative Solutions

### Option A: External Rendering

Use FreeType's internal COLR v1 rasterizer (if available):

- Check FreeType version for `FT_Render_Glyph_with_Color` (experimental)
- May not be available in FreeType 2.13

### Option B: GPU Shader Implementation

Move COLR v1 evaluation to fragment shader:

- Upload paint graph as uniform data
- Evaluate gradients/composites in shader
- Much faster, but complex implementation

### Option C: Reference Implementation

Study other implementations:

- HarfBuzz paint API
- Skia's COLR v1 support
- Chrome/Firefox emoji rendering

## Out of Scope

This document does NOT cover:

- ❌ Emoji combining (that's documents 01-05)
- ❌ Font fallback
- ❌ Performance optimization
- ❌ GPU rendering

## Next Actions

1. **Immediate**: Add diagnostic logging (P0)
2. **Test**: Run vehicle emoji with verbose output
3. **Analyze**: Identify missing features from logs
4. **Implement**: Add most critical missing features (likely composite modes)
5. **Verify**: Re-test vehicle emoji

---

**Document Status**: Investigation phase - needs diagnostic data
**Last Updated**: 2025-01-23
**Depends On**: Documents 01-05 (emoji combining) should be fixed first
**Priority**: P1 (high, but after emoji combining)
