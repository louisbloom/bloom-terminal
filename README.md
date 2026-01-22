# vterm-sdl3 Terminal Emulator

A terminal emulator using libvterm for emulation and SDL3 for hardware-accelerated rendering.

## Features

- Accurate terminal emulation with libvterm
- Hardware-accelerated SDL3 rendering
- Fontconfig-based font management
- Keyboard input and window resizing
- Multiple font variant support

## Quick Start

### Dependencies
- libvterm
- SDL3
- freetype2
- fontconfig

### Build
```bash
autoreconf -i
./configure
make
```

### Usage
```bash
./src/vterm-sdl3 [OPTIONS] [INPUT_FILE]
```

Options:
- `-h`: Show help
- `-v`: Verbose output
- `INPUT_FILE`: Terminal input file
- `-`: Read from stdin

### Build Tools
For building the project, use `build.sh`:
```bash
./build.sh [OPTIONS]
```

Build options:
- `--install-deps`: Install dependencies
- `--no-debug`: Disable debug build
- `--prefix=PATH`: Set install path
- `--run`: Run after building
- `-y/--yes`: Auto-confirm prompts
- `--compdb`: Generate compile_commands.json

### LSP Support
Generate `compile_commands.json` for language server integration:
```bash
./build.sh --compdb
```
Requires [bear](https://github.com/rizsotto/Bear) installed.

Note: You must have a clean build directory for accurate results. Use `rm -rf build` before generating.