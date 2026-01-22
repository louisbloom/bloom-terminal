# vterm-sdl3

A terminal emulator built with SDL3, libvterm, and FreeType.

## Features

- Full terminal emulation using libvterm
- Hardware-accelerated rendering with SDL3
- Font rendering with FreeType and Fontconfig
- Support for Unicode characters and emoji
- Configurable color schemes
- Proper terminal resize handling

## Building

The project uses GNU Autotools. For convenience, a build script is provided:

```bash
chmod +x build.sh
./build.sh
```

This will:

1. Generate the configure script
2. Configure the build with appropriate flags
3. Build the project with parallel jobs
4. Create a `build/` directory with the compiled binary

### Build Script Options

The build script supports several options:

```bash
./build.sh --help
```

Available options:

- `--install` - Only install the project (skip build and run)
- `--bear` - Generate compile_commands.json using bear
- `--no-debug` - Disable debug build
- `--format` - Format source files with clang-format, shfmt, and prettier
- `--prefix=PATH` - Set installation prefix (default: $HOME/.local)

## Usage

```bash
# Run the terminal emulator
build/src/vterm-sdl3

# Run with verbose output
build/src/vterm-sdl3 -v

# Process input from a file
build/src/vterm-sdl3 input.txt

# Process input from stdin
echo -e "\x1b[31mHello World\x1b[0m" | build/src/vterm-sdl3 -
```

## Dependencies

- libvterm
- SDL3
- fontconfig
- freetype2
- cairo
- cairo-ft

## Development

The project includes:

- `.clang-format` for code formatting
- `build.sh` for automated builds
- Example scripts demonstrating various terminal features
