# Arducam B0190 Autofocus + NoIR Tuning File Setup
## Raspberry Pi 5 / libcamera / PiSP

#NOTE 
#The installer for the application will take care of installing the tuning file. The instuctions below are for reference
#if it needs to be modified or recreated.


This guide walks through verifying the hardware setup and creating a merged tuning file that enables both NoIR color science and autofocus.
The standard tuning file and configuration file that comes with the raspberry pi does not enable these features.

---

## Prerequisites

- Raspberry Pi 5 running Raspberry Pi OS (Bookworm or later)
- Arducam B0190 connected to a camera port (e.g. cam1)
- libcamera installed (comes with RPi OS)
- Python 3 available on your development machine or the Pi
- The three IMX219 tuning files from the Raspberry Pi libcamera GitHub:
  - `imx219.json`
  - `imx219_af.json`
  - `imx219_noir.json`

Download them from:
`https://github.com/ArduCAM/libcamera/tree/arducam/src/ipa/rpi/pisp/data`

---

## Step 1: Configure the Device Tree Overlay

Edit `/boot/firmware/config.txt` and ensure the overlay for your camera port includes the `vcm` parameter:

```
dtoverlay=imx219,cam1,vcm
```

Adjust `cam0` or `cam1` to match whichever port the B0190 is connected to. Reboot after making this change.

---

## Step 2: Verify the VCM Driver is Loaded

After rebooting, check that the dw9807 VCM kernel driver is loaded:

```bash
lsmod | grep vcm
```

You should see `dw9807_vcm` in the output. If not, the Arducam driver package may not be installed or the overlay is incorrect.

---

## Step 3: Verify the VCM is Enumerated as a V4L2 Subdevice

```bash
media-ctl --print-topology 2>&1 | grep -A3 -i "dw9807\|lens"
```

You should see something like:

```
- entity 19: dw9807 10-000c (0 pad, 0 link, 0 routes)
             type V4L2 subdev subtype Lens flags 0
             device node name /dev/v4l-subdev7
```

If this entry is missing, the VCM is not being detected. Check `dmesg | grep -i dw9807` for probe errors and verify the overlay configuration in Step 1.

---

## Step 4: Create the (or use my)  Merged Tuning File

The merged file uses `imx219_noir.json` as the base (for the no-IR-filter sensor) and adds the `rpi.af` block from `imx219_af.json`.

Run this Python script on your development machine (or on the Pi) with both files in the same directory:

```python
import json

base_dir = '/path/to/your/tuning/files'

with open(f'{base_dir}/imx219_noir.json') as f:
    noir = json.load(f)

with open(f'{base_dir}/imx219_af.json') as f:
    af = json.load(f)

# Extract the rpi.af block
af_block = next(a for a in af['algorithms'] if 'rpi.af' in a)

# Append to noir algorithms
noir['algorithms'].append(af_block)

output = f'{base_dir}/imx219_noir_af.json'
with open(output, 'w') as f:
    json.dump(noir, f, indent=2)

print(f"Written to {output}")
present = any('rpi.af' in a for a in noir['algorithms'])
print(f"rpi.af block present: {present}")
```

This produces `imx219_noir_af.json` — the merged tuning file.

# How this will be used

The installer will install the tuning file along with the application, in a directory ./tuning/ 
When the imx219 camera is opened using libcamera, 

the enviroment variable will be set before opening the camera:

    std::string tuningFile = getTuningFilePath();
    if (!tuningFile.empty() && std::filesystem::exists(tuningFile))
    {
        LOG_INFO << "CameraFactory: Using tuning file: " << tuningFile << std::endl;
        setenv("LIBCAMERA_RPI_TUNING_FILE", tuningFile.c_str(), 1);
    }
    else
    {
        LOG_WARNING << "CameraFactory: IMX219 tuning file not found, using libcamera default" << std::endl;
    }
    camera = std::make_unique<CameraIMX219>();

This will enable the focus and noir capabilities (3/9/2026 - I've tested the focus and it works; I am assuming the noir will work but TBD)



