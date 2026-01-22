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