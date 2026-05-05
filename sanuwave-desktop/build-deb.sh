#!/bin/bash
#
# Build script for Sanuwave Client .deb package
#
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log()   { echo -e "${GREEN}[BUILD]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

INSTALL_DEPS=0
VERBOSE=0
CLEAN=0

while [[ $# -gt 0 ]]; do
    case $1 in
        --install-deps) INSTALL_DEPS=1; shift ;;
        --verbose|-v)   VERBOSE=1; shift ;;
        --clean)        CLEAN=1; shift ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --install-deps  Install build dependencies first"
            echo "  --verbose, -v   Enable verbose output"
            echo "  --clean         Clean build artifacts before building"
            echo "  --help, -h      Show this help"
            exit 0
            ;;
        *) error "Unknown option: $1" ;;
    esac
done

[[ $VERBOSE -eq 1 ]] && set -x

log "Checking build tools..."
command -v dpkg-buildpackage &> /dev/null || error "Missing dpkg-buildpackage. Install: sudo apt install dpkg-dev"
command -v cmake &> /dev/null || error "Missing cmake"
command -v g++ &> /dev/null || error "Missing g++"
dpkg -l debhelper &> /dev/null || error "Missing debhelper. Install: sudo apt install debhelper"

if [[ $INSTALL_DEPS -eq 1 ]]; then
    log "Installing build dependencies..."
    sudo apt update
    sudo apt install -y \
        build-essential \
        cmake \
        debhelper \
        devscripts \
        dpkg-dev \
        fakeroot \
        qt6-base-dev \
        libgl1-mesa-dev \
        libturbojpeg0-dev \
        libceres-dev \
        libeigen3-dev \
        libboost-all-dev \
        libflann-dev \
        libfreeimage-dev \
        libmetis-dev \
        libgoogle-glog-dev \
        libgflags-dev \
        libsqlite3-dev \
        libglew-dev \
        libcgal-dev
fi

if [[ $CLEAN -eq 1 ]]; then
    log "Cleaning previous build artifacts..."
    rm -rf debian/.debhelper debian/sanuwave-client debian/tmp
    rm -f debian/files debian/*.substvars debian/*.debhelper.log
    rm -rf obj-*
    rm -f ../sanuwave-client_*.deb ../sanuwave-client_*.changes ../sanuwave-client_*.buildinfo
fi

log "Verifying debian directory structure..."
[[ -d "debian" ]] || error "debian/ directory not found"

for file in control rules changelog copyright; do
    [[ -f "debian/$file" ]] || error "Missing debian/$file"
done

chmod +x debian/rules
[[ -x debian/postinst ]] && chmod +x debian/postinst
[[ -x debian/postrm ]] && chmod +x debian/postrm

mkdir -p debian/source
[[ -f "debian/source/format" ]] || echo "3.0 (native)" > debian/source/format

log "Building .deb package..."
if [[ $VERBOSE -eq 1 ]]; then
    dpkg-buildpackage -us -uc -b --build=binary
else
    dpkg-buildpackage -us -uc -b --build=binary 2>&1 | tee build.log
fi

mv ../sanuwave-client_*.deb . 2>/dev/null || true

DEB_FILE=$(ls sanuwave-client_*.deb 2>/dev/null | head -1)
if [[ -n "$DEB_FILE" ]]; then
    log "Build complete!"
    echo ""
    echo "========================================"
    echo "  Package: $DEB_FILE"
    echo "  Size:    $(du -h "$DEB_FILE" | cut -f1)"
    echo ""
    echo "  Install with:"
    echo "    sudo dpkg -i $DEB_FILE"
    echo "    sudo apt install -f"
    echo ""
    echo "  Verbose install:"
    echo "    sudo DEBUG=1 dpkg -i $DEB_FILE"
    echo "========================================"
    
    if [[ $VERBOSE -eq 1 ]]; then
        echo ""
        log "Package contents:"
        dpkg-deb -c "$DEB_FILE"
    fi
else
    error "Build failed - no .deb file produced"
fi
