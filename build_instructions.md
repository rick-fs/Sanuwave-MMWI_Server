Sanuwave Imaging System - Build Instructions
============================================

Version: 1.0
Applies to: sanuwave_imaging_server, SanuwaveClient


================================================================
OVERVIEW
================================================================

The Sanuwave imaging system consists of two components:

  1. sanuwave_imaging_server
     Runs on the Raspberry Pi 5. It controls the cameras, LEDs,
     and sensors, and communicates with the desktop client over
     a TCP network connection on port 8080.

  2. SanuwaveClient
     Runs on a Windows or Linux desktop computer. It connects to
     the server, displays the camera streams, and controls
     capture sessions.

The server is built on an Ubuntu desktop machine using a
cross-compiler that produces an ARM64 binary for the Pi. The
resulting package is then copied to the Pi and installed.

The client is built natively on whichever desktop OS you are
using (Windows or Linux).

These instructions assume a fresh OS installation with nothing
pre-installed. Follow each step in order. Do not skip steps.


================================================================
CONVENTIONS USED IN THIS DOCUMENT
================================================================

Commands shown in a block like this must be typed exactly as
written, unless noted otherwise:

    sudo apt install -y cmake

Angle brackets indicate a value you must substitute:

    ssh <your_username>@<pi_ip_address>

For example, if your Pi's username is "pi" and its IP address
is 192.168.1.50, you would type:

    ssh pi@192.168.1.50

Lines beginning with # are comments explaining the command that
follows. Do not type them.


================================================================
PART 1 - BUILDING THE SERVER (sanuwave_imaging_server)
================================================================

The server is cross-compiled on an Ubuntu desktop machine. This
means you use Ubuntu to build a binary that runs on the
Raspberry Pi, without needing to compile anything on the Pi
itself.

You will need:
  - An Ubuntu desktop machine (Ubuntu 22.04 LTS or later,
    x86_64)
  - A Raspberry Pi 5 running Raspberry Pi OS Trixie (Debian 13)
  - Both machines connected to the same network
  - SSH access from Ubuntu to the Pi

----------------------------------------------------------------
1.1  Prepare the Raspberry Pi
----------------------------------------------------------------

These steps are performed on the RASPBERRY PI.

Connect to the Pi via SSH from your Ubuntu machine. Replace
the placeholders with your Pi's actual username and IP address:

    ssh <pi_username>@<pi_ip_address>

Once logged in, update the package list and install the
libraries that the server depends on at runtime and build time:

    sudo apt update
    sudo apt install -y \
        libcamera-dev \
        libturbojpeg0-dev \
        libgtk-3-dev \
        libgstreamer1.0-dev \
        libgstreamer-plugins-base1.0-dev \
        libavcodec-dev \
        libavformat-dev \
        libswscale-dev \
        libv4l-dev \
        libjpeg-dev \
        libpng-dev \
        libtiff-dev \
        libtbb-dev \
        i2c-tools

When this completes, log out of the Pi:

    exit

You are now back on your Ubuntu machine. All remaining steps in
section 1 are performed on UBUNTU unless stated otherwise.

----------------------------------------------------------------
1.2  Install Build Tools (Ubuntu)
----------------------------------------------------------------

Install the cross-compiler toolchain and supporting utilities:

    sudo apt update
    sudo apt install -y \
        gcc-aarch64-linux-gnu \
        g++-aarch64-linux-gnu \
        cmake \
        ninja-build \
        gdb-multiarch \
        rsync \
        symlinks \
        git \
        pkg-config

Verify the cross-compiler installed correctly:

    aarch64-linux-gnu-gcc --version

You should see output similar to:
    aarch64-linux-gnu-gcc (Ubuntu ...) 12.x.x

----------------------------------------------------------------
1.3  Configure SSH Key Access (Ubuntu)
----------------------------------------------------------------

The build process uses SSH and rsync to copy files between your
Ubuntu machine and the Pi. Setting up key-based authentication
avoids being prompted for a password on every transfer.

Check if you already have an SSH key:

    ls ~/.ssh/id_ed25519

If that file exists, skip the key generation step. If you see
"No such file or directory", generate a key now:

    ssh-keygen -t ed25519

Press Enter at all prompts to accept the defaults.

Copy your key to the Pi (you will be prompted for the Pi's
password once):

    ssh-copy-id <pi_username>@<pi_ip_address>

Verify the connection works without a password:

    ssh <pi_username>@<pi_ip_address> 'echo "SSH working"'

You should see: SSH working

----------------------------------------------------------------
1.4  Create the Sysroot (Ubuntu)
----------------------------------------------------------------

A sysroot is a copy of the Pi's headers and libraries on your
Ubuntu machine. The cross-compiler uses it to find the correct
ARM64 versions of all dependencies when building.

A script called sync-sysroot.sh is provided in the
sanuwave-device/scripts/ directory. Before running it, open it
in a text editor and update the two variables near the top:

    nano ~/sanuwave/sanuwave-device/scripts/sync-sysroot.sh

Find and change these two lines:

    DEFAULT_RPI_HOST="pi@raspberrypi.local"
    SYSROOT_PATH="/home/rickfrank/rpi-sysroot"

Replace them with your Pi's login and your desired sysroot
path, for example:

    DEFAULT_RPI_HOST="<pi_username>@<pi_ip_address>"
    SYSROOT_PATH="$HOME/rpi-sysroots/trixie"

Save and close (Ctrl+O, Enter, Ctrl+X). Make the script
executable and run it:

    chmod +x ~/sanuwave/sanuwave-device/scripts/sync-sysroot.sh
    ~/sanuwave/sanuwave-device/scripts/sync-sysroot.sh

The script will test the SSH connection, sync the Pi's /lib,
/usr, and /opt directories to your sysroot, and verify that
TurboJPEG is present. On the first run this may take several
minutes.

When it finishes you should see:
    Sysroot is ready for cross-compilation!

The Pi's library files contain symbolic links that use absolute
paths. These are correct on the Pi but point to the wrong
location on your Ubuntu machine. The following command converts
them to relative paths so the cross-compiler can follow them:

    sudo symlinks -rc ~/rpi-sysroots

Verify the sysroot contains the expected directories:

    ls ~/rpi-sysroots/trixie/usr/include/aarch64-linux-gnu
    ls ~/rpi-sysroots/trixie/usr/lib/aarch64-linux-gnu

Both commands should list many files. If either directory is
empty, check your SSH connection and repeat section 1.3 before
running the script again.

----------------------------------------------------------------
1.5  Cross-Compile OpenCV for the Pi (Ubuntu)
----------------------------------------------------------------

The server uses OpenCV for image processing. OpenCV is not
available as a pre-built ARM64 package, so it must be compiled
from source on your Ubuntu machine and then deployed to the Pi.

This process takes 15 to 30 minutes depending on your machine.

Step 1: Install OpenCV build dependencies

    sudo apt install -y cmake ninja-build pkg-config

Step 2: Download the OpenCV source code

    mkdir -p ~/opencv_cross
    cd ~/opencv_cross
    git clone --depth 1 --branch 4.10.0 \
        https://github.com/opencv/opencv.git

Step 3: Create the CMake toolchain files

These files tell CMake how to use the cross-compiler. You will
create them once and reuse them for all future builds.

    mkdir -p ~/toolchains

Create the first file. The following command opens a text
editor (nano). Type or paste the content shown, then save with
Ctrl+O, Enter, Ctrl+X.

    nano ~/toolchains/aarch64-rpi5-toolchain-common.cmake

Content to enter:

---
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER /usr/bin/aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER /usr/bin/aarch64-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_C_FLAGS_INIT "-march=armv8-a+crc+simd")
set(CMAKE_CXX_FLAGS_INIT "-march=armv8-a+crc+simd")
---

Save and close (Ctrl+O, Enter, Ctrl+X), then create the second
file:

    nano ~/toolchains/aarch64-rpi5-toolchain-opencv.cmake

Content:

---
include(${CMAKE_CURRENT_LIST_DIR}/aarch64-rpi5-toolchain-common.cmake)

if(DEFINED ENV{RPI_SYSROOT})
    set(RPI_SYSROOT $ENV{RPI_SYSROOT})
else()
    set(RPI_SYSROOT $ENV{HOME}/rpi-sysroots/trixie)
endif()
set(CMAKE_SYSROOT ${RPI_SYSROOT})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -isystem ${RPI_SYSROOT}/usr/include/aarch64-linux-gnu -isystem ${RPI_SYSROOT}/usr/include")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -isystem ${RPI_SYSROOT}/usr/include/aarch64-linux-gnu -isystem ${RPI_SYSROOT}/usr/include")

set(CMAKE_FIND_ROOT_PATH
    ${RPI_SYSROOT}
    ${RPI_SYSROOT}/usr
)

set(ENV{PKG_CONFIG_SYSROOT_DIR} ${RPI_SYSROOT})
set(ENV{PKG_CONFIG_PATH} "${RPI_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${RPI_SYSROOT}/usr/share/pkgconfig")
---

Save and close, then create the third file:

    nano ~/toolchains/aarch64-rpi5-toolchain-trixie.cmake

Content:

---
include(${CMAKE_CURRENT_LIST_DIR}/aarch64-rpi5-toolchain-common.cmake)

set(RPI_SYSROOT $ENV{HOME}/rpi-sysroots/trixie)
set(CMAKE_SYSROOT ${RPI_SYSROOT})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -isystem ${RPI_SYSROOT}/usr/include/aarch64-linux-gnu -isystem ${RPI_SYSROOT}/usr/include")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -isystem ${RPI_SYSROOT}/usr/include/aarch64-linux-gnu -isystem ${RPI_SYSROOT}/usr/include")

set(CMAKE_FIND_ROOT_PATH
    ${RPI_SYSROOT}
    ${RPI_SYSROOT}/usr
    ${RPI_SYSROOT}/opt/opencv
)

set(ENV{PKG_CONFIG_SYSROOT_DIR} ${RPI_SYSROOT})
set(ENV{PKG_CONFIG_PATH} "${RPI_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${RPI_SYSROOT}/usr/share/pkgconfig:${RPI_SYSROOT}/opt/opencv/lib/pkgconfig")

set(OpenCV_DIR ${RPI_SYSROOT}/opt/opencv/lib/cmake/opencv4)
set(LIBCAMERA_INCLUDE_DIR ${RPI_SYSROOT}/usr/include/libcamera)
set(LIBCAMERA_LIBRARY ${RPI_SYSROOT}/usr/lib/aarch64-linux-gnu/libcamera.so)
---

Save and close.

Step 4: Create the OpenCV configuration script

    nano ~/opencv_cross/config.sh

Content (substitute your Pi username and IP address):

---
#!/bin/bash
set -e

SYSROOT=$HOME/rpi-sysroots/trixie
export PKG_CONFIG_SYSROOT_DIR=${SYSROOT}
export PKG_CONFIG_LIBDIR=${SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${SYSROOT}/usr/share/pkgconfig
export RPI_SYSROOT=${SYSROOT}

BUILD_DIR=~/opencv_builds/relwithdebinfo
SOURCE_DIR=~/opencv_cross/opencv
TOOLCHAIN_FILE=~/toolchains/aarch64-rpi5-toolchain-opencv.cmake
INSTALL_PREFIX=/opt/opencv

mkdir -p $BUILD_DIR
cd $BUILD_DIR

cmake -DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN_FILE \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX \
    -DOPENCV_GENERATE_PKGCONFIG=ON \
    -DOPENCV_ENABLE_NONFREE=OFF \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_TESTS=OFF \
    -DBUILD_PERF_TESTS=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_DOCS=OFF \
    -DBUILD_opencv_apps=OFF \
    -DBUILD_opencv_python2=OFF \
    -DBUILD_opencv_python3=OFF \
    -DBUILD_opencv_java=OFF \
    -DBUILD_opencv_core=ON \
    -DBUILD_opencv_imgproc=ON \
    -DBUILD_opencv_imgcodecs=ON \
    -DBUILD_opencv_videoio=ON \
    -DBUILD_opencv_calib3d=ON \
    -DBUILD_opencv_features2d=ON \
    -DBUILD_opencv_flann=ON \
    -DBUILD_opencv_highgui=ON \
    -DBUILD_opencv_dnn=ON \
    -DBUILD_opencv_photo=ON \
    -DBUILD_opencv_video=ON \
    -DBUILD_opencv_ml=OFF \
    -DBUILD_opencv_objdetect=OFF \
    -DBUILD_opencv_stitching=OFF \
    -DBUILD_opencv_videostab=OFF \
    -DBUILD_opencv_gapi=OFF \
    -DWITH_GTK=ON \
    -DWITH_V4L=ON \
    -DWITH_LIBV4L=ON \
    -DWITH_GSTREAMER=ON \
    -DWITH_FFMPEG=ON \
    -DWITH_OPENCL=ON \
    -DWITH_TBB=ON \
    -DWITH_OPENMP=OFF \
    -DWITH_IPP=OFF \
    -DWITH_JPEG=ON \
    -DWITH_PNG=ON \
    -DWITH_TIFF=ON \
    -DOPENCV_DNN_OPENCL=ON \
    -DENABLE_NEON=ON \
    $SOURCE_DIR

echo ""
echo "Configuration complete. Run: cd $BUILD_DIR && make -j\$(nproc)"
---

Save and close. Make it executable:

    chmod +x ~/opencv_cross/config.sh

Step 5: Run the configuration

    ~/opencv_cross/config.sh

If this step fails, see the Troubleshooting section at the end
of Part 1.

Step 6: Build OpenCV

    cd ~/opencv_builds/relwithdebinfo
    make -j$(nproc)

This will take 15 to 30 minutes. Output will scroll past.
Wait for it to complete and confirm there are no errors at the
end.

Step 7: Create the deployment script

    nano ~/opencv_cross/install_to_pi.sh

Substitute your Pi username and IP address in the script:

---
#!/bin/bash
set -e

BUILD_DIR=~/opencv_builds/relwithdebinfo
INSTALL_STAGING=~/opencv_install
PI_USER=<pi_username>
PI_HOST=<pi_ip_address>

echo "Installing to staging directory..."
cd $BUILD_DIR
rm -rf $INSTALL_STAGING
make install DESTDIR=$INSTALL_STAGING

echo "Creating package..."
cd $INSTALL_STAGING
tar -czf opencv_rpi5.tar.gz opt/

echo "Copying to Raspberry Pi..."
scp opencv_rpi5.tar.gz $PI_USER@$PI_HOST:~

echo "Installing on Raspberry Pi..."
ssh $PI_USER@$PI_HOST << 'ENDSSH'
    cd ~
    sudo tar -xzf opencv_rpi5.tar.gz -C /
    echo '/opt/opencv/lib' | sudo tee /etc/ld.so.conf.d/opencv.conf
    sudo ldconfig
    sudo ln -sf /opt/opencv/lib/pkgconfig/opencv4.pc \
        /usr/share/pkgconfig/opencv4.pc
    echo ""
    echo "Verifying installation..."
    pkg-config --modversion opencv4
ENDSSH

echo ""
echo "OpenCV installed to /opt/opencv on Raspberry Pi"
---

Save and close. Make it executable:

    chmod +x ~/opencv_cross/install_to_pi.sh

Step 8: Deploy OpenCV to the Pi

    ~/opencv_cross/install_to_pi.sh

When complete you should see:
    OpenCV installed to /opt/opencv on Raspberry Pi

Step 9: Sync OpenCV back to the sysroot

Now that OpenCV is installed on the Pi, run sync-sysroot.sh
again. It syncs /opt along with /lib and /usr, so OpenCV will
be pulled into your sysroot automatically:

    ~/sanuwave/sanuwave-device/scripts/sync-sysroot.sh

Then run the symlink fix again:

    sudo symlinks -rc ~/rpi-sysroots

----------------------------------------------------------------
1.6  Get the Source Code (Ubuntu)
----------------------------------------------------------------

If you received the source as a ZIP or archive file, extract it
to a directory of your choice, for example:

    mkdir -p ~/sanuwave
    cd ~/sanuwave
    unzip <path_to_archive>.zip

If you are cloning from a Git repository:

    mkdir -p ~/sanuwave
    cd ~/sanuwave
    git clone <repository_url> .

The directory structure should look like this after extracting:

    ~/sanuwave/
    ├── sanuwave-desktop/    (client source)
    ├── sanuwave-device/     (server source)
    └── shared/              (shared protocol headers)

----------------------------------------------------------------
1.7  Build the Server (Ubuntu)
----------------------------------------------------------------

Configure the build using the Trixie toolchain file:

    cd ~/sanuwave/sanuwave-device
    cmake -S . -B build \
        -DCMAKE_TOOLCHAIN_FILE=~/toolchains/aarch64-rpi5-toolchain-trixie.cmake

You should see output ending with a line similar to:
    -- Build files have been written to: .../build

If CMake reports an error about a missing package, see the
Troubleshooting section at the end of Part 1.

Now build:

    cmake --build build -j$(nproc)

When complete, verify the binary exists:

    ls -lh build/sanuwave_imaging_server

----------------------------------------------------------------
1.8  Deploy to the Raspberry Pi (Ubuntu)
----------------------------------------------------------------

There are two ways to get the server onto the Pi: a quick
direct copy of the binary (suitable for development and
testing), and a formal Debian package install (recommended for
production use). Both are described below.

METHOD A: Direct binary copy (quick)

This copies the compiled binary directly to the Pi without
packaging. It is faster but does not install the udev rules
or camera tuning file.

    ssh <pi_username>@<pi_ip_address> \
        'mkdir -p ~/sanuwave/prototype/sanuwave-device'

    scp build/sanuwave_imaging_server \
        <pi_username>@<pi_ip_address>:~/sanuwave/prototype/sanuwave-device/

To run the server:

    ssh <pi_username>@<pi_ip_address>
    ~/sanuwave/prototype/sanuwave-device/sanuwave_imaging_server

METHOD B: Debian package install (recommended)

This builds a proper installable package that includes the
binary, the camera tuning file, and the udev rules. This is
the recommended method for a production deployment.

Step 1: Build the package

    cd ~/sanuwave/sanuwave-device/build
    cpack -G DEB

A file named similar to the following will be created:

    sanuwave-imaging-server_1.0.0.xxx-xxxxxxx_arm64.deb

Step 2: Copy the package to the Pi

    scp build/*.deb <pi_username>@<pi_ip_address>:~/

Step 3: SSH into the Pi and install

    ssh <pi_username>@<pi_ip_address>
    sudo dpkg -i ~/sanuwave-imaging-server_*.deb

If dpkg reports missing dependencies, run:

    sudo apt --fix-broken install

Then repeat the dpkg command.

Step 4: Add your user to the required hardware groups

The server needs access to the camera, I2C sensors, and GPIO.
This is granted by adding your user account to three system
groups. Run the following command, replacing <pi_username>
with the account you use to log into the Pi:

    sudo usermod -aG video,i2c,gpio <pi_username>

You must log out and log back in for this to take effect:

    exit
    ssh <pi_username>@<pi_ip_address>

Verify the groups were added:

    groups

You should see video, i2c, and gpio listed in the output.

Step 5: Configure the autofocus camera overlay

If the system includes an Arducam B0190 autofocus camera, the
Raspberry Pi boot configuration must be updated to enable the
autofocus driver. Open the boot configuration file:

    sudo nano /boot/firmware/config.txt

Add the following line at the end of the file:

    dtoverlay=imx219,cam1,vcm

If the camera is connected to camera port 0 instead of port 1,
use cam0 instead:

    dtoverlay=imx219,cam0,vcm

Save and close (Ctrl+O, Enter, Ctrl+X). Reboot the Pi:

    sudo reboot

After the Pi restarts, reconnect via SSH:

    ssh <pi_username>@<pi_ip_address>

Step 6: Verify the installation

    which sanuwave_imaging_server

You should see:
    /usr/local/bin/sanuwave_imaging_server

Start the server to confirm it runs:

    sanuwave_imaging_server

You should see startup log output. Press Ctrl+C to stop it.

Log out of the Pi:

    exit

----------------------------------------------------------------
1.9  Updating After Code Changes (Ubuntu)
----------------------------------------------------------------

When the source code changes, you do not need to repeat the
OpenCV build or sysroot setup. Only repeat these steps:

  - If Pi libraries changed (e.g. after a Pi OS update):
    Repeat section 1.4 to re-sync the sysroot.

  - To rebuild and redeploy the binary:
    Repeat section 1.7 and section 1.8.

  - To do a clean rebuild from scratch:

    rm -rf ~/sanuwave/sanuwave-device/build
    cmake -S . -B build \
        -DCMAKE_TOOLCHAIN_FILE=~/toolchains/aarch64-rpi5-toolchain-trixie.cmake
    cmake --build build -j$(nproc)

----------------------------------------------------------------
1.10  Troubleshooting - Server Build
----------------------------------------------------------------

Problem: CMake reports "Could not find libcamera"
Solution: The sysroot does not contain the libcamera headers.
  On the Pi:
    sudo apt install -y libcamera-dev
  On Ubuntu, re-sync the sysroot (section 1.4).

Problem: CMake reports "Could not find OpenCV"
Solution: OpenCV is not in the sysroot. Complete section 1.5
  step 9, then delete the build directory and reconfigure:
    rm -rf build
    cmake -S . -B build \
      -DCMAKE_TOOLCHAIN_FILE=~/toolchains/aarch64-rpi5-toolchain-trixie.cmake

Problem: CMake finds x86_64 libraries instead of aarch64
Solution: pkg-config is picking up host paths. Delete the
  build directory and reconfigure:
    rm -rf build
    cmake -S . -B build \
      -DCMAKE_TOOLCHAIN_FILE=~/toolchains/aarch64-rpi5-toolchain-trixie.cmake

Problem: Compiler errors about unknown type '__time64_t'
Solution: The compiler is mixing host and sysroot headers.
  Verify that aarch64-rpi5-toolchain-trixie.cmake contains the
  -isystem lines shown in section 1.5 step 3. Delete the build
  directory and reconfigure.

Problem: dpkg reports missing dependencies after install
Solution:
    sudo apt --fix-broken install
  Then retry:
    sudo dpkg -i ~/sanuwave-imaging-server_*.deb

Problem: Missing headers after a Pi OS update
Solution: Re-sync the sysroot (section 1.4), then clean and
  rebuild (section 1.9).


================================================================
PART 2 - BUILDING THE CLIENT (SanuwaveClient)
================================================================

The client is a Qt6 desktop application. It can be built on
either Windows or Linux. Follow the section that matches your
operating system.

----------------------------------------------------------------
2.1  Prerequisites - Both Platforms
----------------------------------------------------------------

Before building, you need the following installed:

  Qt 6.10.0 or later
    Download the Qt Online Installer from:
    https://www.qt.io/download-qt-installer
    During installation, select the Qt 6.10.x component for
    your platform (Desktop, MSVC 2022 64-bit for Windows, or
    Desktop gcc 64-bit for Linux).
    Note the path where Qt is installed. You will need it
    during the CMake configuration step.

  CMake 3.16 or later
    Windows: https://cmake.org/download/
              Select "Add CMake to the system PATH" during
              installation.
    Linux:
      sudo apt install -y cmake

  Git
    Windows: https://git-scm.com/download/win
    Linux:
      sudo apt install -y git

  libjpeg-turbo (TurboJPEG)
    Windows: Download the official 64-bit Windows installer
    from:
    https://libjpeg-turbo.org/Documentation/OfficialBinaries
    Use the default installation path when prompted. The build
    system expects the library at C:\libjpeg-turbo64\.
    Linux:
      sudo apt install -y libturbojpeg0-dev

----------------------------------------------------------------
2.2  Get the Source Code
----------------------------------------------------------------

If you received the source as a ZIP or archive file, extract it
to a folder of your choice.

If you are cloning from a Git repository, open a terminal
(Command Prompt or PowerShell on Windows, Terminal on Linux)
and run:

    git clone <repository_url> sanuwave

The folder structure after extracting should look like this:

    sanuwave/
    ├── sanuwave-desktop/    (client source)
    ├── sanuwave-device/     (server source)
    └── shared/              (shared protocol headers)

All client build commands are run from inside the
sanuwave-desktop directory.

----------------------------------------------------------------
2.3  Build the Client on Windows
----------------------------------------------------------------

These steps are performed on your WINDOWS machine.

You may use any C++ build environment you prefer (Visual
Studio, Qt Creator, or the CMake command line). These
instructions use the Visual Studio Developer Command Prompt,
which sets up the MSVC compiler environment automatically.

Step 1: Open a Developer Command Prompt

  Open the Start menu and search for:
    "Developer Command Prompt for VS"

  Or navigate to:
    Start > All Programs > Visual Studio 20xx >
    Developer Command Prompt for VS 20xx

Step 2: Navigate to the client source directory

  Replace the path with wherever you extracted the source:

    cd C:\path\to\sanuwave\sanuwave-desktop

Step 3: Configure the build

  Replace the Qt path below with your actual Qt installation
  path. The path must point to the directory containing
  Qt6Config.cmake, inside the lib\cmake\Qt6 subfolder of
  your Qt installation:

    cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.10.0\msvc2022_64\lib\cmake\Qt6" -DCMAKE_BUILD_TYPE=Release

  A common Qt installation path is:
    C:\Qt\6.10.0\msvc2022_64\lib\cmake\Qt6

  If you installed Qt to a different location, adjust the path
  accordingly.

  CMake will search for Qt, TurboJPEG, and other dependencies.
  If it completes without errors you will see:
    -- Build files have been written to: ...

Step 4: Build

    cmake --build build --config Release -j

  When complete, the executable will be at:
    build\bin\SanuwaveClient.exe

Step 5: Build the Windows installer

  The installer is created using NSIS. Install NSIS first:
    https://nsis.sourceforge.io/Download

  Then, from the build directory:

    cd build
    cpack -G NSIS

  This creates a file named similar to:
    SanuwaveClient-1.0.0.xxx-xxxxxxx-win64.exe

  You can also create a ZIP archive instead:

    cpack -G ZIP

Step 6: Run the client

  The executable can be run directly without installing:
    build\bin\SanuwaveClient.exe

  Or run the NSIS installer produced in step 5, which will
  create a Start menu shortcut and a desktop icon.

----------------------------------------------------------------
2.4  Build the Client on Linux (Ubuntu)
----------------------------------------------------------------

These steps are performed on your UBUNTU machine.

Step 1: Install system build dependencies

    sudo apt update
    sudo apt install -y \
        cmake \
        git \
        build-essential \
        libturbojpeg0-dev \
        libgl1-mesa-dev

Step 2: Install Qt6

  If installing Qt via apt:

    sudo apt install -y \
        qt6-base-dev \
        qt6-tools-dev \
        qt6-tools-dev-tools \
        libqt6network6-dev

  If you installed Qt via the Qt Online Installer, note your
  installation path (e.g. ~/Qt/6.10.0/gcc_64) for use in the
  cmake command in the next step.

Step 3: Navigate to the client source directory

    cd ~/sanuwave/sanuwave-desktop

Step 4: Configure the build

  If Qt was installed via apt:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

  If Qt was installed via the Qt Online Installer:

    cmake -S . -B build \
      -DCMAKE_PREFIX_PATH=~/Qt/6.10.0/gcc_64/lib/cmake/Qt6 \
      -DCMAKE_BUILD_TYPE=Release

  When complete without errors you will see:
    -- Build files have been written to: ...

Step 5: Build

    cmake --build build -j$(nproc)

  When complete, the executable will be at:
    build/bin/SanuwaveClient

Step 6: Build the Debian package (optional)

  To create an installable .deb package:

    cd build
    cpack -G DEB

  This creates:
    SanuwaveClient-1.0.0.xxx-xxxxxxx-Linux.deb

  Install it:

    sudo dpkg -i SanuwaveClient-*.deb

Step 7: Run the client

    build/bin/SanuwaveClient

----------------------------------------------------------------
2.5  Troubleshooting - Client Build
----------------------------------------------------------------

Problem: CMake reports "Could not find Qt6"
Solution: The CMAKE_PREFIX_PATH is incorrect or Qt is not
  installed. Verify that the path you supplied contains a file
  called Qt6Config.cmake. On a default Qt Online Installer
  setup this is typically at:
    Windows: C:\Qt\6.10.0\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake
    Linux:   ~/Qt/6.10.0/gcc_64/lib/cmake/Qt6/Qt6Config.cmake
  Update CMAKE_PREFIX_PATH to match your installation and
  reconfigure.

Problem: CMake reports "TurboJPEG not found"
  Windows: Confirm libjpeg-turbo is installed at
    C:\libjpeg-turbo64\
  If it was installed to a different path, add these to the
  cmake command:
    -DTURBOJPEG_INCLUDE_DIR="C:\your\path\include"
    -DTURBOJPEG_LIBRARY="C:\your\path\lib\turbojpeg.lib"
  Linux:
    sudo apt install -y libturbojpeg0-dev

Problem: On Windows, the application shows a missing DLL error
Solution: Qt runtime DLLs are missing from the output folder.
  The build runs windeployqt automatically, but if it was
  skipped, run it manually from a Developer Command Prompt:
    "C:\Qt\6.10.0\msvc2022_64\bin\windeployqt.exe" build\bin\SanuwaveClient.exe

Problem: On Linux, the application fails with a Qt platform
  plugin error such as "could not find or load Qt platform
  plugin xcb"
Solution:
    sudo apt install -y libxcb-xinerama0 libxcb-cursor0


================================================================
PART 3 - CONNECTING THE CLIENT TO THE SERVER
================================================================

Once both components are built and installed:

  1. Ensure the Raspberry Pi is powered on and connected to
     the same network as the desktop computer.

  2. Start the server on the Pi if it is not already running:

       ssh <pi_username>@<pi_ip_address>
       sanuwave_imaging_server

     You should see startup log output indicating the server
     is listening on port 8080.

  3. Launch SanuwaveClient on the desktop.

  4. In the client Settings dialog, enter the Pi's IP address
     and port 8080.

  5. Click Connect.

If the client cannot connect, verify that no firewall on the
network or on the Pi is blocking TCP port 8080:

    ssh <pi_username>@<pi_ip_address>
    sudo ss -tlnp | grep 8080

You should see a line showing the server listening on 0.0.0.0:8080.


================================================================
APPENDIX - EXPECTED DIRECTORY STRUCTURE (Ubuntu)
================================================================

After completing all build steps, your Ubuntu machine should
have the following structure:

~/
├── rpi-sysroots/
│   └── trixie/
│       ├── usr/
│       │   ├── include/
│       │   └── lib/
│       │       └── aarch64-linux-gnu/
│       └── opt/
│           └── opencv/
├── toolchains/
│   ├── aarch64-rpi5-toolchain-common.cmake
│   ├── aarch64-rpi5-toolchain-opencv.cmake
│   └── aarch64-rpi5-toolchain-trixie.cmake
├── opencv_cross/
│   ├── opencv/               (OpenCV source)
│   ├── config.sh
│   └── install_to_pi.sh
├── opencv_builds/
│   └── relwithdebinfo/       (OpenCV build output)
├── opencv_install/           (OpenCV staging directory)
└── sanuwave/
    ├── sanuwave-desktop/
    ├── sanuwave-device/
    └── shared/
