# vterm-sdl3 Build Guide

## Prerequisites (Fedora)
```bash
sudo dnf install -y autoconf automake libtool pkg-config gcc make \
    libvterm-devel SDL3-devel fontconfig-devel
```

## Quick Build
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