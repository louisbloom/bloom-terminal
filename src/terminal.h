#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdint.h>
#include <vterm.h>

#define SCROLLBACK_SIZE 1000

// Abstract position type
typedef struct
{
    int row;
    int col;
} TerminalPos;

// Abstract cell attributes
typedef struct
{
    unsigned int bold : 1;
    unsigned int underline : 1;
    unsigned int italic : 1;
    unsigned int blink : 1;
    unsigned int reverse : 1;
    unsigned int strikethrough : 1;
    unsigned int font : 4;
    unsigned int dwl : 1;
    unsigned int dhl : 2;
} TerminalCellAttr;

// Abstract cell structure
typedef struct
{
    uint32_t chars[VTERM_MAX_CHARS_PER_CELL];
    TerminalCellAttr attrs;
    VTermColor fg;
    VTermColor bg;
} TerminalCell;

typedef struct
{
    VTerm *vt;
    VTermState *state;
    VTermScreen *screen;
    int width;
    int height;
    int need_redraw;
    VTermPos cursor_pos;
    char *title;
    VTermScreenCell **scrollback;
    int scrollback_lines;
    int scrollback_capacity;
} Terminal;

Terminal *terminal_init(int width, int height);
void terminal_destroy(Terminal *term);
void terminal_resize(Terminal *term, int width, int height);
int terminal_process_input(Terminal *term, const char *input, size_t len);
void terminal_render(Terminal *term);
int terminal_get_cell(Terminal *term, int row, int col, VTermScreenCell *cell);
int terminal_get_cell_abstract(Terminal *term, int row, int col, TerminalCell *cell);
int terminal_get_dimensions(Terminal *term, int *rows, int *cols);
TerminalPos terminal_get_cursor_pos(Terminal *term);
const char *terminal_get_title(Terminal *term);
void terminal_convert_color_to_rgb(const Terminal *term, const VTermColor *vcol, uint8_t *r, uint8_t *g, uint8_t *b);
int terminal_color_is_default_bg(const VTermColor *color);

#endif /* TERMINAL_H */
