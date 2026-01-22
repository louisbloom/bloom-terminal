#include "font_backend.h"
#include "common.h"
#include <string.h>

// Initialize the font backend
bool font_backend_init(FontBackend *backend)
{
    if (!backend) {
        return false;
    }

    backend->loaded_styles = 0;

    // Initialize all font data pointers to NULL
    for (int i = 0; i < FONT_STYLE_COUNT; i++) {
        backend->font_data[i] = NULL;
        memset(&backend->metrics[i], 0, sizeof(FontMetrics));
    }

    return true;
}

// Destroy the font backend
void font_backend_destroy(FontBackend *backend)
{
    if (!backend)
        return;

    // Destroy all loaded font data
    for (FontStyle style = FONT_STYLE_NORMAL; style < FONT_STYLE_COUNT; style++) {
        if (backend->font_data[style]) {
            backend->destroy_font(backend, backend->font_data[style]);
            backend->font_data[style] = NULL;
        }
    }
}

// Load a font with a specific style
bool font_backend_load_font(FontBackend *backend, FontStyle style,
                            const char *font_path, float font_size, const FontOptions *options)
{
    if (!backend || style >= FONT_STYLE_COUNT || !font_path) {
        return false;
    }

    // Unload existing font of this style if present
    if (backend->font_data[style]) {
        backend->destroy_font(backend, backend->font_data[style]);
        backend->font_data[style] = NULL;
        backend->loaded_styles &= ~(1u << style);
    }

    // Load the font
    void *font_data = backend->init_font(backend, font_path,
                                         font_size, style, options);
    if (!font_data) {
        return false;
    }

    // Get font metrics
    if (!backend->get_metrics(backend, font_data,
                              &backend->metrics[style])) {
        backend->destroy_font(backend, font_data);
        return false;
    }

    backend->font_data[style] = font_data;
    backend->loaded_styles |= (1u << style);

    vlog("Loaded font style %d from %s\n", style, font_path);

    return true;
}

// Get metrics for a specific style
const FontMetrics *font_backend_get_metrics(FontBackend *backend, FontStyle style)
{
    if (!backend || style >= FONT_STYLE_COUNT ||
        !(backend->loaded_styles & (1u << style))) {
        return NULL;
    }

    return &backend->metrics[style];
}

// Unified glyph renderer for single and multiple codepoints
GlyphBitmap *font_backend_render_glyphs(FontBackend *backend, FontStyle style,
                                        uint32_t *codepoints, int codepoint_count,
                                        uint8_t fg_r, uint8_t fg_g, uint8_t fg_b)
{
    if (!backend || style >= FONT_STYLE_COUNT ||
        !(backend->loaded_styles & (1u << style)) || !codepoints || codepoint_count <= 0) {
        return NULL;
    }

    return backend->render_glyphs(backend, backend->font_data[style],
                                  codepoints, codepoint_count, fg_r, fg_g, fg_b);
}

// Check if a style is loaded
bool font_backend_has_style(FontBackend *backend, FontStyle style)
{
    return backend && style < FONT_STYLE_COUNT &&
           (backend->loaded_styles & (1u << style));
}
