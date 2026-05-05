#!/bin/bash
# pull_cfe_from_pi.sh
# Pull the rp1-cfe driver source from the Pi to the local editing directory.
#
# Usage: ./pull_cfe_from_pi.sh

PI_HOST="opticsadm@192.168.20.166"
PI_SRC="~/rpi-linux/drivers/media/platform/raspberrypi/rp1_cfe"
LOCAL_DIR="/home/rickfrank/sanuwave/prototype/driver/rpi_cfe"

set -e

mkdir -p "$LOCAL_DIR"

echo "Pulling cfe source files from Pi..."
scp "$PI_HOST:$PI_SRC/cfe.c"   "$LOCAL_DIR/cfe.c"
scp "$PI_HOST:$PI_SRC/cfe.h"   "$LOCAL_DIR/cfe.h"

echo ""
echo "Files saved to $LOCAL_DIR:"
ls -la "$LOCAL_DIR/cfe.c" "$LOCAL_DIR/cfe.h"
echo ""
echo "Edit these files, then run ./push_build_cfe.sh to deploy."
