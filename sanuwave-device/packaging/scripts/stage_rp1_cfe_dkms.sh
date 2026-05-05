#!/bin/bash
# stage_rp1_cfe_dkms.sh
#
# Vendors the stock kernel sources (cfe.c/.h, csi2.c/.h, dphy.c/.h,
# pisp_fe.c/.h, and related headers) from the local rpi-linux clone
# into packaging/dkms/rp1-cfe/, and regenerates the SANUWAVE patch from
# the clone's working-tree diff.
#
# This is a maintainer-only tool.  Other developers never run it — they
# consume the committed output from the sanuwave-imaging-server repo and
# build the .deb from there.  Run this whenever the kernel patch
# changes, then commit the resulting files.
#
# dkms.conf and Makefile in the destination are maintained in-tree and
# not regenerated here.
#
# Prereqs on the Ubuntu dev machine:
#   1. rpi-linux cloned locally (default: ~/rpi-linux).
#   2. Clone checked out at the commit matching the Pi where the patch
#      was developed.
#   3. The patched cfe.c in the clone's working tree — copy it from the
#      Pi via scp or keep Ubuntu's edits in lockstep with the Pi's.
#
# Usage (from anywhere; paths are resolved relative to the script):
#     ./packaging/scripts/stage_rp1_cfe_dkms.sh [path/to/rpi-linux]
#
# Env override:
#     RPI_LINUX — path to the rpi-linux clone (default: $HOME/rpi-linux)

set -euo pipefail

RPI_LINUX="${1:-${RPI_LINUX:-$HOME/rpi-linux}}"
CFE_SRC_DIR="${RPI_LINUX}/drivers/media/platform/raspberrypi/rp1_cfe"

# Resolve the destination relative to this script's location so the
# script works whether you run it from the repo root or from elsewhere.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)/dkms/rp1-cfe"

if [ ! -d "${CFE_SRC_DIR}" ]; then
    echo "ERROR: rp1-cfe source not found at ${CFE_SRC_DIR}" >&2
    echo "" >&2
    echo "Clone rpi-linux locally first:" >&2
    echo "    git clone --branch rpi-6.12.y \\" >&2
    echo "        https://github.com/raspberrypi/linux.git ${RPI_LINUX}" >&2
    echo "" >&2
    echo "Then check out the commit matching the Pi where the patch was" >&2
    echo "developed and copy the patched cfe.c from the Pi." >&2
    exit 1
fi

if [ ! -d "${DEST_DIR}" ]; then
    echo "ERROR: destination dir ${DEST_DIR} does not exist." >&2
    echo "Create it first and place dkms.conf + Makefile there." >&2
    exit 1
fi

# Fail loudly if the clone has unstaged changes outside cfe.c.  The
# patch we generate is "everything modified in the working tree relative
# to HEAD", so stray changes would get swept into the patch.
pushd "${RPI_LINUX}" > /dev/null
DIRTY=$(git status --porcelain drivers/media/platform/raspberrypi/rp1_cfe/ \
            | grep -v 'drivers/media/platform/raspberrypi/rp1_cfe/cfe\.c$' || true)
if [ -n "${DIRTY}" ]; then
    echo "ERROR: rpi-linux at ${RPI_LINUX} has unstaged changes outside cfe.c:" >&2
    echo "${DIRTY}" >&2
    echo "Commit, stash, or discard them before staging." >&2
    exit 1
fi
UPSTREAM_COMMIT=$(git rev-parse HEAD)
UPSTREAM_BRANCH=$(git rev-parse --abbrev-ref HEAD)
popd > /dev/null

echo "Staging rp1-cfe DKMS sources:"
echo "  rpi-linux path:   ${RPI_LINUX}"
echo "  rpi-linux commit: ${UPSTREAM_COMMIT}"
echo "  rpi-linux branch: ${UPSTREAM_BRANCH}"
echo "  destination:      ${DEST_DIR}"
echo ""

# Stock files the out-of-tree Makefile links against plus their
# transitive includes.  Verified against the upstream rp1_cfe/ directory.
STOCK_FILES=(
    cfe.c cfe.h cfe_fmts.h
    csi2.c csi2.h
    dphy.c dphy.h
    pisp_fe.c pisp_fe.h pisp_fe_config.h
    pisp_common.h pisp_statistics.h pisp_types.h
)

# Pull each file from HEAD (not the working tree) so cfe.c lands as
# unmodified upstream — the patch captures the working-tree changes
# separately.
pushd "${RPI_LINUX}" > /dev/null
for f in "${STOCK_FILES[@]}"; do
    git show "HEAD:drivers/media/platform/raspberrypi/rp1_cfe/${f}" \
        > "${DEST_DIR}/${f}"
    echo "    staged ${f}"
done
popd > /dev/null

# Regenerate the SANUWAVE patch so that -p1 applies it against the
# DKMS source directory.  git diff produces `a/drivers/.../cfe.c` paths
# which need too many strips.  We produce a patch with shorter paths
# that `patch -p1` can apply when run from the DKMS staging dir.
#
# DKMS looks for patches under <source>/patches/, not the source root,
# so we write there.
mkdir -p "${DEST_DIR}/patches"
pushd "${RPI_LINUX}" > /dev/null
git show HEAD:drivers/media/platform/raspberrypi/rp1_cfe/cfe.c > /tmp/cfe.c.stock.$$
diff -u --label "a/cfe.c" --label "b/cfe.c" \
    /tmp/cfe.c.stock.$$ \
    drivers/media/platform/raspberrypi/rp1_cfe/cfe.c \
    > "${DEST_DIR}/patches/rp1-cfe-sanuwave-strobe.patch" || true
rm -f /tmp/cfe.c.stock.$$
popd > /dev/null

# Remove any stale copy from the root (from previous staging runs that
# wrote the patch in the wrong place).
rm -f "${DEST_DIR}/rp1-cfe-sanuwave-strobe.patch"

PATCH_LINES=$(wc -l < "${DEST_DIR}/patches/rp1-cfe-sanuwave-strobe.patch")
echo "    staged patches/rp1-cfe-sanuwave-strobe.patch (${PATCH_LINES} lines)"

if [ "${PATCH_LINES}" -eq 0 ]; then
    echo "" >&2
    echo "WARNING: patch is empty — no working-tree changes in rpi-linux." >&2
    echo "         Did you copy the patched cfe.c from the Pi?" >&2
fi

# Stamp which upstream commit these sources came from, so we can
# always tell what kernel version the package targets.
cat > "${DEST_DIR}/UPSTREAM_COMMIT" <<EOF
rpi-linux commit: ${UPSTREAM_COMMIT}
rpi-linux branch: ${UPSTREAM_BRANCH}
staged from:      ${RPI_LINUX}
staged at:        $(date -u +%Y-%m-%dT%H:%M:%SZ)
EOF
echo "    staged UPSTREAM_COMMIT"

echo ""
echo "Done.  Review the staged files:"
echo "    git status packaging/dkms/rp1-cfe/"
echo ""
echo "Inspect the patch:"
echo "    cat packaging/dkms/rp1-cfe/rp1-cfe-sanuwave-strobe.patch"
echo ""
echo "If everything looks right, commit:"
echo "    git add packaging/dkms/rp1-cfe/"
echo "    git commit -m 'rp1-cfe: refresh DKMS sources from ${UPSTREAM_COMMIT:0:10}'"
