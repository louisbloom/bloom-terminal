# vterm-sdl3 Build Guide

## Prerequisites (Fedora)

```bash
sudo dnf install -y autoconf automake libtool pkg-config gcc make \
    libvterm-devel SDL3-devel fontconfig-devel
```

## Quick Build with build.sh

```bash
# Make the build script executable
chmod +x build.sh

# Run the build script (automatically handles all steps)
./build.sh

# For a debug build
./build.sh --no-debug

# For a build with compile_commands.json generation
./build.sh --bear

# To install only (skip build and run)
./build.sh --install

# To install to a custom prefix
./build.sh --prefix=/usr/local
```

## Build Script Options

- `--install`: Only install the project (skip build and run)
- `--bear`: Generate compile_commands.json using bear
- `--no-debug`: Disable debug build
- `--prefix=PATH`: Set installation prefix (default: $HOME/.local)
- `--help`: Show help message

## Manual Build Process

If you prefer to build manually without using build.sh:

```bash
# Generate build files
autoreconf -fvi

# Build
mkdir build && cd build
../configure --enable-debug
make -j$(nproc)

# Run
./src/vterm-sdl3
```

## Configuration Options

```bash
../configure \
    --prefix=/usr/local \
    --enable-debug
```

## Development

```bash
# Clean and rebuild
make clean && make -j$(nproc) && make check

# Debug build
../configure --enable-debug
make && gdb --args ./src/vterm-sdl3
```
