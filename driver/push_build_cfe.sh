#!/bin/bash
# push_build_cfe.sh
# Push edited cfe source to the Pi, rebuild the module, install, and reboot.
#
# Usage: ./push_build_cfe.sh          (build + install + reboot)
#        ./push_build_cfe.sh --no-reboot   (build + install only)

PI_HOST="opticsadm@192.168.20.166"
PI_SRC="~/rpi-linux/drivers/media/platform/raspberrypi/rp1_cfe"
PI_BUILD="~/rp1-cfe-build"
PI_MODULE="/lib/modules/\$(uname -r)/kernel/drivers/media/platform/raspberrypi/rp1_cfe/rp1-cfe.ko.xz"
LOCAL_DIR="/home/rickfrank/sanuwave/prototype/driver/rpi_cfe"

REBOOT=true
if [ "$1" = "--no-reboot" ]; then
    REBOOT=false
fi

set -e

# Verify local files exist
if [ ! -f "$LOCAL_DIR/cfe.c" ]; then
    echo "ERROR: $LOCAL_DIR/cfe.c not found. Run pull_cfe_from_pi.sh first."
    exit 1
fi

echo "=== Step 1: Push edited files to Pi ==="
scp "$LOCAL_DIR/cfe.c" "$PI_HOST:$PI_SRC/cfe.c"
if [ -f "$LOCAL_DIR/cfe.h" ]; then
    scp "$LOCAL_DIR/cfe.h" "$PI_HOST:$PI_SRC/cfe.h"
fi
echo "Files pushed."

echo ""
echo "=== Step 2: Copy to build directory, build, install ==="
ssh "$PI_HOST" bash -s << 'REMOTE_SCRIPT'
set -e

PI_SRC=~/rpi-linux/drivers/media/platform/raspberrypi/rp1_cfe
PI_BUILD=~/rp1-cfe-build
KMOD_DIR=/lib/modules/$(uname -r)/kernel/drivers/media/platform/raspberrypi/rp1_cfe

echo "--- Copying source to build directory ---"
cp $PI_SRC/*.c $PI_SRC/*.h $PI_BUILD/

echo "--- Building module ---"
cd $PI_BUILD
make clean
make

echo "--- Verifying SANUWAVE strings in binary ---"
if strings rp1-cfe.ko | grep -q SANUWAVE; then
    echo "OK: SANUWAVE strings found in rp1-cfe.ko"
else
    echo "WARNING: No SANUWAVE strings found — patch may not be applied"
fi

echo "--- Compressing and installing ---"
xz -f -k rp1-cfe.ko
sudo cp rp1-cfe.ko.xz $KMOD_DIR/rp1-cfe.ko.xz
sudo depmod -a

echo ""
echo "=== Module installed successfully ==="
REMOTE_SCRIPT

if [ "$REBOOT" = true ]; then
    echo ""
    echo "=== Step 3: Rebooting Pi ==="
    ssh "$PI_HOST" "sudo reboot" || true
    echo "Pi is rebooting. Wait ~30 seconds, then reconnect."
    echo ""
    echo "After reboot, verify with:"
    echo "  ssh $PI_HOST 'sudo dmesg -n 7 && dmesg | grep SANUWAVE'"
else
    echo ""
    echo "=== Skipping reboot (--no-reboot) ==="
    echo "Run 'ssh $PI_HOST sudo reboot' when ready."
fi
