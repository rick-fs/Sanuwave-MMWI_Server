# SanuwaveClient — Build Instructions

## Prerequisites

### All Platforms
- CMake 3.19+
- Qt 6 (Core, Widgets, Network)
- libjpeg-turbo (development package)
- Git (for version stamping)
- C++17-capable compiler

> CMake 3.16+ will work for manual builds, but 3.19+ is required to use the preset workflow below.

### Linux (Ubuntu/Debian)
```bash
sudo apt install cmake ninja-build git build-essential \
    qt6-base-dev \
    libturbojpeg0-dev \
    pkg-config
```

### Windows
- Visual Studio 2022 with C++ workload
- Qt 6.x installed locally (path configured via `CMakeUserPresets.json` — see below)
- [libjpeg-turbo](https://libjpeg-turbo.org/Documentation/OfficialBinaries) 64-bit installed to `C:/libjpeg-turbo64/`
- NSIS (optional, for installer packaging)

---

## Clone

```bash
git clone <repo-url> SanuwaveClient
cd SanuwaveClient
```

> The build embeds the git hash and commit count into the binary. A shallow clone or missing git history will result in `unknown` version info.

---

## First-Time Setup (Windows only)

The Qt installation path is intentionally not stored in source control. Before building on Windows, create a `CMakeUserPresets.json` file in the project root by copying the provided template:

```bat
copy CMakeUserPresets.json.example CMakeUserPresets.json
```

Then edit `CMakeUserPresets.json` and set `CMAKE_PREFIX_PATH` to your local Qt installation:

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "windows-local",
      "displayName": "Windows (local)",
      "inherits": "windows-base",
      "cacheVariables": {
        "CMAKE_PREFIX_PATH": "D:/Your/Path/To/Qt/lib/cmake/Qt6"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "windows-local",
      "configurePreset": "windows-local"
    }
  ]
}
```

> `CMakeUserPresets.json` is listed in `.gitignore` and will never be committed.

---

## Build — Linux

```bash
cmake --preset linux-release
cmake --build --preset linux-release
```

For a debug build:
```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
```

Output binary: `build/release/bin/SanuwaveClient` (or `build/debug/bin/...`).

---

## Build — Windows

Using the preset (recommended — VS Code and Visual Studio pick this up automatically):

```bat
cmake --preset windows-local
cmake --build --preset windows-local
```

Or via the IDE: Visual Studio 2022 and VS Code (cmake-tools extension) will automatically detect `CMakeUserPresets.json` and show **Windows (local)** in the preset picker.

Qt DLLs and TurboJPEG DLLs are automatically copied to the output directory via `windeployqt` as a post-build step.

---

## Packaging

### Linux — Debian package + tarball
```bash
cd build/release
cpack -G DEB    # produces SanuwaveClient-<version>-<hash>.deb
cpack -G TGZ    # produces SanuwaveClient-<version>-<hash>.tar.gz
```

Install the `.deb`:
```bash
sudo dpkg -i SanuwaveClient-*.deb
```

Dependencies pulled in automatically: `libc6`, `libqt6core6`, `libqt6widgets6`, `libqt6network6`, `libturbojpeg0`.

### Windows — NSIS installer + ZIP
```bat
cd build/windows-local
cpack -G NSIS   # produces SanuwaveClient-<version>-<hash>.exe installer
cpack -G ZIP    # produces SanuwaveClient-<version>-<hash>.zip
```

NSIS must be installed and on PATH for the installer target.

---

## Version Info

The build system stamps the binary with version metadata at configure time:

| Symbol | Example | Source |
|---|---|---|
| `VERSION_STRING` | `v1.0.0.312.a3f9c1d` | `PROJECT_VERSION` + commit count + short hash |
| `VERSION_DISPLAY` | `v1.0.0 (build 312)` | Human-readable, shown in About dialog |
| `GIT_HASH` | `a3f9c1d` | Short hash (7 chars) |
| `GIT_BRANCH` | `main` | Current branch |

A `-dirty` suffix is appended to the package filename if there are uncommitted changes at configure time.

> **Note:** Version info is baked in at configure time. Re-run `cmake --preset <name>` after new commits if an accurate hash is needed in the binary.

---

## Shared Headers

The client references headers from a sibling `shared/` directory:

```
../shared/include/
```

Ensure this path exists relative to the client source root (it contains `protocol_constants.h` and related shared types).

---

## Troubleshooting

**`CMakeUserPresets.json` not found (Windows)**
- Copy `CMakeUserPresets.json.example` to `CMakeUserPresets.json` and set your Qt path. See *First-Time Setup* above.

**TurboJPEG not found**
- Linux: `sudo apt install libturbojpeg0-dev`
- Windows: verify installation at `C:/libjpeg-turbo64/` or set `TURBOJPEG_INCLUDE_DIR` / `TURBOJPEG_LIBRARY` manually in your CMake cache

**Qt6 not found**
- Linux: confirm `qmake --version` reports Qt 6; try setting `-DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt6`
- Windows: verify `CMAKE_PREFIX_PATH` in your `CMakeUserPresets.json` points to the `lib/cmake/Qt6` subdirectory of your Qt installation

**`moc` errors on Linux**
- Ensure `CMAKE_AUTOMOC_MOC_EXECUTABLE` points to the Qt6 `moc` binary: `/usr/lib/qt6/libexec/moc`
