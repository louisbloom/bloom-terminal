#include "common.h"
#include "renderer.h"
#include "terminal.h"
#include <SDL3/SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WINDOW_WIDTH  800
#define WINDOW_HEIGHT 600

/* Global verbose flag - controls debug output */
int verbose = 0;

// Function prototypes
static void print_usage(const char *progname);
static void process_input_from_source(Terminal *term, const char *source);

int main(int argc, char *argv[])
{
    Terminal *term;
    Renderer *rend = NULL;
    SDL_Window *window = NULL;
    SDL_Renderer *sdl_rend = NULL;
    int running = 1;
    SDL_Event event;
    char *input_source = NULL;
    int opt;

    // Parse command line arguments
    int debug_grid_enabled = 0;
    while ((opt = getopt(argc, argv, "hved")) != -1) {
        switch (opt) {
        case 'h':
            print_usage(argv[0]);
            return 0;
        case 'v':
            verbose = 1;
            break;
        case 'e':
            running = 0; // Set running to false to exit event loop
            fprintf(stderr, "STATUS: exit_flag_set=1\n");
            break;
        case 'd':
            debug_grid_enabled = 1;
            fprintf(stderr, "STATUS: debug grid enabled via CLI flag\n");
            break;
        case '?':
            fprintf(stderr, "ERROR: Unknown option: -%c\n", optopt);
            print_usage(argv[0]);
            return 1;
        }
    }

    // Check for input source argument
    if (optind < argc) {
        input_source = argv[optind];
    }

    // Set app metadata before SDL initialization as recommended by SDL3
    if (verbose) {
        fprintf(stderr, "DEBUG: Setting SDL app metadata\n");
    }
    if (!SDL_SetAppMetadata("vterm-sdl3", "1.0.0", "org.vterm.sdl3")) {
        fprintf(stderr, "WARNING: Failed to set SDL app metadata: %s\n", SDL_GetError());
    }

    // Print SDL version info if verbose
    if (verbose) {
        int sdl_version = SDL_GetVersion();
        fprintf(stderr, "DEBUG: SDL version %d.%d.%d\n",
                SDL_VERSIONNUM_MAJOR(sdl_version),
                SDL_VERSIONNUM_MINOR(sdl_version),
                SDL_VERSIONNUM_MICRO(sdl_version));
    }

    // Initialize SDL with verbose logging
    if (verbose) {
        fprintf(stderr, "DEBUG: Initializing SDL video subsystem\n");
        fprintf(stderr, "DEBUG: DISPLAY=%s\n", getenv("DISPLAY") ? getenv("DISPLAY") : "(not set)");
        fprintf(stderr, "DEBUG: WAYLAND_DISPLAY=%s\n", getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "(not set)");
    }

    // Clear any previous errors before attempting initialization
    SDL_ClearError();

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        const char *error = SDL_GetError();
        fprintf(stderr, "ERROR: Failed to initialize SDL video subsystem\n");

        // Check if SDL provided a specific error message
        if (error && error[0] != '\0') {
            fprintf(stderr, "ERROR: SDL_GetError() returned: '%s'\n", error);
        } else {
            fprintf(stderr, "ERROR: No specific error message from SDL\n");
        }

        fprintf(stderr, "ERROR: This could be due to:\n");
        fprintf(stderr, "ERROR: 1. Missing SDL3 runtime libraries\n");
        fprintf(stderr, "ERROR: 2. No display available (DISPLAY environment variable)\n");
        fprintf(stderr, "ERROR: 3. SDL3 driver issues\n");

        return 1;
    } else {
        if (verbose) {
            fprintf(stderr, "DEBUG: SDL initialized successfully\n");
        }
    }

    // FreeType is initialized in renderer_init, not here
    vlog("FreeType will be initialized in renderer\n");

    // Initialize terminal
    term = terminal_init(WINDOW_WIDTH, WINDOW_HEIGHT);
    if (!term) {
        fprintf(stderr, "Failed to initialize terminal\n");
        SDL_Quit();
        return 1;
    }

    // Only create window and renderer if we're going to run the event loop
    if (running) {
        // Create window with flags
        vlog("Creating window with dimensions %dx%d\n", WINDOW_WIDTH, WINDOW_HEIGHT);

        // Clear errors before window creation
        SDL_ClearError();

        Uint32 window_flags = SDL_WINDOW_RESIZABLE;
        window = SDL_CreateWindow("vterm-sdl3 Terminal", WINDOW_WIDTH, WINDOW_HEIGHT, window_flags);
        if (!window) {
            const char *error = SDL_GetError();
            if (error && error[0] != '\0') {
                fprintf(stderr, "ERROR: Failed to create window: %s\n", error);
            } else {
                fprintf(stderr, "ERROR: Failed to create window (no specific error message)\n");
            }
            terminal_destroy(term);
            SDL_Quit();
            return 1;
        }
        vlog("Window created successfully\n");

        // Create renderer with flags
        vlog("Creating renderer\n");

        // Clear errors before renderer creation
        SDL_ClearError();

        sdl_rend = SDL_CreateRenderer(window, NULL);
        if (!sdl_rend) {
            const char *error = SDL_GetError();
            if (error && error[0] != '\0') {
                fprintf(stderr, "ERROR: Failed to create renderer: %s\n", error);
            } else {
                fprintf(stderr, "ERROR: Failed to create renderer (no specific error message)\n");
            }
            SDL_DestroyWindow(window);
            terminal_destroy(term);
            SDL_Quit();
            return 1;
        }
        vlog("Renderer created successfully\n");

        // Initialize renderer
        rend = renderer_init(sdl_rend, window);
        if (!rend) {
            fprintf(stderr, "Failed to initialize renderer\n");
            terminal_destroy(term);
            SDL_DestroyRenderer(sdl_rend);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

        // Set the VTermState for color conversion
        rend->state = vterm_obtain_state(term->vt);

        // Set initial width/height in renderer
        rend->width = WINDOW_WIDTH;
        rend->height = WINDOW_HEIGHT;

        // Load fonts
        if (renderer_load_fonts(rend) < 0) {
            fprintf(stderr, "Failed to load fonts\n");
            renderer_destroy(rend);
            terminal_destroy(term);
            SDL_DestroyRenderer(sdl_rend);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
    }

    // Process input if provided
    if (input_source) {
        vlog("Processing input from: %s\n",
             strcmp(input_source, "-") == 0 ? "stdin" : input_source);
        process_input_from_source(term, input_source);
    }

    // Only set initial redraw flag and enter event loop if running
    if (running) {
        // Set initial redraw flag to ensure first render
        term->need_redraw = 1;

        // Enable debug grid if requested via CLI
        if (debug_grid_enabled && rend) {
            renderer_toggle_debug_grid(rend);
            vlog("Debug grid enabled via CLI flag\n");
        }

        // Main event loop
        while (running) {
            // Process events
            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = 0;
                    break;

                case SDL_EVENT_KEY_DOWN:
                    // Handle key presses and send to terminal
                    {
                        char key_buffer[16] = { 0 };
                        size_t len = 0;

                        // Convert SDL key events to terminal input
                        switch (event.key.key) {
                        case SDLK_RETURN:
                            vlog("ENTER key pressed, sending CRLF (%d bytes)\n", 2);
                            key_buffer[0] = '\r';
                            key_buffer[1] = '\n';
                            len = 2;
                            break;
                        case SDLK_BACKSPACE:
                            key_buffer[0] = '\x7f'; // DEL character
                            len = 1;
                            break;
                        case SDLK_ESCAPE:
                            key_buffer[0] = '\x1b'; // ESC character
                            len = 1;
                            break;
                        case SDLK_UP:
                            strcpy(key_buffer, "\x1b[A");
                            len = 3;
                            break;
                        case SDLK_DOWN:
                            strcpy(key_buffer, "\x1b[B");
                            len = 3;
                            break;
                        case SDLK_RIGHT:
                            strcpy(key_buffer, "\x1b[C");
                            len = 3;
                            break;
                        case SDLK_LEFT:
                            strcpy(key_buffer, "\x1b[D");
                            len = 3;
                            break;
                        default:
                            // Handle printable characters
                            if (event.key.key >= 32 && event.key.key < 127) {
                                // Check for Ctrl+G specifically
                                if (event.key.key == 'g' || event.key.key == 'G') {
                                    if (event.key.mod & SDL_KMOD_CTRL) {
                                        if (rend) {
                                            renderer_toggle_debug_grid(rend);
                                            // Force a redraw when debug grid is toggled
                                            term->need_redraw = 1;
                                        }
                                        // Don't send to terminal
                                        len = 0;
                                        vlog("Ctrl+G pressed, debug grid toggled\n");
                                    } else {
                                        key_buffer[0] = (char)event.key.key;
                                        len = 1;
                                    }
                                } else {
                                    key_buffer[0] = (char)event.key.key;
                                    len = 1;
                                }
                            }
                            break;
                        }

                        if (len > 0) {
                            terminal_process_input(term, key_buffer, len);
                        }
                    }
                    break;

                case SDL_EVENT_WINDOW_RESIZED:
                    // Handle window resize
                    terminal_resize(term, event.window.data1, event.window.data2);
                    renderer_resize(rend, event.window.data1, event.window.data2);
                    break;
                }
            }

            // Render terminal only if needed
            if (term->need_redraw) {
                renderer_draw_terminal(rend, term);
                SDL_RenderPresent(sdl_rend);
                term->need_redraw = 0; // Clear the flag
            }

            // Small delay to prevent excessive CPU usage
            SDL_Delay(16); // ~60 FPS
        }
    }

    // Cleanup
    if (rend)
        renderer_destroy(rend);
    if (sdl_rend)
        SDL_DestroyRenderer(sdl_rend);
    if (window)
        SDL_DestroyWindow(window);
    terminal_destroy(term);
    SDL_Quit();

    return 0;
}

void vlog(const char *format, ...)
{
    if (!verbose)
        return;

    va_list args;
    va_start(args, format);
    fprintf(stderr, "DEBUG: ");
    vfprintf(stderr, format, args);
    va_end(args);
}

static void print_usage(const char *progname)
{
    printf("Usage: %s [OPTIONS] [INPUT_FILE]\n", progname);
    printf("Terminal emulator using libvterm and SDL3\n\n");
    printf("Options:\n");
    printf("  -h          Show this help message\n");
    printf("  -v          Verbose output (debug information)\n");
    printf("  -e          Exit immediately (for testing)\n");
    printf("  -d          Enable debug grid (for testing)\n");
    printf("\n");
    printf("Input:\n");
    printf("  INPUT_FILE  File containing terminal input to process\n");
    printf("  -           Read terminal input from stdin\n");
    printf("  (none)      Run interactively without pre-loaded input\n");
}

static void process_input_from_source(Terminal *term, const char *source)
{
    FILE *input_file;
    char buffer[4096];
    size_t bytes_read;
    size_t total_bytes = 0;

    if (strcmp(source, "-") == 0) {
        input_file = stdin;
        vlog("Reading from stdin\n");
    } else {
        input_file = fopen(source, "r");
        if (!input_file) {
            fprintf(stderr, "ERROR: Cannot open file '%s': %s\n",
                    source, strerror(errno));
            return;
        }
        vlog("Opened file: %s\n", source);
    }

    // Read and process input
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), input_file)) > 0) {
        terminal_process_input(term, buffer, bytes_read);
        total_bytes += bytes_read;

        vlog("Processed %zu bytes (total: %zu)\n",
             bytes_read, total_bytes);
    }

    if (input_file != stdin) {
        fclose(input_file);
    }

    // Output machine-readable status
    fprintf(stderr, "STATUS: input_bytes=%zu\n", total_bytes);
    fprintf(stderr, "STATUS: input_source=%s\n",
            strcmp(source, "-") == 0 ? "stdin" : source);

    // Get terminal dimensions for debug output
    int rows, cols;
    if (terminal_get_dimensions(term, &rows, &cols) == 0) {
        fprintf(stderr, "STATUS: terminal_dimensions=%dx%d\n", cols, rows);
    }
}
