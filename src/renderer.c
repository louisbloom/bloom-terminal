#include "renderer.h"
#include "common.h"
#include "font_backend.h"
#include "font_resolver.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper function to convert VTermColor to SDL color values
static void convert_vterm_color_to_sdl(const VTermColor *vcol, Uint8 *r, Uint8 *g, Uint8 *b)
{
    if (VTERM_COLOR_IS_RGB(vcol)) {
        *r = vcol->rgb.red;
        *g = vcol->rgb.green;
        *b = vcol->rgb.blue;
    } else if (VTERM_COLOR_IS_INDEXED(vcol)) {
        // Standard 256-color palette
        static const Uint8 indexed_colors[16][3] = {
            // Standard colors (0-7)
            { 0, 0, 0 },       // black
            { 255, 0, 0 },     // red
            { 0, 255, 0 },     // green
            { 255, 255, 0 },   // yellow
            { 0, 0, 255 },     // blue
            { 255, 0, 255 },   // magenta
            { 0, 255, 255 },   // cyan
            { 255, 255, 255 }, // white
            // Bright colors (8-15)
            { 128, 128, 128 }, // bright black (dark gray)
            { 255, 128, 128 }, // bright red
            { 128, 255, 128 }, // bright green
            { 255, 255, 128 }, // bright yellow
            { 128, 128, 255 }, // bright blue
            { 255, 128, 255 }, // bright magenta
            { 128, 255, 255 }, // bright cyan
            { 255, 255, 255 }  // bright white
        };

        Uint8 idx = vcol->indexed.idx;
        if (idx < 16) {
            *r = indexed_colors[idx][0];
            *g = indexed_colors[idx][1];
            *b = indexed_colors[idx][2];
        } else {
            // For colors 16-255, use a simple mapping
            // This is a simplified approach - a full implementation would use the xterm 256-color palette
            *r = *g = *b = (idx - 16) * 255 / 240;
        }
    } else {
        // Default to white for unknown color types
        *r = *g = *b = 255;
    }
}

// Helper function to create an SDL_Texture from a GlyphBitmap
static SDL_Texture *create_texture_from_glyph_bitmap(Renderer *rend, GlyphBitmap *glyph_bitmap)
{
    // Handle empty glyphs (e.g., spaces)
    if (!glyph_bitmap) {
        vlog("create_texture_from_glyph_bitmap: null glyph bitmap\n");
        return NULL;
    }

    // Allow zero-width or zero-height glyphs (they just won't render anything)
    if (glyph_bitmap->width <= 0 || glyph_bitmap->height <= 0) {
        vlog("create_texture_from_glyph_bitmap: empty glyph bitmap (%dx%d)\n",
             glyph_bitmap->width, glyph_bitmap->height);
        return NULL; // Not an error, just nothing to render
    }

    // Handle glyphs with no pixels (e.g., spaces)
    if (!glyph_bitmap->pixels) {
        vlog("create_texture_from_glyph_bitmap: glyph with no pixels (%dx%d)\n",
             glyph_bitmap->width, glyph_bitmap->height);
        return NULL; // Not an error, just nothing to render
    }

    SDL_Surface *surface = SDL_CreateSurface(glyph_bitmap->width, glyph_bitmap->height, SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        vlog("create_texture_from_glyph_bitmap: failed to create surface for glyph\n");
        return NULL;
    }

    memcpy(surface->pixels, glyph_bitmap->pixels, glyph_bitmap->width * glyph_bitmap->height * 4);

    SDL_Texture *texture = SDL_CreateTextureFromSurface(rend->renderer, surface);
    SDL_DestroySurface(surface);

    if (texture) {
        // Set texture scale mode to nearest neighbor to prevent glyph blurring
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    } else {
        vlog("create_texture_from_glyph_bitmap: failed to create texture from surface\n");
    }

    return texture;
}

Renderer *renderer_init(SDL_Renderer *sdl_renderer, SDL_Window *window)
{
    Renderer *rend = malloc(sizeof(Renderer));
    if (!rend) {
        return NULL;
    }

    rend->renderer = sdl_renderer;
    rend->window = window;
    rend->font_backend = NULL;
    rend->cell_width = 0;
    rend->cell_height = 0;
    rend->char_width = 0;
    rend->char_height = 0;
    rend->font_ascent = 0;
    rend->font_descent = 0;
    rend->width = 0;
    rend->height = 0;
    rend->debug_grid = 0; // Initialize debug grid to off

    // Initialize font backend with FreeType/Cairo backend
    rend->font_backend = &ft_backend;
    if (!font_backend_init(rend->font_backend)) {
        vlog("Failed to initialize font backend\n");
        free(rend);
        return NULL;
    }

    return rend;
}

void renderer_destroy(Renderer *rend)
{
    if (rend) {
        if (rend->font_backend) {
            font_backend_destroy(rend->font_backend);
        }
        free(rend);
    }
}

int renderer_load_fonts(Renderer *rend)
{
    const float font_size = 24.0f; // Default font size in points

    vlog("Loading fonts with size %.1f\n", font_size);

    // Initialize font resolver
    if (font_resolver_init() != 0) {
        fprintf(stderr, "Failed to initialize font resolver\n");
        return -1;
    }

    // Setup DPI options
    FontOptions options = { 0 };
    options.antialias = true;
    options.hinting = 1;
    options.hint_style = 1;
    options.subpixel_order = 0;
    options.lcd_filter = 0;
    options.ft_load_flags = 0;
    options.dpi_x = 96;
    options.dpi_y = 96;

    // Get DPI from window if available
    if (rend->window) {
        float pixel_density = SDL_GetWindowPixelDensity(rend->window);
        if (pixel_density > 0.0f) {
            // Calculate DPI based on pixel density (assuming 96 DPI as base)
            // This is a reasonable approximation for HiDPI displays
            int dpi = (int)(96.0f * pixel_density);
            options.dpi_x = dpi;
            options.dpi_y = dpi;
            vlog("SDL Pixel Density: %.2f (calculated DPI: %d)\n", pixel_density, dpi);
        } else {
            // Fallback to default DPI
            vlog("Failed to get window pixel density: %s\n", SDL_GetError());
        }
    }

    // Load normal monospace font
    FontResolutionResult result;
    if (font_resolver_find_font(FONT_TYPE_NORMAL, &result) == 0) {
        if (font_backend_load_font(rend->font_backend, FONT_STYLE_NORMAL, result.font_path, font_size, &options)) {
            vlog("Normal font loaded successfully from %s\n", result.font_path);
        } else {
            fprintf(stderr, "Failed to load normal font from %s\n", result.font_path);
            font_resolver_free_result(&result);
            font_resolver_cleanup();
            return -1;
        }
        font_resolver_free_result(&result);
    } else {
        fprintf(stderr, "Failed to find normal font\n");
        font_resolver_cleanup();
        return -1;
    }

    // Load bold font
    if (font_resolver_find_font(FONT_TYPE_BOLD, &result) == 0) {
        if (font_backend_load_font(rend->font_backend, FONT_STYLE_BOLD, result.font_path, font_size, &options)) {
            vlog("Bold font loaded successfully from %s\n", result.font_path);
        } else {
            vlog("Failed to load bold font from %s\n", result.font_path);
        }
        font_resolver_free_result(&result);
    }

    // Load emoji font
    if (font_resolver_find_font(FONT_TYPE_EMOJI, &result) == 0) {
        if (font_backend_load_font(rend->font_backend, FONT_STYLE_EMOJI, result.font_path, font_size, &options)) {
            vlog("Emoji font loaded successfully from %s\n", result.font_path);
        } else {
            vlog("Failed to load emoji font from %s\n", result.font_path);
        }
        font_resolver_free_result(&result);
    }

    // Cleanup font resolver
    font_resolver_cleanup();

    // Calculate cell dimensions from font metrics using normal font
    const FontMetrics *metrics = font_backend_get_metrics(rend->font_backend, FONT_STYLE_NORMAL);
    if (metrics) {
        rend->font_ascent = metrics->ascent;
        rend->font_descent = metrics->descent;
        rend->char_width = metrics->glyph_width;
        rend->char_height = metrics->glyph_height;
        rend->cell_width = metrics->cell_width;
        rend->cell_height = metrics->cell_height;

        vlog("Font metrics - ascent: %d, descent: %d\n",
             rend->font_ascent, rend->font_descent);
        vlog("Character dimensions - width: %d, height: %d\n",
             rend->char_width, rend->char_height);
        vlog("Cell dimensions - width: %d, height: %d\n",
             rend->cell_width, rend->cell_height);

        // Log which fonts were successfully loaded
        vlog("Font loading summary:\n");
        vlog("  Normal font: %s\n", font_backend_has_style(rend->font_backend, FONT_STYLE_NORMAL) ? "Loaded" : "Not loaded");
        vlog("  Bold font: %s\n", font_backend_has_style(rend->font_backend, FONT_STYLE_BOLD) ? "Loaded" : "Not loaded");
        vlog("  Emoji font: %s\n", font_backend_has_style(rend->font_backend, FONT_STYLE_EMOJI) ? "Loaded" : "Not loaded");
    } else {
        vlog("ERROR: No font available for metrics calculation\n");
        return -1;
    }

    return 0;
}

void renderer_draw_terminal(Renderer *rend, Terminal *term)
{
    if (!rend || !term || !font_backend_has_style(rend->font_backend, FONT_STYLE_NORMAL)) {
        vlog("Renderer draw terminal failed: invalid parameters\n");
        return;
    }

    vlog("Drawing terminal with dimensions %dx%d\n", rend->width, rend->height);
    vlog("Cell dimensions: %dx%d, Font ascent: %d\n",
         rend->cell_width, rend->cell_height, rend->font_ascent);

    // Clear screen
    SDL_SetRenderDrawColor(rend->renderer, 0, 0, 0, 255);
    SDL_RenderClear(rend->renderer);

    // Calculate number of rows and columns that fit in the window
    int term_rows, term_cols;
    terminal_get_dimensions(term, &term_rows, &term_cols);

    int display_rows = rend->height / rend->cell_height;
    int display_cols = rend->width / rend->cell_width;

    // Limit to actual terminal size
    if (display_rows > term_rows)
        display_rows = term_rows;
    if (display_cols > term_cols)
        display_cols = term_cols;

    // Get cursor position for rendering
    VTermPos cursor_pos = { 0, 0 };
    // Note: In a full implementation, we would get the actual cursor position from the terminal

    // Render each cell
    for (int row = 0; row < display_rows; row++) {
        for (int col = 0; col < display_cols; col++) {
            VTermScreenCell cell;
            if (terminal_get_cell(term, row, col, &cell) < 0) {
                continue;
            }

            // Font selection is now handled in the rendering code below
            // Add logging for font attributes
            if (cell.attrs.bold || cell.attrs.italic) {
                vlog("Cell (%d,%d) attributes: bold=%d, italic=%d\n",
                     row, col, cell.attrs.bold, cell.attrs.italic);
            }

            // Set foreground and background colors using vterm's color conversion
            Uint8 r = 255, g = 255, b = 255;    // default white
            Uint8 bg_r = 0, bg_g = 0, bg_b = 0; // default black (will be transparent)

            // Convert foreground color
            convert_vterm_color_to_sdl(&cell.fg, &r, &g, &b);

            // Convert background color
            convert_vterm_color_to_sdl(&cell.bg, &bg_r, &bg_g, &bg_b);

            if (!VTERM_COLOR_IS_DEFAULT_BG(&cell.bg)) {
                SDL_FRect bg_rect = {
                    col * rend->cell_width,
                    row * rend->cell_height,
                    rend->cell_width,
                    rend->cell_height
                };
                SDL_SetRenderDrawColor(rend->renderer, bg_r, bg_g, bg_b, 255);
                SDL_RenderFillRect(rend->renderer, &bg_rect);
            }

            // Skip empty cells
            if (cell.chars[0] == 0) {
                continue;
            }

            // Convert character to UTF-8 string for rendering
            char text[32] = { 0 };
            int pos = 0;
            for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0 && pos < (int)(sizeof(text) - 3); i++) {
                uint32_t ch = cell.chars[i];
                if (ch < 0x80) {
                    text[pos++] = (char)ch;
                } else if (ch < 0x800) {
                    text[pos++] = (char)(0xC0 | (ch >> 6));
                    text[pos++] = (char)(0x80 | (ch & 0x3F));
                } else if (ch < 0x10000) {
                    text[pos++] = (char)(0xE0 | (ch >> 12));
                    text[pos++] = (char)(0x80 | ((ch >> 6) & 0x3F));
                    text[pos++] = (char)(0x80 | (ch & 0x3F));
                } else if (ch < 0x110000) {
                    text[pos++] = (char)(0xF0 | (ch >> 18));
                    text[pos++] = (char)(0x80 | ((ch >> 12) & 0x3F));
                    text[pos++] = (char)(0x80 | ((ch >> 6) & 0x3F));
                    text[pos++] = (char)(0x80 | (ch & 0x3F));
                }
            }

            // Render character if it's printable
            if (pos > 0) {
                // Get the first codepoint (simplified - handles only first character)
                uint32_t codepoint = cell.chars[0];

                // Select appropriate font style
                FontStyle style = FONT_STYLE_NORMAL;
                const char *font_type = "normal";
                if (cell.attrs.bold && font_backend_has_style(rend->font_backend, FONT_STYLE_BOLD)) {
                    style = FONT_STYLE_BOLD;
                    font_type = "bold";
                }

                if ((codepoint >= 0x1F000 && codepoint <= 0x1F9FF) && font_backend_has_style(rend->font_backend, FONT_STYLE_EMOJI)) {
                    style = FONT_STYLE_EMOJI;
                    font_type = "emoji";
                }

                vlog("  Selected font: %s (style=%d)\n", font_type, style);

                // Render glyph using font backend to get bitmap
                GlyphBitmap *glyph_bitmap = font_backend_render_glyphs(rend->font_backend, style, &codepoint, 1, r, g, b);
                if (glyph_bitmap) {
                    // Create texture from glyph bitmap
                    SDL_Texture *texture = create_texture_from_glyph_bitmap(rend, glyph_bitmap);
                    if (texture) {
                        // Get font metrics for positioning
                        const FontMetrics *metrics = font_backend_get_metrics(rend->font_backend, style);
                        if (!metrics) {
                            metrics = font_backend_get_metrics(rend->font_backend, FONT_STYLE_NORMAL);
                        }

                        if (metrics) {
                            // Calculate cell position
                            int cell_x = col * rend->cell_width;
                            int cell_y = row * rend->cell_height;

                            // Use the normal font's ascent as the global baseline
                            // glyph_bitmap->y_offset is the distance from baseline to top of glyph (positive above baseline)
                            // Position the glyph so that the baseline aligns correctly
                            // Use precise positioning without centering to avoid fractional coordinates that may cause blurring
                            float text_x = (float)cell_x + glyph_bitmap->x_offset;
                            float text_y = (float)cell_y + rend->font_ascent - glyph_bitmap->y_offset;

                            SDL_FRect dest_rect = {
                                text_x,
                                text_y,
                                (float)glyph_bitmap->width,
                                (float)glyph_bitmap->height
                            };
                            SDL_RenderTexture(rend->renderer, texture, NULL, &dest_rect);
                        }
                        SDL_DestroyTexture(texture);
                    }
                    // Free the glyph bitmap
                    rend->font_backend->free_glyph_bitmap(rend->font_backend, glyph_bitmap);
                }
            }

            // Render cursor if this is the cursor position
            // Note: This is a simplified cursor implementation
            // In a full implementation, you'd want to get the actual cursor position from the terminal
            if (row == cursor_pos.row && col == cursor_pos.col) {
                SDL_FRect cursor_rect = {
                    col * rend->cell_width,
                    row * rend->cell_height,
                    rend->cell_width,
                    rend->cell_height
                };
                SDL_SetRenderDrawColor(rend->renderer, r, g, b, 255); // Invert cursor color
                SDL_RenderRect(rend->renderer, &cursor_rect);
            }
        }
    }

    // Render debug grid if enabled
    if (rend->debug_grid) {
        vlog("DEBUG GRID: *** GRID RENDERING STARTED ***\n");
        vlog("DEBUG GRID: Rendering debug grid with display_cols=%d, display_rows=%d\n", display_cols, display_rows);
        vlog("DEBUG GRID: Renderer dimensions: %dx%d\n", rend->width, rend->height);
        vlog("DEBUG GRID: Cell dimensions: %dx%d\n", rend->cell_width, rend->cell_height);
        vlog("DEBUG GRID: debug_grid flag is %d\n", rend->debug_grid);
        // Draw a more visible grid by using a brighter color and slightly thicker lines
        SDL_SetRenderDrawColor(rend->renderer, 255, 255, 255, 200); // Slightly more opaque
        SDL_SetRenderDrawBlendMode(rend->renderer, SDL_BLENDMODE_BLEND);
        // Draw vertical lines
        for (int col = 0; col <= display_cols; col++) {
            SDL_FRect vline = { (float)(col * rend->cell_width), 0.0f, 2.0f, (float)rend->height };
            SDL_RenderFillRect(rend->renderer, &vline);
        }
        // Draw horizontal lines
        for (int row = 0; row <= display_rows; row++) {
            SDL_FRect hline = { 0.0f, (float)(row * rend->cell_height), (float)rend->width, 2.0f };
            SDL_RenderFillRect(rend->renderer, &hline);
        }
        SDL_SetRenderDrawBlendMode(rend->renderer, SDL_BLENDMODE_NONE);
        vlog("DEBUG GRID: *** GRID RENDERING COMPLETED ***\n");
    }
}

void renderer_present(Renderer *rend)
{
    SDL_RenderPresent(rend->renderer);
}

void renderer_toggle_debug_grid(Renderer *rend)
{
    if (rend) {
        rend->debug_grid = !rend->debug_grid;
        vlog("Debug grid toggled to %s\n", rend->debug_grid ? "ON" : "OFF");
        vlog("Current debug_grid value: %d\n", rend->debug_grid);
    }
}

void renderer_resize(Renderer *rend, int width, int height)
{
    rend->width = width;
    rend->height = height;
}
