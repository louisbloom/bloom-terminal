#ifndef FONT_RESOLVER_H
#define FONT_RESOLVER_H

#include <stdbool.h>

// Font resolution types
typedef enum
{
    FONT_TYPE_NORMAL, // Primary text font (monospace/terminal)
    FONT_TYPE_BOLD,   // Bold monospace font
    FONT_TYPE_EMOJI   // Color emoji font
} FontType;

// Font resolution result
typedef struct
{
    char *font_path;   // Resolved font file path
    char *family_name; // Matched font family name
    float size;        // Font size

    // Font rendering options
    bool antialias;
    int hinting;
    int hint_style;
    int subpixel_order;
    int lcd_filter;
    int ft_load_flags; // FreeType load flags
} FontResolutionResult;

// Font resolver functions
int font_resolver_init();

// Resolve a specific font type with fallback strategies
int font_resolver_find_font(
    FontType type,               // Font type to resolve
    FontResolutionResult *result // Output resolution result
);

// Free resolution result
void font_resolver_free_result(FontResolutionResult *result);

// Cleanup font resolver
void font_resolver_cleanup();

#endif // FONT_RESOLVER_H