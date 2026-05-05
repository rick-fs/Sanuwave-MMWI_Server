#!/bin/bash
# sync-sysroot.sh - Sync Raspberry Pi filesystem to local sysroot for cross-compilation
#
# Usage: ./sync-sysroot.sh [raspberry_pi_address]
# Example: ./sync-sysroot.sh pi@192.168.1.100
#          ./sync-sysroot.sh pi@raspberrypi.local

set -e  # Exit on error

# Configuration
DEFAULT_RPI_HOST="pi@raspberrypi.local"
SYSROOT_PATH="/home/rickfrank/rpi-sysroot"

# Use provided host or default
RPI_HOST="${1:-$DEFAULT_RPI_HOST}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}======================================${NC}"
echo -e "${GREEN}Raspberry Pi Sysroot Sync${NC}"
echo -e "${GREEN}======================================${NC}"
echo ""
echo -e "Raspberry Pi: ${YELLOW}${RPI_HOST}${NC}"
echo -e "Sysroot Path: ${YELLOW}${SYSROOT_PATH}${NC}"
echo ""

# Check if sysroot directory exists
if [ ! -d "$SYSROOT_PATH" ]; then
    echo -e "${YELLOW}Sysroot directory does not exist. Creating...${NC}"
    mkdir -p "$SYSROOT_PATH"
fi

# Test SSH connection
echo -e "${YELLOW}Testing SSH connection...${NC}"
if ! ssh -o ConnectTimeout=5 -o BatchMode=yes "$RPI_HOST" exit 2>/dev/null; then
    echo -e "${RED}ERROR: Cannot connect to $RPI_HOST${NC}"
    echo "Please check:"
    echo "  1. The Raspberry Pi is powered on and connected to the network"
    echo "  2. SSH is enabled on the Raspberry Pi"
    echo "  3. You have SSH keys set up or can authenticate"
    echo "  4. The hostname/IP address is correct"
    exit 1
fi
echo -e "${GREEN}✓ SSH connection successful${NC}"
echo ""

# Sync the filesystem
echo -e "${YELLOW}Starting rsync (this may take a while on first run)...${NC}"
echo ""

rsync -avzh --rsync-path="rsync" \
    --progress \
    --delete \
    --exclude='usr/share/doc/*' \
    --exclude='usr/share/man/*' \
    --exclude='usr/share/locale/*' \
    --exclude='var/cache/*' \
    --exclude='var/log/*' \
    --exclude='tmp/*' \
    --exclude='home/*' \
    --exclude='root/*' \
    "$RPI_HOST":/{lib,usr,opt} \
    "$SYSROOT_PATH"/

echo ""
echo -e "${GREEN}======================================${NC}"
echo -e "${GREEN}✓ Sync complete!${NC}"
echo -e "${GREEN}======================================${NC}"
echo ""

# Verify TurboJPEG is present
echo -e "${YELLOW}Verifying TurboJPEG installation...${NC}"
if [ -f "$SYSROOT_PATH/usr/lib/aarch64-linux-gnu/libturbojpeg.so" ]; then
    echo -e "${GREEN}✓ Found: $SYSROOT_PATH/usr/lib/aarch64-linux-gnu/libturbojpeg.so${NC}"
elif [ -f "$SYSROOT_PATH/usr/lib/arm-linux-gnueabihf/libturbojpeg.so" ]; then
    echo -e "${GREEN}✓ Found: $SYSROOT_PATH/usr/lib/arm-linux-gnueabihf/libturbojpeg.so${NC}"
else
    echo -e "${YELLOW}⚠ TurboJPEG not found in sysroot${NC}"
    echo "You may need to install it on the Raspberry Pi:"
    echo "  ssh $RPI_HOST 'sudo apt install libturbojpeg0-dev'"
    echo "Then re-run this script."
fi

if [ -f "$SYSROOT_PATH/usr/include/turbojpeg.h" ]; then
    echo -e "${GREEN}✓ Found: $SYSROOT_PATH/usr/include/turbojpeg.h${NC}"
else
    echo -e "${YELLOW}⚠ TurboJPEG headers not found in sysroot${NC}"
fi

echo ""
echo "Sysroot is ready for cross-compilation!"
echo "You can now build with: cd build && cmake .. && make"
