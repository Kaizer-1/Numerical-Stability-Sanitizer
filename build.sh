#!/usr/bin/env bash
set -euo pipefail

# Locate Homebrew LLVM (macOS) or fall back to system paths.
detect_llvm() {
    # Homebrew arm64
    if [ -d /opt/homebrew/opt/llvm ]; then
        echo /opt/homebrew/opt/llvm
        return
    fi
    # Homebrew x86
    if [ -d /usr/local/opt/llvm ]; then
        echo /usr/local/opt/llvm
        return
    fi
    # Linux system LLVM (llvm-config on PATH)
    if command -v llvm-config >/dev/null 2>&1; then
        llvm-config --prefix
        return
    fi
    echo ""
}

LLVM_PREFIX=$(detect_llvm)

if [ -z "$LLVM_PREFIX" ]; then
    echo "ERROR: Could not find LLVM. Install it with:"
    echo "  macOS:  brew install llvm"
    echo "  Ubuntu: sudo apt install llvm-dev clang"
    exit 1
fi

LLVM_CMAKE_DIR="$LLVM_PREFIX/lib/cmake/llvm"
CLANG="$LLVM_PREFIX/bin/clang"
CLANGXX="$LLVM_PREFIX/bin/clang++"

echo "Using LLVM at: $LLVM_PREFIX"

cmake -S . -B build \
    -DLLVM_DIR="$LLVM_CMAKE_DIR" \
    -DCMAKE_PREFIX_PATH="$LLVM_PREFIX" \
    -DCMAKE_C_COMPILER="$CLANG" \
    -DCMAKE_CXX_COMPILER="$CLANGXX" \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build -- -j"$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)"

echo ""
echo "Build complete. Compiler wrapper: build/numerical-clang"
