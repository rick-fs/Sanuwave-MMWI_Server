# Proof of Concept: Patching the rp1-cfe Kernel Module on Pi 5

**Goal:** Rebuild the `rp1-cfe` camera driver with a one-line `dev_info()` added to the
ISR, install it, and confirm via `dmesg` that your patched module is loaded and running.
This proves the build/install/replace pipeline works before adding real GPIO logic.

**Target:** `opticsadm@192.168.20.166` (Pi 5, kernel `6.12.62+rpt-rpi-2712`)



---

## Prerequisites

You need the kernel headers installed on the Pi. Verify:

```bash
ls /lib/modules/$(uname -r)/build
```

If that directory doesn't exist:

```bash
sudo apt install -y raspberrypi-kernel-headers
```

**Important:** Your kernel packages are held (`apt-mark hold`). Don't unhold them.
The headers package must match your running kernel exactly. Verify:

```bash
uname -r
# Expected: 6.12.62+rpt-rpi-2712
```

---

## Step 1: Clone the kernel source (matching your running kernel)

You need the rp1_cfe source that matches your running kernel. On the Pi:

```bash
cd ~
git clone --depth=1 -b rpi-6.12.y https://github.com/raspberrypi/linux.git rpi-linux
```

This takes several minutes even with `--depth=1`. Consider running inside `tmux`
or `screen` in case your SSH session drops.

> **To verify your kernel branch**, check with:
> ```bash
> cat /proc/version
> ```
> and pick the matching branch from https://github.com/raspberrypi/linux/branches

---

## Step 2: Locate the driver source

**Note:** The directory uses an underscore, not a hyphen: `rp1_cfe`, not `rp1-cfe`.

```bash
ls ~/rpi-linux/drivers/media/platform/raspberrypi/rp1_cfe/
```

You should see:

```
Kconfig  Makefile  cfe.c  cfe.h  cfe_fmts.h  csi2.c  csi2.h
dphy.c  dphy.h  pisp_common.h  pisp_fe.c  pisp_fe.h
pisp_fe_config.h  pisp_statistics.h  pisp_types.h
```

The key file is **`cfe.c`**. The ISR is the function `cfe_isr()`. The frame-start
handler is `cfe_sof_isr_handler()` and the frame-end handler is
`cfe_eof_isr_handler()`.

---

## Step 3: Add your proof-of-concept patch

Open `cfe.c` in nano:

```bash
nano ~/rpi-linux/drivers/media/platform/raspberrypi/rp1_cfe/cfe.c
```

> **Nano over SSH:** If nano looks garbled, run `export TERM=xterm-256color` first.
> Use `Ctrl+W` to search. `Ctrl+O` to save. `Ctrl+X` to exit.

Search for `static void cfe_sof_isr_handler` (`Ctrl+W`, type the search string,
press Enter). The function looks like this:

```c
static void cfe_sof_isr_handler(struct cfe_node *node)
{
    struct cfe_device *cfe = node->cfe;
    bool matching_fs = true;
    unsigned int i;

    cfe_dbg_verbose("%s: [%s] seq %u\n", __func__, node_desc[node->id].name,
                    node->fs_count);
    ...
```

Add ONE line right after the existing `cfe_dbg_verbose` call, before the
`if (WARN(...))` block:

```c
    cfe_dbg_verbose("%s: [%s] seq %u\n", __func__, node_desc[node->id].name,
                    node->fs_count);

    /* SANUWAVE PROOF-OF-CONCEPT: confirm patched module is running */
    if (node->fs_count == 1)
        dev_info(cfe->v4l2_dev.dev, "SANUWAVE: patched rp1-cfe loaded, first SOF received\n");
```

**Why `fs_count == 1`:** This prints only on the first frame start per node,
not on every frame. Printing on every ISR would flood dmesg and hurt performance.
You will see multiple messages (one per CSI2 channel node) — that's expected.

Save and exit (`Ctrl+O`, `Enter`, `Ctrl+X`).

---

## Step 4: Set up the out-of-tree module build

Create a build directory and copy the source files:

```bash
mkdir -p ~/rp1-cfe-build
cp ~/rpi-linux/drivers/media/platform/raspberrypi/rp1_cfe/*.c \
   ~/rpi-linux/drivers/media/platform/raspberrypi/rp1_cfe/*.h \
   ~/rp1-cfe-build/
```

Create the out-of-tree Makefile:

```bash
cat > ~/rp1-cfe-build/Makefile << 'MAKEOF'
# Out-of-tree build for rp1-cfe module
obj-m := rp1-cfe.o
rp1-cfe-objs := cfe.o csi2.o pisp_fe.o dphy.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
MAKEOF
```

> **Note:** The `$(MAKE)` lines must be indented with a **tab character**, not spaces.
> If you copy-paste and get spaces, the build will fail with
> `*** missing separator. Stop.`
>
> To fix, open the Makefile in nano, delete the whitespace before `$(MAKE)`,
> and press Tab to insert a real tab.

---

## Step 4a: Fix missing pisp_common.h header (required)

The `pisp_fe_config.h` file includes `<media/raspberrypi/pisp_common.h>`, but the
kernel headers package installs it under a different path (`uapi/linux/media/...`).
Create a symlink so the compiler can find it:

```bash
sudo ln -s \
  /usr/src/linux-headers-$(uname -r | sed 's/+rpt-rpi-2712/+rpt-common-rpi/')/include/uapi/linux/media/raspberrypi \
  /usr/src/linux-headers-$(uname -r | sed 's/+rpt-rpi-2712/+rpt-common-rpi/')/include/media/raspberrypi
```

> **If that `sed` expression doesn't match your headers path**, find it manually:
> ```bash
> find /usr/src -path "*/media/raspberrypi/pisp_common.h" 2>/dev/null
> ```
> Then create the symlink from the `uapi/linux/media/raspberrypi` directory to
> a new `include/media/raspberrypi` in the same headers tree.

Without this symlink, the build fails with:
```
fatal error: media/raspberrypi/pisp_common.h: No such file or directory
```

---

## Step 5: Build the module

```bash
cd ~/rp1-cfe-build
make
```

If successful, you'll see output ending with:

```
  LD [M]  /home/opticsadm/rp1-cfe-build/rp1-cfe.ko
```

And you'll have a file `rp1-cfe.ko` in the directory.

---

## Step 6: Back up the original module

```bash
ORIG=/lib/modules/$(uname -r)/kernel/drivers/media/platform/raspberrypi/rp1_cfe/rp1-cfe.ko.xz
sudo cp "$ORIG" "${ORIG}.stock_backup"
echo "Backed up: $ORIG"
```

---

## Step 7: Install the patched module

The stock module is xz-compressed, so compress yours to match:

```bash
ORIG_DIR=/lib/modules/$(uname -r)/kernel/drivers/media/platform/raspberrypi/rp1_cfe

xz -f -k ~/rp1-cfe-build/rp1-cfe.ko
sudo cp ~/rp1-cfe-build/rp1-cfe.ko.xz "$ORIG_DIR/rp1-cfe.ko.xz"
sudo depmod -a
```

---

## Step 8: Reboot and verify

```bash
sudo reboot
```

After reboot (~30 seconds), reconnect via SSH.

First, confirm the out-of-tree module loaded (not the stock one):

```bash
dmesg | grep "out-of-tree"
# Expected: rp1_cfe: loading out-of-tree module taints kernel.
```

The `SANUWAVE` message only fires when the camera captures its first frame.
Trigger a capture:

```bash
rpicam-still -o /tmp/test.jpg --timeout 2000
```

Then check:

```bash
dmesg | grep SANUWAVE
```

Expected output (multiple lines, one per CSI2 channel node):

```
[  227.398509] rp1-cfe 1f00110000.csi: SANUWAVE: patched rp1-cfe loaded, first SOF received
[  227.398517] rp1-cfe 1f00110000.csi: SANUWAVE: patched rp1-cfe loaded, first SOF received
[  227.398522] rp1-cfe 1f00110000.csi: SANUWAVE: patched rp1-cfe loaded, first SOF received
[  227.398524] rp1-cfe 1f00110000.csi: SANUWAVE: patched rp1-cfe loaded, first SOF received
```

**If you see those lines, the proof of concept is complete.**

---

## Step 9: Verify camera still works

The `rpicam-still` command from Step 8 should have produced a valid JPEG:

```bash
file /tmp/test.jpg
# Expected: JPEG image data, ...
```

If the camera works and the dmesg message appears, you have a fully validated
build/install/test pipeline for modifying the Pi 5 camera driver.

---

## Rolling back

To restore the stock module:

```bash
ORIG=/lib/modules/$(uname -r)/kernel/drivers/media/platform/raspberrypi/rp1_cfe/rp1-cfe.ko.xz
sudo cp "${ORIG}.stock_backup" "$ORIG"
sudo depmod -a
sudo reboot
```

---

## What's next

Once validated, the real patch adds:

1. Two `gpio_desc` pointers to the `cfe_device` struct (in `cfe.h`)
2. `devm_gpiod_get_optional()` calls in `cfe_probe()` (in `cfe.c`)
3. `gpiod_set_value()` calls in `cfe_sof_isr_handler()` and `cfe_eof_isr_handler()`
4. Device tree overlay with `frame-sync-gpios` property

But that's for after the hardware team has validated the wiring and the
LM3643 ramp-up timing on the bench.

---

## Troubleshooting

**`fatal error: media/raspberrypi/pisp_common.h: No such file or directory`**
The symlink from Step 4a is missing or points to the wrong place. Verify:
```bash
ls -la /usr/src/linux-headers-*-common-rpi/include/media/raspberrypi/
```
Should show a symlink to the `uapi/linux/media/raspberrypi` directory.

**`*** missing separator. Stop.` when running make**
The Makefile has spaces instead of tabs before `$(MAKE)`. Open in nano,
delete the leading whitespace, and press Tab to insert a real tab character.

**"Module has no symbol version for module_layout"**
Your kernel headers don't match the running kernel. Verify:
```bash
dpkg -l | grep raspberrypi-kernel-headers
uname -r
```
Both versions must match.

**Build succeeds but module won't load after reboot**
Check `dmesg | grep rp1` for errors. The most common cause is a version
magic mismatch — same root cause as above.

**No `SANUWAVE` message after reboot**
The message only fires on the first camera capture, not at boot. Run
`rpicam-still -o /tmp/test.jpg --timeout 2000` first, then check dmesg.

**Camera doesn't work after installing patched module**
Roll back (see above) and verify the stock module works. Then re-examine
your patch for typos or misplaced code.

**"rp1-cfe: Unknown symbol" errors in dmesg**
The module depends on symbols from the kernel's V4L2/videobuf2 subsystem.
If you see this, the headers/kernel mismatch is the likely cause.
