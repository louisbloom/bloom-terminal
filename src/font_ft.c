#define FT_CONFIG_OPTION_SUBPIXEL_RENDERING
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include <freetype2/freetype/ftcolor.h>
#include FT_MULTIPLE_MASTERS_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_TABLES_H
#include "common.h"
#include "font_backend.h"
#include <cairo/cairo-ft.h>
#include <cairo/cairo.h>
#include <fontconfig/fontconfig.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Internal structure for FreeType/Cairo font data
typedef struct
{
    FT_Face ft_face;          // FreeType face
    float font_size;          // Requested font size in pixels (default 14pt)
    float scale;              // Scale factor for this font size
    unsigned char *font_data; // Raw font file data (if loaded from memory)
    size_t font_data_size;    // Size of font data
    FontStyle style;          // The style this font was loaded for
    float default_weight;     // Default weight for this font
    float min_weight;         // Minimum weight for this font
    float max_weight;         // Maximum weight for this font
    bool has_colr;            // Whether the font has COLR table

    // Font rendering options from Fontconfig
    bool antialias;
    int hinting;
    int hint_style;
    int subpixel_order;
    int lcd_filter;
    int ft_load_flags; // FreeType load flags
    int dpi_x;         // Horizontal DPI for HiDPI support
    int dpi_y;         // Vertical DPI for HiDPI support
} FtFontData;

// Load font file into memory
static bool load_font_file(const char *font_path, unsigned char **data, size_t *size)
{
    FILE *file = fopen(font_path, "rb");
    if (!file) {
        vlog("Failed to open font file: %s\n", font_path);
        return false;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(file);
        vlog("Invalid font file size: %s\n", font_path);
        return false;
    }

    *data = malloc(file_size);
    if (!*data) {
        fclose(file);
        vlog("Failed to allocate memory for font data: %s\n", font_path);
        return false;
    }

    size_t bytes_read = fread(*data, 1, file_size, file);
    fclose(file);

    if (bytes_read != (size_t)file_size) {
        free(*data);
        *data = NULL;
        vlog("Failed to read font file: %s\n", font_path);
        return false;
    }

    *size = bytes_read;
    return true;
}

// Helper function to free a glyph bitmap
static void ft_free_glyph_bitmap(FontBackend *backend, GlyphBitmap *bitmap)
{
    (void)backend; // Unused

    if (!bitmap)
        return;

    free(bitmap->pixels);
    free(bitmap);
}

// Apply font variations based on style
static void apply_font_variations(FtFontData *ft_data, FontStyle style)
{
    if (!ft_data || !ft_data->ft_face)
        return;

    // Check if the font is a variable font by looking for axes
    FT_MM_Var *mm_var = NULL;
    FT_Error ft_error = FT_Get_MM_Var(ft_data->ft_face, &mm_var);
    if (ft_error != 0 || !mm_var) {
        vlog("Font is not a variable font or failed to get MM_Var, skipping variations\n");
        return;
    }

    vlog("Font is a variable font with %d axes\n", mm_var->num_axis);

    // Store weight information for later use
    ft_data->default_weight = 400.0; // Default weight
    ft_data->min_weight = 100.0;     // Minimum weight
    ft_data->max_weight = 900.0;     // Maximum weight

    // Try to find the 'wght' (weight) axis to get actual values
    for (FT_UInt i = 0; i < mm_var->num_axis; i++) {
        if (mm_var->axis[i].tag == FT_MAKE_TAG('w', 'g', 'h', 't')) {
            ft_data->default_weight = (float)mm_var->axis[i].def / 65536.0f;
            ft_data->min_weight = (float)mm_var->axis[i].minimum / 65536.0f;
            ft_data->max_weight = (float)mm_var->axis[i].maximum / 65536.0f;
            vlog("Stored font weight info: min=%f, default=%f, max=%f\n",
                 ft_data->min_weight, ft_data->default_weight, ft_data->max_weight);
            break;
        }
    }

    // Create an array of design coordinates for all axes
    FT_Fixed *coords = malloc(mm_var->num_axis * sizeof(FT_Fixed));
    if (!coords) {
        FT_Done_MM_Var(ft_data->ft_face->glyph->library, mm_var);
        vlog("Failed to allocate memory for font variations\n");
        return;
    }

    // Initialize all coordinates to their default values
    for (FT_UInt i = 0; i < mm_var->num_axis; i++) {
        coords[i] = mm_var->axis[i].def;
    }

    // Handle bold style
    if (style == FONT_STYLE_BOLD) {
        // Try to find the 'wght' (weight) axis
        for (FT_UInt i = 0; i < mm_var->num_axis; i++) {
            if (mm_var->axis[i].tag == FT_MAKE_TAG('w', 'g', 'h', 't')) {
                // Dynamically calculate bold weight based on font's axis range
                // Use a distinguishable weight that's closer to max than default
                float default_weight = (float)mm_var->axis[i].def / 65536.0f;
                float min_weight = (float)mm_var->axis[i].minimum / 65536.0f;
                float max_weight = (float)mm_var->axis[i].maximum / 65536.0f;

                // If we have a reasonable range, calculate a bold weight
                if (max_weight > default_weight && max_weight - min_weight > 100.0) {
                    // Calculate bold weight as 90% of the way from default to max for more dramatic difference
                    float bold_weight = default_weight + (max_weight - default_weight) * 0.9f;
                    coords[i] = (FT_Fixed)(bold_weight * 65536.0f);
                    vlog("Set 'wght' axis to %f for bold style (min: %f, default: %f, max: %f) - using 90%% of range\n",
                         bold_weight, min_weight, default_weight, max_weight);
                } else {
                    // Fallback to traditional bold weight
                    coords[i] = (FT_Fixed)(700.0f * 65536.0f);
                    vlog("Set 'wght' axis to 700.0 (fallback) for bold style\n");
                }
                break;
            }
        }
    }

    // Apply the design coordinates to the FreeType face
    ft_error = FT_Set_Var_Design_Coordinates(ft_data->ft_face, mm_var->num_axis, coords);
    if (ft_error == 0) {
        vlog("Successfully applied FreeType design coordinates\n");
    } else {
        vlog("Failed to apply FreeType design coordinates: error %d\n", ft_error);
    }

    free(coords);
    FT_Done_MM_Var(ft_data->ft_face->glyph->library, mm_var);
}

// Check for COLR table in the font
static bool check_colr_table(FT_Face face)
{
    // Try to load the COLR table
    FT_ULong colr_len = 0;
    FT_Error error = FT_Load_Sfnt_Table(face, FT_MAKE_TAG('C', 'O', 'L', 'R'), 0, NULL, &colr_len);
    if (error == 0 && colr_len > 0) {
        vlog("Font has COLR table (length: %lu)\n", colr_len);
        return true;
    }

    vlog("Font does not have COLR table (error: %d)\n", error);
    return false;
}

// Initialize FreeType/Cairo font
static void *ft_init_font(FontBackend *backend, const char *font_path,
                          float font_size, FontStyle style, const FontOptions *options)
{
    (void)backend; // Unused

    vlog("Initializing FreeType/Cairo font from %s, size %.1f, style %d\n", font_path, font_size, style);

    // Load font file
    unsigned char *font_data = NULL;
    size_t font_data_size = 0;
    if (!load_font_file(font_path, &font_data, &font_data_size)) {
        return NULL;
    }

    // Allocate font data structure
    FtFontData *ft_data = calloc(1, sizeof(FtFontData));
    if (!ft_data) {
        free(font_data);
        return NULL;
    }

    ft_data->font_data = font_data;
    ft_data->font_data_size = font_data_size;
    ft_data->font_size = font_size;
    ft_data->style = style;

    // Set font options from Fontconfig
    if (options) {
        ft_data->antialias = options->antialias;
        ft_data->hinting = options->hinting;
        ft_data->hint_style = options->hint_style;
        ft_data->subpixel_order = options->subpixel_order;
        ft_data->lcd_filter = options->lcd_filter;
        ft_data->ft_load_flags = options->ft_load_flags;
        ft_data->dpi_x = options->dpi_x;
        ft_data->dpi_y = options->dpi_y;
    } else {
        // Set default options if none provided
        ft_data->antialias = true;
        ft_data->hinting = true;
        ft_data->hint_style = FC_HINT_SLIGHT;
        ft_data->subpixel_order = FC_RGBA_NONE;
        ft_data->lcd_filter = FC_LCD_DEFAULT;
        ft_data->ft_load_flags = FT_LOAD_TARGET_LIGHT;
        ft_data->dpi_x = 96;
        ft_data->dpi_y = 96;
    }

    // Initialize FreeType library (this should ideally be done once globally)
    static FT_Library ft_library = NULL;
    if (!ft_library) {
        FT_Error error = FT_Init_FreeType(&ft_library);
        if (error) {
            vlog("Failed to initialize FreeType library\n");
            free(font_data);
            free(ft_data);
            return NULL;
        }
    }

    // Create FreeType face from memory
    FT_Error error = FT_New_Memory_Face(ft_library, font_data, font_data_size, 0, &ft_data->ft_face);
    if (error) {
        vlog("Failed to create FreeType face from %s\n", font_path);
        free(font_data);
        free(ft_data);
        return NULL;
    }

    // Set character size with DPI support (using 26.6 fixed-point format)
    // This is the correct approach for HiDPI support as per the design document
    error = FT_Set_Char_Size(ft_data->ft_face, 0, (FT_F26Dot6)(font_size * 64), ft_data->dpi_x, ft_data->dpi_y);
    if (error) {
        vlog("Failed to set character size for FreeType face\n");
        FT_Done_Face(ft_data->ft_face);
        free(font_data);
        free(ft_data);
        return NULL;
    }

    // Calculate scale for pixel height
    ft_data->scale = (float)font_size / (float)ft_data->ft_face->units_per_EM;
    vlog("Font scale factor: %f for size %.1f\n", ft_data->scale, font_size);

    // Check for COLR table
    ft_data->has_colr = check_colr_table(ft_data->ft_face);

    // Apply font variations based on style
    apply_font_variations(ft_data, style);

    vlog("Created FreeType/Cairo font from %s\n", font_path);

    return ft_data;
}

// Destroy FreeType/Cairo font
static void ft_destroy_font(FontBackend *backend, void *font_data)
{
    (void)backend; // Unused

    FtFontData *ft_data = (FtFontData *)font_data;
    if (!ft_data)
        return;

    if (ft_data->ft_face) {
        FT_Done_Face(ft_data->ft_face);
    }

    free(ft_data->font_data);
    free(ft_data);
}

// Get font metrics
static bool ft_get_metrics(FontBackend *backend, void *font_data, FontMetrics *metrics)
{
    (void)backend; // Unused

    FtFontData *ft_data = (FtFontData *)font_data;
    if (!ft_data || !metrics)
        return false;

    FT_Face face = ft_data->ft_face;

    // Calculate metrics using 26.6 fixed-point conversion as per design document
    // Cell width = max_advance_width (converted from 26.6 fixed-point)
    // Cell height = ascender + descender (ensuring baseline alignment)
    metrics->ascent = face->size->metrics.ascender >> 6;    // Convert from 26.6 fixed-point
    metrics->descent = -face->size->metrics.descender >> 6; // Convert from 26.6 fixed-point
    metrics->line_gap = (face->size->metrics.height - face->size->metrics.ascender + face->size->metrics.descender) >> 6;

    // Precise monospace cell_width from 'm' glyph advance (fallback to max_advance)
    FT_UInt m_index = FT_Get_Char_Index(face, 'm');
    int orig_max_advance = face->size->metrics.max_advance >> 6;
    if (m_index != 0) {
        FT_Error m_err = FT_Load_Glyph(face, m_index, FT_LOAD_NO_BITMAP);
        if (m_err == 0) {
            metrics->cell_width = (int)(face->glyph->advance.x >> 6);
            vlog("cell_width from 'm' advance: %d (max_advance fallback: %d)\n",
                 metrics->cell_width, orig_max_advance);
            goto m_done; // Skip max_advance
        }
    }
    metrics->cell_width = orig_max_advance;
    vlog("cell_width fallback to max_advance: %d\n", metrics->cell_width);
m_done:;

    // For cell height, use ascender + descender for proper baseline alignment
    metrics->cell_height = metrics->ascent + metrics->descent;

    // For glyph dimensions, we'll use the actual metrics from the face
    // This is a simplified approach - in a full implementation we'd measure actual glyphs
    metrics->glyph_width = metrics->cell_width;   // Approximation for now
    metrics->glyph_height = metrics->cell_height; // Approximation for now

    vlog("FreeType/Cairo metrics (26.6 fixed-point): asc=%d, des=%d, line_gap=%d, glyph=%dx%d, cell=%dx%d\n",
         metrics->ascent, metrics->descent, metrics->line_gap,
         metrics->glyph_width, metrics->glyph_height,
         metrics->cell_width, metrics->cell_height);

    return true;
}

// Render glyphs using FreeType directly (unified function for single/multiple codepoints)
static GlyphBitmap *ft_render_glyph(FontBackend *backend, void *font_data,
                                    uint32_t *codepoints, int codepoint_count,
                                    uint8_t fg_r, uint8_t fg_g, uint8_t fg_b)
{
    (void)backend; // Unused

    FtFontData *ft_data = (FtFontData *)font_data;
    if (!ft_data || !codepoints || codepoint_count <= 0)
        return NULL;

    // For now, only handle the first codepoint (same behavior as before)
    uint32_t codepoint = codepoints[0];
    vlog("Rendering glyph U+%04X with direct FreeType (style=%d)\n", codepoint, ft_data->style);

    FT_Face face = ft_data->ft_face;

    // Load the glyph
    FT_UInt glyph_index = FT_Get_Char_Index(face, codepoint);
    if (glyph_index == 0) {
        vlog("No glyph index found for U+%04X\n", codepoint);
        return NULL;
    }

    // Load glyph with appropriate flags
    FT_Int32 load_flags = FT_LOAD_DEFAULT;
    if (ft_data->antialias) {
        load_flags |= FT_LOAD_TARGET_NORMAL;
    } else {
        load_flags |= FT_LOAD_TARGET_MONO;
    }

    // Apply hinting settings
    if (ft_data->hinting) {
        switch (ft_data->hint_style) {
        case FC_HINT_NONE:
            load_flags |= FT_LOAD_NO_HINTING;
            break;
        case FC_HINT_SLIGHT:
            load_flags |= FT_LOAD_TARGET_LIGHT;
            break;
        case FC_HINT_MEDIUM:
        case FC_HINT_FULL:
            load_flags |= FT_LOAD_TARGET_NORMAL;
            break;
        }
    } else {
        load_flags |= FT_LOAD_NO_HINTING;
    }

    FT_Error error = FT_Load_Glyph(face, glyph_index, load_flags);
    if (error) {
        vlog("Failed to load glyph for U+%04X: error %d\n", codepoint, error);
        return NULL;
    }

    // Handle COLR fonts differently (keep using Cairo for color glyphs)
    if (ft_data->has_colr) {
        vlog("Using Cairo for COLR glyph U+%04X\n", codepoint);
        // Fall back to Cairo for COLR fonts since they need special handling
        // This preserves the existing Cairo-based rendering for color fonts
        goto cairo_fallback;
    }

    // Render the glyph to a bitmap
    error = FT_Render_Glyph(face->glyph, ft_data->antialias ? FT_RENDER_MODE_NORMAL : FT_RENDER_MODE_MONO);
    if (error) {
        vlog("Failed to render glyph for U+%04X: error %d\n", codepoint, error);
        return NULL;
    }

    FT_GlyphSlot slot = face->glyph;
    FT_Bitmap *bitmap = &slot->bitmap;

    // Allocate glyph bitmap
    GlyphBitmap *glyph_bitmap = malloc(sizeof(GlyphBitmap));
    if (!glyph_bitmap) {
        return NULL;
    }

    glyph_bitmap->width = bitmap->width;
    glyph_bitmap->height = bitmap->rows;
    glyph_bitmap->x_offset = slot->bitmap_left;
    glyph_bitmap->y_offset = slot->bitmap_top;
    glyph_bitmap->advance = (int)(slot->advance.x >> 6); // Convert from 26.6 fixed point

    // Handle zero-width or zero-height glyphs (e.g., spaces)
    if (glyph_bitmap->width <= 0 || glyph_bitmap->height <= 0) {
        vlog("Glyph has zero dimensions: %dx%d, creating empty bitmap\n", glyph_bitmap->width, glyph_bitmap->height);
        glyph_bitmap->pixels = NULL; // No pixels for empty glyphs
        return glyph_bitmap;
    }

    // Allocate RGBA pixels
    glyph_bitmap->pixels = malloc(glyph_bitmap->width * glyph_bitmap->height * 4);
    if (!glyph_bitmap->pixels) {
        free(glyph_bitmap);
        return NULL;
    }

    // Convert FreeType bitmap to RGBA
    if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY) {
        // Grayscale bitmap
        for (int y = 0; y < (int)bitmap->rows; y++) {
            unsigned char *src_row = bitmap->buffer + y * bitmap->pitch;
            unsigned char *dst_row = glyph_bitmap->pixels + y * glyph_bitmap->width * 4;
            for (int x = 0; x < (int)bitmap->width; x++) {
                unsigned char alpha = src_row[x];
                dst_row[x * 4 + 0] = fg_r;  // R
                dst_row[x * 4 + 1] = fg_g;  // G
                dst_row[x * 4 + 2] = fg_b;  // B
                dst_row[x * 4 + 3] = alpha; // A
            }
        }
    } else if (bitmap->pixel_mode == FT_PIXEL_MODE_MONO) {
        // Monochrome bitmap
        for (int y = 0; y < (int)bitmap->rows; y++) {
            unsigned char *src_row = bitmap->buffer + y * bitmap->pitch;
            unsigned char *dst_row = glyph_bitmap->pixels + y * glyph_bitmap->width * 4;
            for (int x = 0; x < (int)bitmap->width; x++) {
                unsigned char byte = src_row[x >> 3];
                unsigned char bit = (byte >> (7 - (x & 7))) & 1;
                unsigned char alpha = bit ? 255 : 0;
                dst_row[x * 4 + 0] = fg_r;  // R
                dst_row[x * 4 + 1] = fg_g;  // G
                dst_row[x * 4 + 2] = fg_b;  // B
                dst_row[x * 4 + 3] = alpha; // A
            }
        }
    } else {
        vlog("Unsupported pixel mode: %d\n", bitmap->pixel_mode);
        free(glyph_bitmap->pixels);
        free(glyph_bitmap);
        return NULL;
    }

    vlog("Successfully rendered glyph U+%04X with direct FreeType: %dx%d pixels, x_offset=%d, y_offset=%d, advance=%d\n",
         codepoint, glyph_bitmap->width, glyph_bitmap->height, glyph_bitmap->x_offset, glyph_bitmap->y_offset, glyph_bitmap->advance);
    return glyph_bitmap;

cairo_fallback:
    // Original Cairo-based rendering for COLR fonts (preserved for compatibility)
    // Create a temporary surface to measure text extents
    cairo_surface_t *temp_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64);
    if (cairo_surface_status(temp_surface) != CAIRO_STATUS_SUCCESS) {
        vlog("Failed to create temporary Cairo surface for glyph U+%04X\n", codepoint);
        return NULL;
    }

    cairo_t *temp_cr = cairo_create(temp_surface);
    if (cairo_status(temp_cr) != CAIRO_STATUS_SUCCESS) {
        vlog("Failed to create temporary Cairo context for glyph U+%04X\n", codepoint);
        cairo_surface_destroy(temp_surface);
        return NULL;
    }

    // Create Cairo font face from FreeType face
    cairo_font_face_t *cairo_face = cairo_ft_font_face_create_for_ft_face(ft_data->ft_face, 0);
    if (cairo_font_face_status(cairo_face) != CAIRO_STATUS_SUCCESS) {
        vlog("Failed to create Cairo font face for glyph U+%04X\n", codepoint);
        cairo_destroy(temp_cr);
        cairo_surface_destroy(temp_surface);
        return NULL;
    }

    // Set the font face and size on temporary context
    cairo_set_font_face(temp_cr, cairo_face);
    cairo_set_font_size(temp_cr, ft_data->font_size);

    // Convert codepoint to UTF-8
    char utf8_char[5] = { 0 }; // Up to 4 bytes for UTF-8 + null terminator

    if (codepoint <= 0x7F) {
        utf8_char[0] = (char)codepoint;
    } else if (codepoint <= 0x7FF) {
        utf8_char[0] = 0xC0 | (codepoint >> 6);
        utf8_char[1] = 0x80 | (codepoint & 0x3F);
    } else if (codepoint <= 0xFFFF) {
        utf8_char[0] = 0xE0 | (codepoint >> 12);
        utf8_char[1] = 0x80 | ((codepoint >> 6) & 0x3F);
        utf8_char[2] = 0x80 | (codepoint & 0x3F);
    } else {
        utf8_char[0] = 0xF0 | (codepoint >> 18);
        utf8_char[1] = 0x80 | ((codepoint >> 12) & 0x3F);
        utf8_char[2] = 0x80 | ((codepoint >> 6) & 0x3F);
        utf8_char[3] = 0x80 | (codepoint & 0x3F);
    }

    // Get text extents for proper sizing
    cairo_text_extents_t extents;
    cairo_text_extents(temp_cr, utf8_char, &extents);

    vlog("Glyph U+%04X text extents: width=%.2f, height=%.2f, x_bearing=%.2f, y_bearing=%.2f, x_advance=%.2f, y_advance=%.2f\n",
         codepoint, extents.width, extents.height, extents.x_bearing, extents.y_bearing, extents.x_advance, extents.y_advance);

    // Determine surface size based on text extents with padding
    int padding = 2; // Use minimal padding
    int surface_width = (int)(extents.width + padding * 2 + 0.5);
    int surface_height = (int)(extents.height + padding * 2 + 0.5);

    // Ensure minimum size
    surface_width = surface_width > 0 ? surface_width : 32;
    surface_height = surface_height > 0 ? surface_height : 32;

    vlog("Glyph U+%04X surface size: %dx%d\n", codepoint, surface_width, surface_height);

    // Clean up temporary context
    cairo_destroy(temp_cr);
    cairo_surface_destroy(temp_surface);

    // Create actual surface and context
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, surface_width, surface_height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        vlog("Failed to create Cairo surface for glyph U+%04X\n", codepoint);
        cairo_font_face_destroy(cairo_face);
        return NULL;
    }

    cairo_t *cr = cairo_create(surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        vlog("Failed to create Cairo context for glyph U+%04X\n", codepoint);
        cairo_surface_destroy(surface);
        cairo_font_face_destroy(cairo_face);
        return NULL;
    }

    // Clear the surface to transparent
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_restore(cr);

    // Set the font face and size
    cairo_set_font_face(cr, cairo_face);
    cairo_set_font_size(cr, ft_data->font_size);

    // Create and configure font options based on Fontconfig settings
    cairo_font_options_t *font_options = cairo_font_options_create();

    // Map Fontconfig antialias setting to Cairo
    if (ft_data->antialias) {
        cairo_font_options_set_antialias(font_options, CAIRO_ANTIALIAS_SUBPIXEL);
    } else {
        cairo_font_options_set_antialias(font_options, CAIRO_ANTIALIAS_NONE);
    }

    // Map Fontconfig hint style to Cairo
    switch (ft_data->hint_style) {
    case FC_HINT_NONE:
        cairo_font_options_set_hint_style(font_options, CAIRO_HINT_STYLE_NONE);
        break;
    case FC_HINT_SLIGHT:
        cairo_font_options_set_hint_style(font_options, CAIRO_HINT_STYLE_SLIGHT);
        break;
    case FC_HINT_MEDIUM:
        cairo_font_options_set_hint_style(font_options, CAIRO_HINT_STYLE_MEDIUM);
        break;
    case FC_HINT_FULL:
        cairo_font_options_set_hint_style(font_options, CAIRO_HINT_STYLE_FULL);
        break;
    default:
        cairo_font_options_set_hint_style(font_options, CAIRO_HINT_STYLE_SLIGHT);
        break;
    }

    // Always enable hint metrics for better consistency
    cairo_font_options_set_hint_metrics(font_options, CAIRO_HINT_METRICS_ON);

    // Set subpixel order for better text rendering (as per design document)
    cairo_font_options_set_subpixel_order(font_options, CAIRO_SUBPIXEL_ORDER_RGB);

    // Apply font options
    cairo_set_font_options(cr, font_options);

    // Get font-wide metrics for proper baseline alignment
    cairo_font_extents_t font_extents;
    cairo_font_extents(cr, &font_extents);

    // Position for rendering
    // For x-positioning, we use the glyph's own bearing for proper horizontal alignment
    double render_x = padding - extents.x_bearing;
    // For y-positioning, we use the glyph's own bearing for proper vertical alignment
    double render_y = padding - extents.y_bearing;

    // Render directly as color glyphs
    vlog("Rendering COLR glyph U+%04X directly with Cairo\n", codepoint);
    cairo_move_to(cr, render_x, render_y);
    cairo_show_text(cr, utf8_char);

    // Check for Cairo errors
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        vlog("Cairo error when rendering glyph U+%04X: %s\n", codepoint, cairo_status_to_string(cairo_status(cr)));
        cairo_font_face_destroy(cairo_face);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return NULL;
    }

    // Get the actual surface dimensions and data
    cairo_surface_flush(surface);
    unsigned char *cairo_pixels = cairo_image_surface_get_data(surface);
    int cairo_width = cairo_image_surface_get_width(surface);
    int cairo_height = cairo_image_surface_get_height(surface);
    int cairo_stride = cairo_image_surface_get_stride(surface);

    // Allocate glyph bitmap
    GlyphBitmap *glyph_bitmap_cairo = malloc(sizeof(GlyphBitmap));
    if (!glyph_bitmap_cairo) {
        cairo_font_face_destroy(cairo_face);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return NULL;
    }

    glyph_bitmap_cairo->width = cairo_width;
    glyph_bitmap_cairo->height = cairo_height;
    glyph_bitmap_cairo->x_offset = (int)(render_x + extents.x_bearing); // Position relative to cell
    // For y_offset, we need to calculate the distance from the top of the bitmap to the baseline
    // render_y is the position where we draw the glyph, and extents.y_bearing tells us where the baseline is relative to that
    glyph_bitmap_cairo->y_offset = (int)(padding - extents.y_bearing); // Position relative to baseline
    glyph_bitmap_cairo->advance = (int)(extents.x_advance + 0.5);      // Round to nearest integer

    // Handle zero-width or zero-height glyphs (e.g., spaces)
    if (glyph_bitmap_cairo->width <= 0 || glyph_bitmap_cairo->height <= 0) {
        vlog("Glyph has zero dimensions: %dx%d, creating empty bitmap\n", glyph_bitmap_cairo->width, glyph_bitmap_cairo->height);
        glyph_bitmap_cairo->pixels = NULL; // No pixels for empty glyphs
        cairo_font_face_destroy(cairo_face);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return glyph_bitmap_cairo;
    }

    // Allocate RGBA pixels
    glyph_bitmap_cairo->pixels = malloc(glyph_bitmap_cairo->width * glyph_bitmap_cairo->height * 4);
    if (!glyph_bitmap_cairo->pixels) {
        free(glyph_bitmap_cairo);
        cairo_font_face_destroy(cairo_face);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return NULL;
    }

    // Copy and convert the pixel data (Cairo uses BGRA, we need RGBA)
    for (int y = 0; y < cairo_height; y++) {
        unsigned char *src_row = cairo_pixels + y * cairo_stride;
        unsigned char *dst_row = glyph_bitmap_cairo->pixels + y * cairo_width * 4;
        for (int x = 0; x < cairo_width; x++) {
            unsigned char b = src_row[x * 4 + 0];
            unsigned char g = src_row[x * 4 + 1];
            unsigned char r = src_row[x * 4 + 2];
            unsigned char a = src_row[x * 4 + 3];

            dst_row[x * 4 + 0] = r; // R
            dst_row[x * 4 + 1] = g; // G
            dst_row[x * 4 + 2] = b; // B
            dst_row[x * 4 + 3] = a; // A
        }
    }

    // Clean up
    cairo_font_options_destroy(font_options);
    cairo_font_face_destroy(cairo_face);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    vlog("Successfully rendered COLR glyph U+%04X with Cairo: %dx%d pixels, x_offset=%d, y_offset=%d, advance=%d\n",
         codepoint, glyph_bitmap_cairo->width, glyph_bitmap_cairo->height, glyph_bitmap_cairo->x_offset, glyph_bitmap_cairo->y_offset, glyph_bitmap_cairo->advance);
    return glyph_bitmap_cairo;
}

// Get glyph info without rendering
static bool ft_get_glyph_info(FontBackend *backend, void *font_data, uint32_t codepoint,
                              int *advance, int *left_bearing, int *top_bearing)
{
    (void)backend; // Unused

    FtFontData *ft_data = (FtFontData *)font_data;
    if (!ft_data)
        return false;

    FT_UInt glyph_index = FT_Get_Char_Index(ft_data->ft_face, codepoint);
    if (glyph_index == 0) {
        return false;
    }

    FT_Error error = FT_Load_Glyph(ft_data->ft_face, glyph_index, FT_LOAD_DEFAULT);
    if (error) {
        return false;
    }

    if (advance)
        *advance = (int)(ft_data->ft_face->glyph->advance.x >> 6); // Convert from 26.6 fixed point
    if (left_bearing)
        *left_bearing = ft_data->ft_face->glyph->metrics.horiBearingX >> 6;

    // For top bearing, we need the glyph box
    if (top_bearing) {
        *top_bearing = ft_data->ft_face->glyph->metrics.horiBearingY >> 6;
    }

    return true;
}

// FreeType/Cairo backend implementation
FontBackend ft_backend = {
    .name = "freetype/cairo",
    .init = font_backend_init,
    .destroy = font_backend_destroy,
    .init_font = ft_init_font,
    .destroy_font = ft_destroy_font,
    .get_metrics = ft_get_metrics,
    .render_glyphs = ft_render_glyph, // Unified renderer
    .get_glyph_info = ft_get_glyph_info,
    .free_glyph_bitmap = ft_free_glyph_bitmap,
    .load_font = font_backend_load_font,
    .get_style_metrics = font_backend_get_metrics,
    .has_style = font_backend_has_style
};