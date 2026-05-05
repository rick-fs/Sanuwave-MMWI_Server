# DW9714 VCM Kernel Driver — Build, Deploy, and Packaging Guide

## Overview

The Arducam B0190 is an IMX219-based camera module with motorized focus driven by a Dongwoon DW9714 voice coil motor (VCM) controller. The Raspberry Pi kernel does not ship with the DW9714 driver enabled (`CONFIG_VIDEO_DW9714 is not set`). Instead, the stock `imx219` device tree overlay declares the VCM with the wrong compatible string, causing the `ad5398_vcm` driver to bind at I2C address `0x0C`. The AD5398 and DW9714 have incompatible register protocols — when the wrong driver binds, focus commands fail with I2C errors.

The fix has three parts: building the DW9714 driver as an out-of-tree kernel module, blacklisting the AD5398 driver, and deploying a corrected device tree overlay that declares `compatible = "dongwoon,dw9714"` so the correct driver binds. A custom libcamera tuning file (`imx219_noir_af.json`) is also required to enable the autofocus algorithm at runtime.

This driver is distributed inside the `sanuwave-imaging-server` .deb package via DKMS, so it rebuilds automatically on kernel updates. This document covers manual builds for development and the packaging integration for production.

---

## Prerequisites

- Raspberry Pi 5 running Raspberry Pi OS (Bookworm or later)
- Arducam B0190 connected to cam1
- SSH access to the Pi (`opticsadm@192.168.20.166`)
- Kernel headers installed for the running kernel
- `dkms` and `dtc` (device tree compiler) installed

## 1. Pin the Kernel (Recommended During Development)

Prevent kernel updates from invalidating your built module while you're working:

```bash
sudo apt-mark hold linux-image-rpi-2712 linux-image-$(uname -r) \
    linux-headers-rpi-2712 linux-headers-$(uname -r) \
    linux-headers-$(uname -r | sed 's/+rpt-rpi-2712/+rpt-common-rpi/')
```

Verify:

```bash
apt-mark showhold
```

To unpin later:

```bash
sudo apt-mark unhold linux-image-rpi-2712 linux-image-$(uname -r) \
    linux-headers-rpi-2712 linux-headers-$(uname -r) \
    linux-headers-$(uname -r | sed 's/+rpt-rpi-2712/+rpt-common-rpi/')
```

## 2. Build the Kernel Module

### 2.1 Verify kernel headers

```bash
uname -r
ls /lib/modules/$(uname -r)/build/Makefile
```

If the headers are missing:

```bash
sudo apt install -y linux-headers-$(uname -r)
```

### 2.2 Create build directory

```bash
mkdir -p ~/dw9714-build
cp /path/to/dw9714.c ~/dw9714-build/
```

The driver source (`dw9714.c`) lives in the repository at `packaging/dkms/dw9714.c`. It is the upstream Intel/Dongwoon V4L2 VCM driver from the Linux kernel tree.

### 2.3 Create the Makefile

```bash
cat > ~/dw9714-build/Makefile << 'EOF'
obj-m := dw9714.o

KDIR := /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
EOF
```

**Important:** The indented lines must use tab characters, not spaces.

### 2.4 Build

```bash
cd ~/dw9714-build
make
```

Expected output ends with `LD [M] .../dw9714.ko`.

### 2.5 Install and verify

```bash
sudo cp dw9714.ko /lib/modules/$(uname -r)/kernel/drivers/media/i2c/
sudo depmod -a
modinfo dw9714
```

The `modinfo` output should show `alias: of:N*T*Cdongwoon,dw9714` confirming the device tree match.

### 2.6 Smoke test

```bash
sudo modprobe dw9714
lsmod | grep dw9714
```

The module should load with refcount 0 (not yet bound to any device).

## 3. Build the Device Tree Overlay

### 3.1 Create the overlay source

```bash
cat > ~/dw9714-build/imx219-b0190-vcm.dts << 'EOF'
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2835";

    fragment@0 {
        target = <&i2c_csi_dsi>;

        __overlay__ {
            #address-cells = <1>;
            #size-cells = <0>;

            imx219@10 {
                lens-focus = <&dw9714>;
            };

            dw9714: dw9714@c {
                compatible = "dongwoon,dw9714";
                reg = <0x0c>;
                status = "okay";
                vcc-supply = <&cam1_reg>;
            };
        };
    };
};
EOF
```

Key details:

- `vcc-supply` (not `VDD-supply`) — must match the regulator name in `dw9714.c` (`devm_regulator_get(&client->dev, "vcc")`). Using the wrong name causes a dummy regulator fallback and I2C errors at boot.
- `lens-focus = <&dw9714>` on the `imx219@10` node — this phandle linkage is how libcamera discovers the VCM associated with the sensor. Without it, `controls::LensPosition` won't be available.
- The stock `imx219` overlay's `dw9807@c` node stays `status = "disabled"` because we do **not** pass the `vcm` parameter in config.txt.

### 3.2 Compile

```bash
cd ~/dw9714-build
dtc -@ -I dts -O dtb -o imx219-b0190-vcm.dtbo imx219-b0190-vcm.dts
```

A warning about `unit_address_vs_reg` on the `imx219@10` node is expected and harmless — we're referencing an existing node to add the `lens-focus` property.

### 3.3 Deploy

```bash
sudo cp imx219-b0190-vcm.dtbo /boot/firmware/overlays/
```

## 4. Blacklist the AD5398 Driver

```bash
echo "blacklist ad5398_vcm" | sudo tee /etc/modprobe.d/ad5398-blacklist.conf
```

## 5. Configure config.txt

Edit `/boot/firmware/config.txt`. The IMX219 lines should read:

```ini
dtoverlay=imx219,cam1
dtoverlay=imx219-b0190-vcm
```

**Not** `dtoverlay=imx219,cam1,vcm` — the `vcm` parameter enables the stock `dw9807@c` node, which is the wrong driver.

## 6. Reboot and Verify

```bash
sudo reboot
```

After reboot:

```bash
# Correct driver bound?
cat /sys/bus/i2c/devices/11-000c/name
# Should print: dw9714

# Module loaded?
lsmod | grep dw9714

# No I2C errors?
dmesg | grep -i "dw9714\|vcm"

# Both cameras detected?
rpicam-hello --list-cameras
```

### 6.1 Test focus control

```bash
rpicam-still --camera 1 --tuning-file /path/to/imx219_noir_af.json \
    -o /tmp/focus_inf.jpg --autofocus-mode manual --lens-position 0.0 -t 3s

rpicam-still --camera 1 --tuning-file /path/to/imx219_noir_af.json \
    -o /tmp/focus_near.jpg --autofocus-mode manual --lens-position 10.0 -t 3s
```

The `--lens-position` value is in diopters (0.0 = infinity, higher = closer). Compare the two images — you should see a clear focus shift.

**Note:** The `--tuning-file` flag is required because the stock `imx219.json` tuning file does not include the `rpi.af` algorithm block. Without it, libcamera will log `Could not set LENS_POSITION - no AF algorithm`. The server handles this at runtime via `setenv("LIBCAMERA_RPI_TUNING_FILE", ...)`.

## 7. DKMS Setup (Survives Kernel Updates)

Instead of manually copying the `.ko`, register the driver with DKMS:

```bash
sudo apt install -y dkms

sudo mkdir -p /usr/src/dw9714-1.0
sudo cp ~/dw9714-build/dw9714.c /usr/src/dw9714-1.0/
```

Create `/usr/src/dw9714-1.0/Makefile`:

```makefile
obj-m := dw9714.o
```

Create `/usr/src/dw9714-1.0/dkms.conf`:

```
PACKAGE_NAME="dw9714"
PACKAGE_VERSION="1.0"
BUILT_MODULE_NAME[0]="dw9714"
DEST_MODULE_LOCATION[0]="/kernel/drivers/media/i2c/"
AUTOINSTALL="yes"
MAKE[0]="make -C /lib/modules/${kernelver}/build M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build modules"
CLEAN="make -C /lib/modules/${kernelver}/build M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build clean"
```

Register, build, install:

```bash
sudo dkms add dw9714/1.0
sudo dkms build dw9714/1.0
sudo dkms install dw9714/1.0
dkms status
```

You can now unpin the kernel packages — DKMS will rebuild the module automatically on kernel updates.

---

## Production Packaging

The .deb package (`sanuwave-imaging-server`) ships the driver source, overlay, and tuning file. The `postinst` script handles DKMS setup, overlay deployment, blacklisting, and config.txt patching automatically.

### Files in the source tree

| File | Install destination | Purpose |
|------|-------------------|---------|
| `packaging/dkms/dw9714.c` | `/usr/local/share/sanuwave/dkms/dw9714/` | Kernel driver source |
| `packaging/dkms/Makefile` | `/usr/local/share/sanuwave/dkms/dw9714/` | DKMS build Makefile |
| `packaging/dkms/dkms.conf` | `/usr/local/share/sanuwave/dkms/dw9714/` | DKMS configuration |
| `packaging/overlays/imx219-b0190-vcm.dtbo` | `/usr/local/share/sanuwave/overlays/` | Compiled device tree overlay |
| `tuning/imx219_noir_af.json` | `/usr/local/share/sanuwave/tuning/` | NoIR + AF tuning file |
| `packaging/postinst` | (CPack control script) | Install-time setup |

### CMakeLists.txt install rules

```cmake
install(FILES
    packaging/dkms/dw9714.c
    packaging/dkms/Makefile
    packaging/dkms/dkms.conf
    DESTINATION share/sanuwave/dkms/dw9714
)

install(FILES
    packaging/overlays/imx219-b0190-vcm.dtbo
    DESTINATION share/sanuwave/overlays
)

install(FILES
    "${CMAKE_SOURCE_DIR}/tuning/imx219_noir_af.json"
    DESTINATION share/sanuwave/tuning
)
```

### What the postinst does

1. Installs `dkms` and kernel headers if missing (requires internet)
2. Copies driver source to `/usr/src/dw9714-1.0/` and runs `dkms add/build/install`
3. Installs `imx219-b0190-vcm.dtbo` to `/boot/firmware/overlays/`
4. Creates `/etc/modprobe.d/ad5398-blacklist.conf`
5. Patches `/boot/firmware/config.txt`: removes old `dtoverlay=imx219,...,vcm` line, adds `dtoverlay=imx219,cam1` and `dtoverlay=imx219-b0190-vcm`

### Runtime tuning file

The server code (`camera_factory.cpp`) sets `LIBCAMERA_RPI_TUNING_FILE` before opening the camera. It checks `/usr/local/share/sanuwave/tuning/imx219_noir_af.json` first (installed from .deb), then falls back to the executable directory (for development builds).

---

## Test Uninstall Package

For development and testing, a utility .deb reverts all driver changes so you can test the install path from a clean state.

### Building the uninstall package

On your dev machine:

```bash
mkdir -p ~/dw9714-uninstall/DEBIAN
```

Create `~/dw9714-uninstall/DEBIAN/control`:

```
Package: sanuwave-dw9714-uninstall
Version: 1.0
Architecture: arm64
Maintainer: Sanuwave
Description: Removes DW9714 VCM driver, overlay, and blacklist.
Priority: optional
```

Create `~/dw9714-uninstall/DEBIAN/postinst` with the uninstall script (see `packaging/test/dw9714-uninstall-postinst` in the repo), then:

```bash
chmod 755 ~/dw9714-uninstall/DEBIAN/postinst
dpkg-deb --build ~/dw9714-uninstall ~/sanuwave-dw9714-uninstall_1.0_arm64.deb
```

### Using the uninstall package

```bash
scp sanuwave-dw9714-uninstall_1.0_arm64.deb opticsadm@192.168.20.166:~/
ssh opticsadm@192.168.20.166 "sudo dpkg -i ~/sanuwave-dw9714-uninstall_1.0_arm64.deb"
```

The postinst will:

1. Unload the `dw9714` module if loaded
2. Remove the DKMS registration and source from `/usr/src/dw9714-1.0/`
3. Remove any manually installed `.ko` from `/lib/modules/`
4. Delete `/etc/modprobe.d/ad5398-blacklist.conf`
5. Remove `imx219-b0190-vcm.dtbo` from `/boot/firmware/overlays/`
6. Revert `config.txt` back to `dtoverlay=imx219,cam1,vcm`

Reboot after running. Then remove the uninstall package itself:

```bash
sudo dpkg --purge sanuwave-dw9714-uninstall
```

The system is now back to stock, ready for a clean install test.

---

## Troubleshooting

**`supply vcc not found, using dummy regulator` in dmesg** — The overlay uses `VDD-supply` instead of `vcc-supply`. The driver code calls `devm_regulator_get(&client->dev, "vcc")`. Recompile the overlay with `vcc-supply = <&cam1_reg>`.

**`I2C write fail` / EIO errors at boot** — The VCM shares the IMX219's power rail. If the regulator isn't wired correctly in the overlay, the driver tries to communicate before the chip is powered. Fix the `vcc-supply` as above.

**`Could not set LENS_POSITION - no AF algorithm`** — The tuning file doesn't include `rpi.af`. Ensure the server is using `imx219_noir_af.json` (which merges the NoIR color science with the AF algorithm block from `imx219_af.json`).

**`ad5398` still binding instead of `dw9714`** — Check that `/etc/modprobe.d/ad5398-blacklist.conf` exists, and that `config.txt` does **not** contain the `vcm` parameter on the `imx219` overlay line.

**Focus doesn't shift between lens positions** — The `--lens-position` value for `rpicam-still` is in diopters, not a 0–1 normalized range. Try `0.0` vs `10.0` for a dramatic difference. Values of `0.0` vs `1.0` are nearly identical.
