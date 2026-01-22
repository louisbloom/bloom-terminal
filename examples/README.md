# vterm-sdl3 Test Examples

This directory contains various test files for testing vterm-sdl3 terminal emulator features.

## How to Use

1. Build vterm-sdl3:
   ```bash
   ./build.sh -y
   ```

2. Run a test:
   ```bash
   # Basic test
   cat examples/basic/hello.txt | build/src/vterm-sdl3 -v -
   
   # Color test
   cat examples/basic/colors.txt | build/src/vterm-sdl3 -v -
   
   # All-in-one test
   cat examples/all_in_one.txt | build/src/vterm-sdl3 -v -
   ```

3. For tests that require timing (like scrolling or rapid updates):
   ```bash
   # Use a small delay between lines
   while IFS= read -r line; do
       echo "$line"
       sleep 0.01
   done < examples/advanced/scroll.txt | build/src/vterm-sdl3 -v -
   ```

4. For tests with interactive prompts (like altscreen):
   ```bash
   cat examples/advanced/altscreen.txt | build/src/vterm-sdl3 -v -
   ```

## Test Categories

### Basic Tests
- `hello.txt` - Simple text with colors and attributes
- `colors.txt` - Color palette tests (8, 16, 256, truecolor)
- `attributes.txt` - Text attributes (bold, italic, underline, etc.)
- `cursor.txt` - Cursor positioning and movement

### Advanced Tests
- `scroll.txt` - Terminal scrolling with many lines
- `altscreen.txt` - Alternate screen buffer
- `window_title.txt` - Window title setting
- `cursor_control.txt` - Cursor visibility and shape control

### Unicode Tests
- `box_drawing.txt` - Box drawing characters
- `emoji.txt` - Emoji characters
- `symbols.txt` - Special symbols (mathematical, currency, arrows)

### Performance Tests
- `large_output.txt` - Large amount of text output
- `rapid_updates.txt` - Rapid screen updates

### Comprehensive Test
- `all_in_one.txt` - All features combined in one test

## Notes

- Some tests may require specific font support (emoji, symbols)
- The `-v` flag enables verbose output for debugging
- Use `-e` flag to exit immediately after processing input
- For best results, ensure your terminal supports the features being tested

## Expected Behavior

1. **Colors**: Should display correct foreground and background colors
2. **Attributes**: Text should show appropriate styling (bold, italic, etc.)
3. **Cursor**: Cursor should move to correct positions
4. **Unicode**: Special characters should render correctly
5. **Performance**: Should handle large outputs and rapid updates smoothly
6. **Window Title**: Window title should update when specified

## Troubleshooting

If a test doesn't work as expected:
1. Check if vterm-sdl3 was built with debugging: `./build.sh -y`
2. Run with `-v` flag to see verbose output
3. Check the terminal capabilities in the source code
4. Verify that the ANSI escape sequences are supported by libvterm
