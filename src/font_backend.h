#ifndef FONT_BACKEND_H
#define FONT_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

// Font metrics structure
typedef struct FontMetrics
{
    int ascent;  // Distance from baseline to top
    int descent; // Distance from baseline to bottom
    int line_gap;
    int glyph_width;  // Typical glyph advance width
    int glyph_height; // Typical glyph bounding height
    int cell_width;   // Cell width including padding
    int cell_height;  // Cell height including padding
} FontMetrics;

// Font style enumeration
typedef enum FontStyle
{
    FONT_STYLE_NORMAL = 0,
    FONT_STYLE_BOLD,
    FONT_STYLE_EMOJI,
    FONT_STYLE_COUNT
} FontStyle;

// Bitmap structure for glyph rendering
typedef struct GlyphBitmap
{
    uint8_t *pixels; // RGBA format, 4 bytes per pixel
    int width;
    int height;
    int x_offset; // Horizontal offset from cell origin
    int y_offset; // Vertical offset from baseline
    int advance;  // Horizontal advance to next glyph
} GlyphBitmap;

// Font options structure
typedef struct FontOptions
{
    bool antialias;
    int hinting;
    int hint_style;
    int subpixel_order;
    int lcd_filter;
    int ft_load_flags; // FreeType load flags
    int dpi_x;         // Horizontal DPI for HiDPI support
    int dpi_y;         // Vertical DPI for HiDPI support
} FontOptions;

// Abstract font backend interface
struct FontBackend;
typedef struct FontBackend FontBackend;

// Backend interface definition
struct FontBackend
{
    const char *name;

    // Font data and state
    void *font_data[FONT_STYLE_COUNT];     // Backend-specific data per font style
    FontMetrics metrics[FONT_STYLE_COUNT]; // Metrics for each font style
    uint32_t loaded_styles;                // Bitmask of loaded font styles

    // Initialize backend
    bool (*init)(FontBackend *backend);

    // Destroy backend
    void (*destroy)(FontBackend *backend);

    // Initialize backend-specific data for a font style
    void *(*init_font)(FontBackend *backend, const char *font_path,
                       float font_size, FontStyle style, const FontOptions *options);

    // Destroy backend-specific data
    void (*destroy_font)(FontBackend *backend, void *font_data);

    // Get metrics for a font
    bool (*get_metrics)(FontBackend *backend, void *font_data, FontMetrics *metrics);

    // Unified glyph renderer for both single and multiple codepoints
    GlyphBitmap *(*render_glyphs)(FontBackend *backend, void *font_data,
                                  uint32_t *codepoints, int codepoint_count,
                                  uint8_t fg_r, uint8_t fg_g, uint8_t fg_b);

    // Get glyph metrics without rendering
    bool (*get_glyph_info)(FontBackend *backend, void *font_data, uint32_t codepoint,
                           int *advance, int *left_bearing, int *top_bearing);

    // Free a glyph bitmap
    void (*free_glyph_bitmap)(FontBackend *backend, GlyphBitmap *bitmap);

    // Font loading functions
    bool (*load_font)(FontBackend *backend, FontStyle style,
                      const char *font_path, float font_size, const FontOptions *options);

    // Get metrics for a specific style
    const FontMetrics *(*get_style_metrics)(FontBackend *backend, FontStyle style);

    // Check if a style is loaded
    bool (*has_style)(FontBackend *backend, FontStyle style);
};

// Font backend API
bool font_backend_init(FontBackend *backend);
void font_backend_destroy(FontBackend *backend);
bool font_backend_load_font(FontBackend *backend, FontStyle style,
                            const char *font_path, float font_size, const FontOptions *options);
const FontMetrics *font_backend_get_metrics(FontBackend *backend, FontStyle style);
GlyphBitmap *font_backend_render_glyphs(FontBackend *backend, FontStyle style,
                                        uint32_t *codepoints, int codepoint_count,
                                        uint8_t fg_r, uint8_t fg_g, uint8_t fg_b);
bool font_backend_has_style(FontBackend *backend, FontStyle style);

// Extern declaration for the FreeType backend implementation
extern FontBackend ft_backend;

#endif // FONT_BACKEND_H
