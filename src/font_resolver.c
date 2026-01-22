#include "font_resolver.h"
#include "common.h"
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdlib.h>
#include <string.h>

// Forward declarations
static char *find_font_with_pattern(const char *pattern, char **family_name);
static int query_font_options(const char *pattern, bool *antialias, int *hinting, int *hint_style,
                              int *subpixel_order, int *lcd_filter, int *ft_load_flags);

// Global fontconfig configuration
static FcConfig *fc_config = NULL;

// Helper function to find font using fontconfig
static char *find_font_with_pattern(const char *pattern, char **family_name)
{
    FcPattern *pat;
    char *font_path = NULL;

    vlog("Attempting to find font with pattern: '%s'\n", pattern);

    pat = FcNameParse((FcChar8 *)pattern);
    if (!pat) {
        vlog("Failed to parse font pattern: '%s'\n", pattern);
        return NULL;
    }

    FcConfigSubstitute(fc_config, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult result;
    FcPattern *match = FcFontMatch(fc_config, pat, &result);
    if (match) {
        FcChar8 *file;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch) {
            font_path = strdup((char *)file);
            vlog("Found font file: '%s'\n", font_path);

            // Get family name if requested
            if (family_name) {
                FcChar8 *family;
                if (FcPatternGetString(match, FC_FAMILY, 0, &family) == FcResultMatch) {
                    *family_name = strdup((char *)family);
                } else {
                    *family_name = strdup(pattern);
                }
            }
        } else {
            vlog("No font file found for pattern: '%s'\n", pattern);
        }
        FcPatternDestroy(match);
    } else {
        vlog("No matching font found for pattern: '%s'\n", pattern);
    }

    FcPatternDestroy(pat);

    return font_path;
}

// Helper function to query font rendering options from Fontconfig
static int query_font_options(const char *pattern, bool *antialias, int *hinting, int *hint_style,
                              int *subpixel_order, int *lcd_filter, int *ft_load_flags)
{
    FcPattern *pat;
    FcPattern *match;
    FcResult result;
    int value;

    vlog("Querying font options for pattern: '%s'\n", pattern);

    pat = FcNameParse((FcChar8 *)pattern);
    if (!pat) {
        vlog("Failed to parse font pattern: '%s'\n", pattern);
        return -1;
    }

    FcConfigSubstitute(fc_config, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    match = FcFontMatch(fc_config, pat, &result);
    if (!match) {
        vlog("No matching font found for pattern: '%s'\n", pattern);
        FcPatternDestroy(pat);
        return -1;
    }

    // Query antialiasing
    if (FcPatternGetBool(match, FC_ANTIALIAS, 0, (FcBool *)&value) == FcResultMatch) {
        *antialias = (value != 0);
        vlog("Font antialias: %s\n", *antialias ? "true" : "false");
    } else {
        *antialias = true; // Default to true
        vlog("Font antialias: default (true)\n");
    }

    // Query hinting
    if (FcPatternGetBool(match, FC_HINTING, 0, (FcBool *)&value) == FcResultMatch) {
        *hinting = value;
        vlog("Font hinting: %s\n", *hinting ? "true" : "false");
    } else {
        *hinting = true; // Default to true
        vlog("Font hinting: default (true)\n");
    }

    // Query hint style
    if (FcPatternGetInteger(match, FC_HINT_STYLE, 0, &value) == FcResultMatch) {
        *hint_style = value;
        vlog("Font hint style: %d\n", *hint_style);
    } else {
        *hint_style = FC_HINT_SLIGHT; // Default to slight
        vlog("Font hint style: default (slight)\n");
    }

    // Query subpixel order
    if (FcPatternGetInteger(match, FC_RGBA, 0, &value) == FcResultMatch) {
        *subpixel_order = value;
        vlog("Font subpixel order: %d\n", *subpixel_order);
    } else {
        *subpixel_order = FC_RGBA_NONE; // Default to none
        vlog("Font subpixel order: default (none)\n");
    }

    // Query LCD filter
    if (FcPatternGetInteger(match, FC_LCD_FILTER, 0, &value) == FcResultMatch) {
        *lcd_filter = value;
        vlog("Font LCD filter: %d\n", *lcd_filter);
    } else {
        *lcd_filter = FC_LCD_DEFAULT; // Default to default
        vlog("Font LCD filter: default (default)\n");
    }

    // Set FreeType load flags based on the options
    *ft_load_flags = 0;
    if (*hinting) {
        switch (*hint_style) {
        case FC_HINT_NONE:
            *ft_load_flags |= FT_LOAD_NO_HINTING;
            break;
        case FC_HINT_SLIGHT:
            *ft_load_flags |= FT_LOAD_TARGET_LIGHT;
            break;
        case FC_HINT_MEDIUM:
            *ft_load_flags |= FT_LOAD_TARGET_NORMAL;
            break;
        case FC_HINT_FULL:
            *ft_load_flags |= FT_LOAD_TARGET_NORMAL;
            break;
        default:
            *ft_load_flags |= FT_LOAD_TARGET_NORMAL;
            break;
        }
    } else {
        *ft_load_flags |= FT_LOAD_NO_HINTING;
    }

    // Add force autohint if requested
    FcBool autohint;
    if (FcPatternGetBool(match, FC_AUTOHINT, 0, &autohint) == FcResultMatch && autohint) {
        *ft_load_flags |= FT_LOAD_FORCE_AUTOHINT;
        vlog("Font force autohint: true\n");
    }

    vlog("Font FreeType load flags: 0x%x\n", *ft_load_flags);

    FcPatternDestroy(match);
    FcPatternDestroy(pat);

    return 0;
}

int font_resolver_init()
{
    // Initialize fontconfig
    FcBool result = FcInit();
    if (!result) {
        vlog("Failed to initialize fontconfig\n");
        return -1;
    }

    // Get the default config
    fc_config = FcConfigGetCurrent();
    if (!fc_config) {
        vlog("Failed to get fontconfig current config\n");
        return -1;
    }

    return 0;
}

int font_resolver_find_font(FontType type, FontResolutionResult *result)
{
    if (!result) {
        return -1;
    }

    // Initialize result
    result->font_path = NULL;
    result->family_name = NULL;
    result->size = 24.0f; // Default size

    // Initialize font options with defaults
    result->antialias = true;
    result->hinting = true;
    result->hint_style = FC_HINT_SLIGHT;
    result->subpixel_order = FC_RGBA_NONE;
    result->lcd_filter = FC_LCD_DEFAULT;
    result->ft_load_flags = FT_LOAD_TARGET_LIGHT;

    const char *pattern = NULL;
    switch (type) {
    case FONT_TYPE_NORMAL:
        pattern = "monospace";
        break;
    case FONT_TYPE_BOLD:
        pattern = "monospace:weight=bold";
        break;
    case FONT_TYPE_EMOJI:
        pattern = "emoji";
        break;
    default:
        vlog("Invalid font type requested\n");
        return -1;
    }

    // Query font rendering options
    query_font_options(pattern, &result->antialias, &result->hinting, &result->hint_style,
                       &result->subpixel_order, &result->lcd_filter, &result->ft_load_flags);

    char *font_path = NULL;
    char *family_name = NULL;

    font_path = find_font_with_pattern(pattern, &family_name);
    if (!font_path) {
        vlog("Failed to find font for pattern: %s\n", pattern);
        return -1;
    }

    result->font_path = font_path;
    result->family_name = family_name;
    return 0;
}

void font_resolver_free_result(FontResolutionResult *result)
{
    if (result) {
        if (result->font_path) {
            free(result->font_path);
            result->font_path = NULL;
        }
        if (result->family_name) {
            free(result->family_name);
            result->family_name = NULL;
        }
    }
}

void font_resolver_cleanup()
{
    if (fc_config) {
        FcFini();
        fc_config = NULL;
    }
}
