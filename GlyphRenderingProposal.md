# Unified Glyph Rendering Redesign: FreeType + HarfBuzz + SDL3

## Executive Summary

This document outlines a complete redesign of the glyph rendering system to eliminate Cairo dependency and leverage FreeType, HarfBuzz, and SDL3 for high-performance, feature-rich text rendering with proper support for:

- Variable font axis control (Weight, Width, Slant, etc.)
- Complex text shaping (ligatures, combining marks, emoji sequences)
- Correct kerning and positioning for all font variations
- Efficient GPU texture uploading via SDL3

## System Versions

**Installed Library Versions:**
- FreeType: 26.2.20
- HarfBuzz: 11.5.1
- SDL3: 3.2.24

## Current Architecture Analysis

### Current Implementation
1. **Font Backend** (`font_ft.c`): Uses FreeType + Cairo
   - FreeType for font loading and variable font support
   - Cairo for glyph rasterization (both COLR color fonts and regular fonts)
   - Variable font support via `FT_Set_Var_Design_Coordinates()`
   - Single codepoint rendering only

2. **Rendering Pipeline** (`renderer.c`):
   - Renders one codepoint at a time
   - Creates SDL_Texture from Cairo-rendered glyph bitmap
   - No text shaping (no ligatures, combining marks, etc.)

### Limitations
- **Cairo Dependency**: Heavy dependency for simple rasterization
- **No Complex Shaping**: Cannot handle multi-codepoint sequences properly
- **No Kerning**: HarfBuzz shaping needed for proper glyph positioning
- **Performance**: Multiple library overhead (FreeType → Cairo → SDL)
- **Missing Features**: Ligatures, emoji sequences, combining marks not properly rendered

## Proposed Architecture

### Overview

```
┌─────────────────┐
│   Terminal      │
│   (libvterm)    │
└────────┬────────┘
         │ Codepoint sequences
         ▼
┌─────────────────────────────────────────────────┐
│         Font Rendering Layer                    │
│  ┌──────────────┐  ┌──────────────┐            │
│  │  FreeType    │  │  HarfBuzz    │            │
│  │  - Load font │  │  - Shape text│            │
│  │  - Variations│  │  - Positioning│            │
│  │  - Rasterize │  │  - Ligatures │            │
│  └──────┬───────┘  └──────┬───────┘            │
│         │ Glyph bitmaps   │ Glyph positions    │
│         └─────────┬────────┘                    │
│                   ▼                             │
│         ┌───────────────────┐                   │
│         │  Texture Manager  │                   │
│         │  - SDL Surface    │                   │
│         │  - SDL Texture    │                   │
│         └─────────┬─────────┘                   │
└───────────────────┼─────────────────────────────┘
                    │
                    ▼
         ┌─────────────────┐
         │   SDL3 Renderer │
         │   (GPU/Texture) │
         └─────────────────┘
```

### Component Redesign

## 1. FreeType Integration (Variable Fonts + Rasterization)

### 1.1 Variable Font Axis Management

**Font Data Structure** (`font_ft.c`):
```c
typedef struct {
    FT_Face ft_face;
    hb_font_t *hb_font;           // NEW: HarfBuzz font wrapper
    float font_size;
    FontStyle style;
    
    // Variable font information
    FT_MM_Var *mm_var;             // NEW: Cache MM_Var for performance
    int num_axes;
    struct {
        FT_ULong tag;              // e.g., 'wght', 'wdth'
        char name[64];
        float min_value;
        float default_value;
        float max_value;
        float current_value;       // Current setting
    } *axes;                       // Dynamic array of axis info
    
    // Rendering options
    bool antialias;
    int hinting;
    int hint_style;
    int ft_load_flags;
    int dpi_x, dpi_y;
    
    // COLR support
    bool has_colr;
    FT_Color *palette;             // NEW: Use FreeType's COLR API
    FT_UShort palette_size;
} FtFontData;
```

**API: Set Variable Font Coordinates**
```c
// Set specific axis value (e.g., Weight=600)
bool ft_set_axis_value(FtFontData *ft_data, const char *axis_tag, float value);

// Set all axes at once
bool ft_set_all_axes(FtFontData *ft_data, float *coords, int num_coords);
```

**Implementation Details:**
1. **Axis Discovery**: At font load time, call `FT_Get_MM_Var()` and cache axis information
2. **Coordinate Setting**: 
   - Use `FT_Set_Var_Design_Coordinates()` to set FreeType axis values
   - Immediately call `hb_ft_font_changed()` to sync HarfBuzz with FreeType changes
3. **Common Axes**:
   - `wght` (Weight): 100-900 (Thin to Black)
   - `wdth` (Width): 50-200 (Condensed to Expanded)
   - `slnt` (Slant): -15 to 0 (Italic angle)
   - `ital` (Italic): 0-1 (Boolean italic switch)

**Example Usage:**
```c
// For bold style
ft_set_axis_value(ft_data, "wght", 700.0f);

// For condensed bold
float coords[2] = {700.0f, 75.0f}; // wght=700, wdth=75
ft_set_all_axes(ft_data, coords, 2);
```

### 1.2 Color Font Support (COLR v0/v1)

**Remove Cairo, use FreeType native COLR:**

```c
// Check and load COLR palette
static bool load_colr_palette(FtFontData *ft_data) {
    FT_UInt num_palettes = 0;
    FT_Error error = FT_Palette_Data_Get(ft_data->ft_face, NULL);
    if (error) return false;
    
    // Get default palette (index 0)
    FT_Palette_Data palette_data;
    FT_Palette_Data_Get(ft_data->ft_face, &palette_data);
    
    FT_Color *palette = malloc(palette_data.num_palette_entries * sizeof(FT_Color));
    error = FT_Palette_Select(ft_data->ft_face, 0, &palette);
    
    if (error == 0) {
        ft_data->palette = palette;
        ft_data->palette_size = palette_data.num_palette_entries;
        ft_data->has_colr = true;
        return true;
    }
    return false;
}

// Render COLR glyph layers
static GlyphBitmap *render_colr_glyph(FtFontData *ft_data, FT_UInt glyph_index) {
    // Use FT_Get_Color_Glyph_Layer() to iterate layers
    // Composite each layer with its palette color
    // Return final RGBA bitmap
}
```

### 1.3 Glyph Rasterization

**Implementation** (replaces Cairo entirely):
```c
// Render single glyph to RGBA bitmap
static GlyphBitmap *rasterize_glyph(FtFontData *ft_data, FT_UInt glyph_index,
                                     uint8_t fg_r, uint8_t fg_g, uint8_t fg_b) {
    FT_Load_Glyph(ft_data->ft_face, glyph_index, ft_data->ft_load_flags);
    
    // Handle COLR glyphs specially
    if (ft_data->has_colr && FT_HAS_COLOR(ft_data->ft_face)) {
        return render_colr_glyph(ft_data, glyph_index);
    }
    
    // Regular rasterization
    FT_Render_Glyph(ft_data->ft_face->glyph, 
                    ft_data->antialias ? FT_RENDER_MODE_NORMAL : FT_RENDER_MODE_MONO);
    
    FT_Bitmap *bitmap = &ft_data->ft_face->glyph->bitmap;
    
    // Convert to RGBA (existing code, works fine)
    GlyphBitmap *result = malloc(sizeof(GlyphBitmap));
    result->width = bitmap->width;
    result->height = bitmap->rows;
    result->x_offset = ft_data->ft_face->glyph->bitmap_left;
    result->y_offset = ft_data->ft_face->glyph->bitmap_top;
    result->advance = ft_data->ft_face->glyph->advance.x >> 6;
    
    // Allocate RGBA pixels and convert (grayscale/mono to RGBA)
    result->pixels = malloc(result->width * result->height * 4);
    // ... conversion code (existing code works)
    
    return result;
}
```

## 2. HarfBuzz Integration (Text Shaping)

### 2.1 Core Shaping Pipeline

**New Structure: Shaped Glyph Run**
```c
typedef struct {
    int num_glyphs;                  // Number of glyphs after shaping
    FT_UInt *glyph_indices;          // Glyph IDs (not codepoints!)
    int32_t *x_positions;            // X position deltas (26.6 fixed point)
    int32_t *y_positions;            // Y position deltas (26.6 fixed point)
    int32_t *x_advances;             // X advance widths (26.6 fixed point)
    int32_t *y_advances;             // Y advance widths (26.6 fixed point)
    uint32_t *clusters;              // Cluster mapping back to input codepoints
} ShapedGlyphRun;
```

**Shaping Function:**
```c
ShapedGlyphRun *shape_text(FtFontData *ft_data, 
                           uint32_t *codepoints, 
                           int codepoint_count,
                           hb_direction_t direction) {
    // Create HarfBuzz buffer
    hb_buffer_t *buf = hb_buffer_create();
    hb_buffer_set_direction(buf, direction);  // HB_DIRECTION_LTR, HB_DIRECTION_RTL
    hb_buffer_set_script(buf, HB_SCRIPT_LATIN);  // Auto-detect in production
    hb_buffer_set_language(buf, hb_language_from_string("en", -1));
    
    // Add codepoints to buffer
    for (int i = 0; i < codepoint_count; i++) {
        hb_buffer_add(buf, codepoints[i], i);
    }
    hb_buffer_guess_segment_properties(buf);
    
    // Shape the text
    hb_shape(ft_data->hb_font, buf, NULL, 0);
    
    // Extract glyph information
    unsigned int glyph_count;
    hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(buf, &glyph_count);
    hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(buf, &glyph_count);
    
    // Allocate result
    ShapedGlyphRun *run = malloc(sizeof(ShapedGlyphRun));
    run->num_glyphs = glyph_count;
    run->glyph_indices = malloc(glyph_count * sizeof(FT_UInt));
    run->x_positions = malloc(glyph_count * sizeof(int32_t));
    run->y_positions = malloc(glyph_count * sizeof(int32_t));
    run->x_advances = malloc(glyph_count * sizeof(int32_t));
    run->y_advances = malloc(glyph_count * sizeof(int32_t));
    run->clusters = malloc(glyph_count * sizeof(uint32_t));
    
    // Copy shaped data
    int32_t cursor_x = 0, cursor_y = 0;
    for (unsigned int i = 0; i < glyph_count; i++) {
        run->glyph_indices[i] = glyph_info[i].codepoint;  // Now a glyph ID!
        run->x_positions[i] = cursor_x + glyph_pos[i].x_offset;
        run->y_positions[i] = cursor_y + glyph_pos[i].y_offset;
        run->x_advances[i] = glyph_pos[i].x_advance;
        run->y_advances[i] = glyph_pos[i].y_advance;
        run->clusters[i] = glyph_info[i].cluster;
        
        cursor_x += glyph_pos[i].x_advance;
        cursor_y += glyph_pos[i].y_advance;
    }
    
    hb_buffer_destroy(buf);
    return run;
}
```

### 2.2 Font Instance Setup

**Initialize HarfBuzz Font from FreeType:**
```c
static hb_font_t *create_hb_font(FT_Face ft_face) {
    // Create HarfBuzz font from FreeType face
    hb_font_t *hb_font = hb_ft_font_create_referenced(ft_face);
    
    // HarfBuzz will automatically use FreeType's rendering
    return hb_font;
}

// When variable font coords change:
static void sync_variations_to_harfbuzz(FtFontData *ft_data) {
    // After calling FT_Set_Var_Design_Coordinates(), call:
    hb_ft_font_changed(ft_data->hb_font);
    
    // Or manually set coords in HarfBuzz:
    // float coords[N] = {weight, width, ...};
    // hb_font_set_var_coords_design(ft_data->hb_font, coords, N);
}
```

### 2.3 Example Shaping Scenarios

**Ligatures:**
```
Input:  ['f', 'i']           (2 codepoints)
Output: [glyph_id=312]       (1 glyph - "fi" ligature)
```

**Emoji Sequence:**
```
Input:  [U+1F1FA, U+1F1F8]   (US flag: 🇺🇸)
Output: [glyph_id=1523]      (1 glyph - flag emoji)
```

**Combining Marks:**
```
Input:  ['a', U+0301]        (a + combining acute accent)
Output: [glyph_id=65, glyph_id=456 with y_offset=-200]
```

## 3. SDL3 Integration

### 3.1 Texture Upload Strategy

**Option A: Traditional SDL_Renderer (RECOMMENDED for terminal)**
```c
static SDL_Texture *upload_glyph_to_texture(Renderer *rend, GlyphBitmap *bitmap) {
    // Create SDL surface from RGBA bitmap
    SDL_Surface *surface = SDL_CreateSurface(bitmap->width, bitmap->height, 
                                             SDL_PIXELFORMAT_RGBA32);
    memcpy(surface->pixels, bitmap->pixels, bitmap->width * bitmap->height * 4);
    
    // Create texture from surface
    SDL_Texture *texture = SDL_CreateTextureFromSurface(rend->renderer, surface);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    
    SDL_DestroySurface(surface);
    return texture;
}
```

**Option B: SDL3 GPU API (for future optimization)**
```c
// For advanced use: Direct GPU texture upload
SDL_GPUTexture *upload_to_gpu(SDL_GPUDevice *device, GlyphBitmap *bitmap) {
    SDL_GPUTextureCreateInfo create_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = bitmap->width,
        .height = bitmap->height,
        .layer_count_or_depth = 1,
        .num_levels = 1
    };
    
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &create_info);
    
    // Upload pixel data
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, ...);
    // ... map, copy pixels, unmap
    SDL_UploadToGPUTexture(command_buffer, transfer, texture, ...);
    
    return texture;
}
```

**Recommendation**: Start with Option A (SDL_Renderer + SDL_Texture) for simplicity and compatibility. Terminal emulators don't typically need GPU API-level control.

### 3.2 Rendering Pipeline

```c
void renderer_draw_shaped_text(Renderer *rend, ShapedGlyphRun *run,
                                FtFontData *ft_data,
                                int base_x, int base_y,
                                uint8_t r, uint8_t g, uint8_t b) {
    int pen_x = base_x;
    int pen_y = base_y;
    
    for (int i = 0; i < run->num_glyphs; i++) {
        // Rasterize glyph
        GlyphBitmap *bitmap = rasterize_glyph(ft_data, run->glyph_indices[i], r, g, b);
        
        // Calculate position (convert from 26.6 fixed point)
        int x = pen_x + (run->x_positions[i] >> 6) + bitmap->x_offset;
        int y = pen_y - (run->y_positions[i] >> 6) - bitmap->y_offset;
        
        // Create and render texture
        SDL_Texture *tex = upload_glyph_to_texture(rend, bitmap);
        SDL_FRect dst = {x, y, bitmap->width, bitmap->height};
        SDL_RenderTexture(rend->renderer, tex, NULL, &dst);
        
        SDL_DestroyTexture(tex);
        free_glyph_bitmap(bitmap);
        
        // Advance pen (convert from 26.6 fixed point)
        pen_x += (run->x_advances[i] >> 6);
        pen_y += (run->y_advances[i] >> 6);
    }
}
```

## 4. Updated Font Interface API

### 4.1 Modified `font.h`

```c
// NEW: Shaped glyph output structure
typedef struct ShapedGlyphs {
    int num_glyphs;
    GlyphBitmap **bitmaps;        // Array of rasterized glyphs
    int *x_positions;              // Pixel positions
    int *y_positions;
    int *x_advances;
    int total_advance;             // Total width of shaped run
} ShapedGlyphs;

// Enhanced Font interface
struct Font {
    // ... existing fields ...
    
    // NEW: Multi-codepoint shaping and rendering
    ShapedGlyphs *(*render_shaped)(Font *font, void *font_data,
                                   uint32_t *codepoints, int codepoint_count,
                                   uint8_t fg_r, uint8_t fg_g, uint8_t fg_b);
    
    // NEW: Variable font axis control
    bool (*set_variation_axis)(Font *font, void *font_data, 
                               const char *axis_tag, float value);
    
    // Keep existing for backward compat
    GlyphBitmap *(*render_glyphs)(Font *font, void *font_data,
                                  uint32_t *codepoints, int codepoint_count,
                                  uint8_t fg_r, uint8_t fg_g, uint8_t fg_b);
};

// Public API
ShapedGlyphs *font_render_shaped_text(Font *font, FontStyle style,
                                       uint32_t *codepoints, int count,
                                       uint8_t r, uint8_t g, uint8_t b);
```

## 5. Migration Plan

### Phase 1: Foundation (Week 1-2)
**Goal:** Replace Cairo with pure FreeType rasterization

1. **Remove Cairo dependency:**
   - Delete all `#include <cairo/*.h>`
   - Remove Cairo code from `font_ft.c` (lines 11-12, 499-726)
   - Update build system (`configure.ac`, `Makefile.am`) to remove cairo deps

2. **Implement FreeType-only rasterization:**
   - Keep existing `rasterize_glyph()` function (lines 359-497) - it already works without Cairo
   - Test with regular monochrome fonts

3. **COLR support via FreeType native APIs:**
   - Implement `load_colr_palette()`
   - Implement `render_colr_glyph()` using `FT_Get_Color_Glyph_Layer()`
   - Test with color emoji fonts (Noto Color Emoji, etc.)

**Deliverable:** Cairo-free build that renders text correctly

### Phase 2: HarfBuzz Integration (Week 3-4)
**Goal:** Add text shaping support

1. **Add HarfBuzz build dependency:**
   - Update `configure.ac`: `PKG_CHECK_MODULES([HARFBUZZ], [harfbuzz >= 2.0])`
   - Update `Makefile.am` to include HarfBuzz CFLAGS/LIBS

2. **Implement shaping pipeline:**
   - Add `hb_font_t` to `FtFontData` structure
   - Implement `create_hb_font()`
   - Implement `shape_text()` function
   - Implement `sync_variations_to_harfbuzz()`

3. **Update variable font handling:**
   - Modify `apply_font_variations()` to call `hb_ft_font_changed()` after setting coords
   - Add axis caching for performance

4. **Create `render_shaped()` backend function:**
   - Shape codepoints with HarfBuzz
   - Rasterize each glyph with FreeType
   - Return `ShapedGlyphs` structure with positioned bitmaps

**Deliverable:** Ligatures, combining marks, emoji sequences work correctly

### Phase 3: Renderer Integration (Week 5)
**Goal:** Wire shaped text rendering into display pipeline

1. **Modify `renderer.c`:**
   - Add `renderer_draw_shaped_text()` function
   - Detect multi-codepoint sequences from terminal
   - Call shaped rendering for complex text
   - Fall back to simple rendering for ASCII

2. **Optimize texture management:**
   - Consider glyph caching (LRU cache)
   - Batch texture uploads where possible

**Deliverable:** Terminal displays ligatures and complex text correctly

### Phase 4: Testing & Optimization (Week 6)
**Goal:** Verify correctness and performance

1. **Test cases:**
   - ASCII text (basic sanity)
   - Ligatures: "fi", "fl", "ff", "ffi" in appropriate fonts
   - Emoji sequences: 🇺🇸 (flags), 👨‍👩‍👧‍👦 (family with ZWJ)
   - Combining marks: à, é, ñ (a+grave, e+acute, n+tilde)
   - Variable fonts: Bold vs Normal weight rendering
   - RTL text: Arabic, Hebrew (if needed)

2. **Performance optimization:**
   - Profile glyph rasterization vs shaping
   - Implement glyph cache if needed
   - Benchmark vs old Cairo implementation

3. **Documentation:**
   - Update AGENTS.md with new architecture
   - Document variable font axis support
   - Add examples for complex text

**Deliverable:** Production-ready implementation

## 6. Build System Changes

### 6.1 configure.ac

```bash
# Remove Cairo
# PKG_CHECK_MODULES([CAIRO], [cairo >= 1.10])
# PKG_CHECK_MODULES([CAIRO_FT], [cairo-ft >= 1.10])

# Add HarfBuzz (if not already present)
PKG_CHECK_MODULES([HARFBUZZ], [harfbuzz >= 2.0])

# Keep FreeType
PKG_CHECK_MODULES([FREETYPE], [freetype2 >= 2.10])
```

### 6.2 Makefile.am

```makefile
# Update CFLAGS and LIBS
AM_CFLAGS = $(SDL3_CFLAGS) $(FREETYPE_CFLAGS) $(HARFBUZZ_CFLAGS) \
            $(VTERM_CFLAGS) $(FONTCONFIG_CFLAGS)

LIBS = $(SDL3_LIBS) $(FREETYPE_LIBS) $(HARFBUZZ_LIBS) \
       $(VTERM_LIBS) $(FONTCONFIG_LIBS)
```

## 7. API Reference Summary

### FreeType APIs Used
```c
FT_Get_MM_Var()                      // Get variable font axes
FT_Set_Var_Design_Coordinates()      // Set axis values (e.g., Weight=600)
FT_Load_Glyph()                      // Load glyph by index
FT_Render_Glyph()                    // Rasterize to bitmap
FT_Palette_Select()                  // Load COLR palette
FT_Get_Color_Glyph_Layer()           // Iterate COLR layers
```

### HarfBuzz APIs Used
```c
hb_ft_font_create_referenced()       // Create HB font from FT_Face
hb_font_set_var_coords_design()      // Sync variable font coords
hb_ft_font_changed()                 // Notify font changed
hb_buffer_create()                   // Create text buffer
hb_buffer_add()                      // Add codepoints
hb_shape()                           // Shape text
hb_buffer_get_glyph_infos()          // Get glyph IDs
hb_buffer_get_glyph_positions()      // Get positions and advances
```

### SDL3 APIs Used
```c
SDL_CreateSurface()                  // Create RGBA surface
SDL_CreateTextureFromSurface()       // Upload to GPU
SDL_SetTextureBlendMode()            // Enable alpha blending
SDL_RenderTexture()                  // Draw textured quad
SDL_DestroyTexture()                 // Free GPU resource
```

## 8. Benefits of New Architecture

### Features
- ✅ **Variable Font Support**: Full control via FT_Set_Var_Design_Coordinates + hb_font_set_var_coords_design
- ✅ **Complex Shaping**: Ligatures, emoji, combining marks via HarfBuzz
- ✅ **Correct Kerning**: HarfBuzz provides proper glyph positioning
- ✅ **Color Emoji**: FreeType native COLR support (no Cairo needed)
- ✅ **Simpler Stack**: FreeType → SDL (no Cairo middleman)

### Performance
- Fewer library dependencies (remove Cairo)
- Direct bitmap upload to SDL textures
- Potential for glyph caching at shaped-run level

### Maintainability
- Industry-standard text stack (FT + HB)
- Better alignment with modern font technologies
- Clearer separation: FT=raster, HB=shape, SDL=display

## 9. Backward Compatibility

- Single-codepoint rendering still works (`render_glyphs` with count=1)
- Falls back to simple rendering for ASCII/Latin text
- Shaped rendering opt-in for complex sequences
- All existing font loading code preserved

## 10. Future Enhancements

1. **Glyph Cache**: LRU cache for frequently used shaped runs
2. **BiDi Support**: Use HarfBuzz's bidirectional text support
3. **Font Fallback Chain**: Multiple fonts for Unicode coverage
4. **Subpixel Rendering**: FreeType LCD filtering + SDL alpha blending
5. **GPU Performance**: Migrate to SDL3 GPU API for even faster uploads

---

## Decisions Made

Based on the recommendations in Section 9, the following decisions have been finalized:

1. **GPU API**: Use SDL_Renderer (simple) for initial implementation
   - Migration to SDL3 GPU API documented in FUTURE_ENHANCEMENTS.md

2. **COLR Migration**: Migrate to FreeType native (consistent stack)
   - Remove Cairo dependency completely
   - Use FT_Palette_* and FT_Get_Color_Glyph_Layer APIs

3. **Variable Font Axes**: Start with Weight (wght) and Width (wdth)
   - Additional axes (slnt, ital, opsz, GRAD) documented in FUTURE_ENHANCEMENTS.md

4. **Text Direction**: LTR only initially
   - Full BiDi support documented in FUTURE_ENHANCEMENTS.md

5. **Caching Strategy**: Render on-demand initially
   - LRU glyph caching documented in FUTURE_ENHANCEMENTS.md for Phase 4 optimization

See `FUTURE_ENHANCEMENTS.md` for detailed designs of deferred features.

---

**Document Version:** 2.1  
**Date:** 2026-01-22  
**Status:** Design Complete - Ready for Implementation
