#!/bin/bash
# install_deb_and_verify.sh
#
# End-to-end validation of the sanuwave-imaging-server .deb package.
# Runs the full install/verify/uninstall cycle on the target Pi:
#
#     1. Capture pre-install system state (loaded rp1-cfe, DKMS registry,
#        modprobe configs, /usr/src entries).
#     2. apt install the .deb, watching postinst output.
#     3. Verify both DKMS modules built and installed into /updates/dkms/.
#     4. Verify the modprobe override file exists and has the expected
#        content.
#     5. Verify modinfo resolves rp1-cfe to the patched /updates/ copy.
#     6. Verify /etc/modprobe.d blacklist and DT overlay were installed.
#     7. Verify config.txt was patched.
#     8. Optionally reboot and, on return, verify the patched module is
#        actually loaded and the server finds both CFEs.
#     9. apt remove the package, verify rollback.
#
# This script does NOT rebuild the .deb.  Build it first (e.g. via
# dpkg-buildpackage) and pass the resulting file.
#
# Usage:
#     sudo ./install_deb_and_verify.sh path/to/sanuwave-imaging-server_<ver>_arm64.deb
#     sudo ./install_deb_and_verify.sh path/to/*.deb --reboot   # runs full reboot cycle
#     sudo ./install_deb_and_verify.sh path/to/*.deb --no-remove  # leave installed
#
# Exit codes:
#     0  — all checks passed
#     non-zero — first failing stage

set -e

DEB="${1:-}"
MODE="${2:-default}"   # default | --reboot | --no-remove

CFE_NAME="rp1-cfe-sanuwave"
CFE_VERSION="1.0"
DW9714_NAME="dw9714"
DW9714_VERSION="1.0"
KVER="$(uname -r)"

MODPROBE_FILE="/etc/modprobe.d/rp1-cfe-sanuwave.conf"
BLACKLIST_FILE="/etc/modprobe.d/ad5398-blacklist.conf"
OVERLAY_NAME="imx219-b0190-vcm"
OVERLAY_FILE="/boot/firmware/overlays/${OVERLAY_NAME}.dtbo"
CONFIG="/boot/firmware/config.txt"

# Artifact of the run — grows as stages pass.  Makes post-mortem easier
# if something fails.
LOG_DIR="/tmp/sanuwave-deb-test-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "${LOG_DIR}"

pass() { echo "  ✓ $*"; }
fail() { echo "  ✗ $*" >&2; echo "    logs: ${LOG_DIR}" >&2; exit 1; }
step() { echo ""; echo "══ $* ══"; }
info() { echo "    $*"; }

if [ "$(id -u)" -ne 0 ]; then
    fail "run as root (sudo)"
fi

if [ -z "${DEB}" ] || [ ! -f "${DEB}" ]; then
    fail "usage: $0 <path-to-.deb> [--reboot|--no-remove]"
fi

DEB_ABS="$(readlink -f "${DEB}")"
DEB_NAME="$(dpkg-deb -f "${DEB_ABS}" Package 2>/dev/null || echo "")"
DEB_VER="$(dpkg-deb -f "${DEB_ABS}" Version 2>/dev/null || echo "")"

if [ -z "${DEB_NAME}" ] || [ -z "${DEB_VER}" ]; then
    fail "could not read package metadata from ${DEB_ABS}"
fi

info ".deb package: ${DEB_NAME} ${DEB_VER}"
info ".deb path:    ${DEB_ABS}"
info "log dir:      ${LOG_DIR}"
info "kernel:       ${KVER}"
info "mode:         ${MODE}"

# ═════════════════════════════════════════════════════════════════════════════
step "0. Capture pre-install state"
# ═════════════════════════════════════════════════════════════════════════════

(
    echo "=== pre-install state $(date -u +%Y-%m-%dT%H:%M:%SZ) ==="
    echo ""
    echo "--- loaded rp1-cfe ---"
    modinfo -F filename rp1-cfe 2>&1 || true
    echo ""
    echo "--- dkms status ---"
    dkms status 2>&1 || true
    echo ""
    echo "--- /usr/src ---"
    ls -la /usr/src/ 2>&1 || true
    echo ""
    echo "--- /etc/modprobe.d ---"
    ls -la /etc/modprobe.d/ 2>&1 || true
    echo ""
    echo "--- running ${DEB_NAME} (if any) ---"
    dpkg -s "${DEB_NAME}" 2>&1 | head -10 || true
) > "${LOG_DIR}/00_pre_install.log"

pass "captured pre-install state to ${LOG_DIR}/00_pre_install.log"

# ═════════════════════════════════════════════════════════════════════════════
step "1. apt install the .deb"
# ═════════════════════════════════════════════════════════════════════════════

# If the package is already installed at some version, purge it first so
# we get a clean postinst run.
if dpkg -s "${DEB_NAME}" > /dev/null 2>&1; then
    info "package ${DEB_NAME} already installed — removing first"
    apt-get remove -y "${DEB_NAME}" > "${LOG_DIR}/01a_pre_remove.log" 2>&1 \
        || fail "apt remove failed (see 01a_pre_remove.log)"
fi

# Use apt install (not dpkg -i) so dependency resolution works.
set +e
apt-get install -y "${DEB_ABS}" > "${LOG_DIR}/01b_install.log" 2>&1
RC=$?
set -e

if [ ${RC} -ne 0 ]; then
    echo ""
    echo "--- apt install output (last 60 lines) ---"
    tail -60 "${LOG_DIR}/01b_install.log"
    echo ""
    fail "apt install failed (full log: ${LOG_DIR}/01b_install.log)"
fi
pass "apt install succeeded"

# Echo the postinst's own output — it contains the DKMS build markers we
# want to confirm visually.
info "postinst summary lines:"
grep -E "(✓|✗|REBOOT)" "${LOG_DIR}/01b_install.log" | sed 's/^/      /'

# ═════════════════════════════════════════════════════════════════════════════
step "2. Verify DKMS registrations"
# ═════════════════════════════════════════════════════════════════════════════

dkms status > "${LOG_DIR}/02_dkms_status.log" 2>&1

if dkms status "${DW9714_NAME}/${DW9714_VERSION}" \
       | grep -q "installed"; then
    pass "${DW9714_NAME}/${DW9714_VERSION} installed for ${KVER}"
else
    fail "${DW9714_NAME}/${DW9714_VERSION} not installed (see 02_dkms_status.log)"
fi

if dkms status "${CFE_NAME}/${CFE_VERSION}" \
       | grep -q "installed"; then
    pass "${CFE_NAME}/${CFE_VERSION} installed for ${KVER}"
else
    fail "${CFE_NAME}/${CFE_VERSION} not installed (see 02_dkms_status.log)"
fi

# ═════════════════════════════════════════════════════════════════════════════
step "3. Verify built .ko files are in /updates/dkms/"
# ═════════════════════════════════════════════════════════════════════════════

UPDATES_DIR="/lib/modules/${KVER}/updates/dkms"
[ -d "${UPDATES_DIR}" ] || fail "${UPDATES_DIR} does not exist"

CFE_KO=$(find "${UPDATES_DIR}" -maxdepth 1 -name "rp1-cfe.ko*" | head -1)
DW9714_KO=$(find "${UPDATES_DIR}" -maxdepth 1 -name "dw9714.ko*" | head -1)

[ -n "${CFE_KO}" ] && [ -f "${CFE_KO}" ] \
    || fail "rp1-cfe.ko* not in ${UPDATES_DIR}"
pass "rp1-cfe.ko at ${CFE_KO}"

[ -n "${DW9714_KO}" ] && [ -f "${DW9714_KO}" ] \
    || fail "dw9714.ko* not in ${UPDATES_DIR}"
pass "dw9714.ko at ${DW9714_KO}"

# Sanity check: SANUWAVE strings baked into rp1-cfe.
if file "${CFE_KO}" | grep -q "XZ compressed"; then
    xzcat "${CFE_KO}" 2>/dev/null | strings | grep -q SANUWAVE \
        || fail "SANUWAVE strings missing from ${CFE_KO}"
else
    strings "${CFE_KO}" | grep -q SANUWAVE \
        || fail "SANUWAVE strings missing from ${CFE_KO}"
fi
pass "SANUWAVE strings confirmed in installed rp1-cfe.ko"

# ═════════════════════════════════════════════════════════════════════════════
step "4. Verify modprobe override"
# ═════════════════════════════════════════════════════════════════════════════

[ -f "${MODPROBE_FILE}" ] || fail "${MODPROBE_FILE} missing"
pass "${MODPROBE_FILE} present"

if grep -q "override rp1-cfe" "${MODPROBE_FILE}"; then
    pass "modprobe override directive present"
else
    info "contents:"
    sed 's/^/      /' "${MODPROBE_FILE}"
    fail "override directive missing from ${MODPROBE_FILE}"
fi

# ═════════════════════════════════════════════════════════════════════════════
step "5. Verify modinfo resolves to patched copy"
# ═════════════════════════════════════════════════════════════════════════════

MODINFO_FILE=$(modinfo -F filename rp1-cfe 2>/dev/null || true)
[ -n "${MODINFO_FILE}" ] || fail "modinfo could not find rp1-cfe"
info "modinfo filename: ${MODINFO_FILE}"

if echo "${MODINFO_FILE}" | grep -q "/updates/"; then
    pass "modinfo points at the patched /updates/ copy"
else
    fail "modinfo points at stock — depmod priority not respected"
fi

# ═════════════════════════════════════════════════════════════════════════════
step "6. Verify supporting config"
# ═════════════════════════════════════════════════════════════════════════════

[ -f "${BLACKLIST_FILE}" ] || fail "${BLACKLIST_FILE} missing"
grep -q "^blacklist ad5398_vcm" "${BLACKLIST_FILE}" \
    || fail "ad5398_vcm blacklist directive missing"
pass "ad5398_vcm blacklist in place"

[ -f "${OVERLAY_FILE}" ] || fail "${OVERLAY_FILE} missing"
pass "${OVERLAY_NAME}.dtbo installed"

grep -qE "^\s*dtoverlay=imx219,cam1\s*$" "${CONFIG}" \
    || fail "dtoverlay=imx219,cam1 not in ${CONFIG}"
pass "dtoverlay=imx219,cam1 in config.txt"

grep -qE "^\s*dtoverlay=${OVERLAY_NAME}" "${CONFIG}" \
    || fail "dtoverlay=${OVERLAY_NAME} not in ${CONFIG}"
pass "dtoverlay=${OVERLAY_NAME} in config.txt"

# ═════════════════════════════════════════════════════════════════════════════
step "7. Pre-reboot summary"
# ═════════════════════════════════════════════════════════════════════════════

info "install-time verification complete"
info "patched module is installed but NOT yet loaded (stock rp1-cfe is still live)"
info "reboot required before the patched module takes effect"

# ═════════════════════════════════════════════════════════════════════════════
if [ "${MODE}" = "--reboot" ]; then
    step "8. Reboot cycle"

    REBOOT_MARKER="/var/lib/sanuwave-deb-test-reboot-marker"
    if [ -f "${REBOOT_MARKER}" ]; then
        # Second invocation — we came back from a reboot.
        info "reboot marker found — post-reboot checks"
        rm -f "${REBOOT_MARKER}"

        # Confirm the patched module is actually loaded.
        LOADED=$(awk '$1 == "rp1_cfe" || $1 == "rp1-cfe" {print; exit}' \
                     /proc/modules || true)
        if [ -z "${LOADED}" ]; then
            fail "rp1-cfe not loaded after reboot"
        fi
        pass "rp1-cfe loaded: ${LOADED}"

        # Confirm kernel log shows patched-module signatures.
        if dmesg | grep -q "SANUWAVE"; then
            pass "dmesg shows SANUWAVE signatures — patched module active"
        else
            fail "no SANUWAVE lines in dmesg — stock module must have loaded"
        fi

        # Confirm sysfs interface is live on at least one CFE.
        if ls /sys/devices/platform/axi/*/1f00*.csi/sanuwave_strobe/strobe_active \
              > /dev/null 2>&1; then
            pass "sanuwave_strobe sysfs interface present"
        else
            fail "sanuwave_strobe sysfs interface not found"
        fi

    else
        info "setting reboot marker and rebooting"
        info "re-run this script with --reboot after the Pi comes back up"
        info "    sudo $0 ${DEB_ABS} --reboot"
        touch "${REBOOT_MARKER}"
        echo ""
        echo "    Rebooting in 5 seconds — ^C to abort"
        sleep 5
        reboot
        exit 0
    fi
fi

# ═════════════════════════════════════════════════════════════════════════════
if [ "${MODE}" != "--no-remove" ]; then
    step "9. apt remove & verify rollback"

    apt-get remove -y "${DEB_NAME}" > "${LOG_DIR}/09_remove.log" 2>&1 \
        || fail "apt remove failed (see 09_remove.log)"
    pass "apt remove succeeded"

    # Verify DKMS registrations are gone.
    if dkms status "${CFE_NAME}/${CFE_VERSION}" 2>/dev/null \
           | grep -q "installed"; then
        fail "${CFE_NAME} still registered after remove"
    fi
    pass "${CFE_NAME} unregistered"

    if dkms status "${DW9714_NAME}/${DW9714_VERSION}" 2>/dev/null \
           | grep -q "installed"; then
        fail "${DW9714_NAME} still registered after remove"
    fi
    pass "${DW9714_NAME} unregistered"

    # Verify the modprobe override file is gone.
    if [ -f "${MODPROBE_FILE}" ]; then
        fail "${MODPROBE_FILE} still present after remove"
    fi
    pass "modprobe override cleaned up"

    # Note: blacklist, DT overlay, and config.txt changes persist.  That
    # is intentional — they are system-level configuration the package
    # cannot reliably own.  Document this in the uninstall README.
    info "NOTE: ad5398 blacklist, DT overlay, and config.txt edits are"
    info "      intentionally left in place by uninstall"

else
    step "9. skipping remove (--no-remove)"
    info "package remains installed; uninstall manually with:"
    info "    sudo apt remove ${DEB_NAME}"
fi

# ═════════════════════════════════════════════════════════════════════════════
echo ""
echo "══════════════════════════════════════════════════════════════════"
echo "  ALL STAGES PASSED"
echo ""
echo "  package:   ${DEB_NAME} ${DEB_VER}"
echo "  log dir:   ${LOG_DIR}"
echo "══════════════════════════════════════════════════════════════════"
echo ""

exit 0
