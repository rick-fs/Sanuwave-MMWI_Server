cmake_minimum_required(VERSION 3.10)
project(sanuwave_imaging_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall")
add_compile_options(
    -Wunused-function
    -Wunused-parameter
    -Wunused-variable
    -Wunused-but-set-variable
    -Wunreachable-code
    -Wall
    -Wextra
)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
message(STATUS "CMAKE_MODULE_PATH: ${CMAKE_MODULE_PATH}")

# ============================================================================
# Common driver library path (Scott's shared driver repo)
# ============================================================================

set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../MMWI_PIC_common)
# ============================================================================
# Build Options
# ============================================================================

option(BUILD_TESTS "Build unit tests" OFF)
option(BUILD_SERVER "Build the main server executable" ON)

# ============================================================================
# Git Version Information
# ============================================================================

set(VERSION_MAJOR 1)
set(VERSION_MINOR 0)
set(VERSION_PATCH 0)

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_SHA_RESULT
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_COUNT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_COUNT_RESULT
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} status --porcelain
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_DIRTY_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(NOT GIT_SHA_RESULT EQUAL 0)
        set(GIT_SHA "unknown")
        set(GIT_COMMIT_COUNT "0")
    endif()

    if(NOT GIT_BRANCH)
        set(GIT_BRANCH "unknown")
    endif()

    if(GIT_DIRTY_OUTPUT)
        set(GIT_DIRTY_SUFFIX "-dirty")
    else()
        set(GIT_DIRTY_SUFFIX "")
    endif()
else()
    set(GIT_SHA "unknown")
    set(GIT_COMMIT_COUNT "0")
    set(GIT_BRANCH "unknown")
    set(GIT_DIRTY_SUFFIX "")
endif()

string(TIMESTAMP BUILD_TIME "%Y-%m-%dT%H:%M:%S")

set(VERSION_BUILD ${GIT_COMMIT_COUNT})
set(PROJECT_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}.${VERSION_BUILD}")
set(PROJECT_VERSION_FULL "${PROJECT_VERSION}-${GIT_SHA}${GIT_DIRTY_SUFFIX}")

message(STATUS "Building sanuwave-imaging-server version: ${PROJECT_VERSION_FULL}")

# Generate version header for runtime access
configure_file(
    ${CMAKE_SOURCE_DIR}/src/version.h.in
    ${CMAKE_BINARY_DIR}/generated/version.h
    @ONLY
)

# ============================================================================
# Common include directories (shared by server and tests)
# ============================================================================

set(COMMON_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/include
    ${COMMON_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/vl53l4cd
    ${CMAKE_SOURCE_DIR}/sdk
    ${CMAKE_BINARY_DIR}/generated
)

# ============================================================================
# Server Build (only when targeting Pi or BUILD_SERVER is ON)
# ============================================================================

if(BUILD_SERVER)
    # Find OpenCV
    set(OpenCV_DIR /opt/opencv/lib/cmake/opencv4)
    find_package(OpenCV REQUIRED)

    # Find libcamera
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBCAMERA REQUIRED libcamera)

    # Find TurboJPEG using custom Find module
    find_package(TurboJPEG REQUIRED)

    # FLIR Lepton SDK sources (third-party vendor code)
    # Warnings suppressed for SDK files only - these are provided by FLIR and contain
    # stub functions with unused parameters. This is standard vendor SDK behavior and
    # does not affect device operation or safety.
    set(LEPTON_SDK_SOURCES
        sdk/LEPTON_SDK.c
        sdk/LEPTON_I2C_Protocol.c
        sdk/LEPTON_I2C_Service.c
        sdk/LEPTON_AGC.c
        sdk/LEPTON_OEM.c
        sdk/LEPTON_SYS.c
        sdk/LEPTON_VID.c
        sdk/crc16fast.c
        sdk/raspi_I2C.c
    )
    set_source_files_properties(${LEPTON_SDK_SOURCES} PROPERTIES COMPILE_FLAGS "-w")

    set(COMMON_SOURCES
        ${COMMON_DIR}/Gpio.cpp
        ${COMMON_DIR}/I2cMgr.cpp
        ${COMMON_DIR}/AS7331.cpp
        ${COMMON_DIR}/VL53L4CD.cpp
        ${COMMON_DIR}/VL53L4CD_platform.cpp
        ${COMMON_DIR}/ToFSensor.cpp
        ${COMMON_DIR}/LedMgr.cpp
        ${COMMON_DIR}/LM3643.cpp
        ${COMMON_DIR}/PCA9545A.cpp
        ${COMMON_DIR}/VD6283TX.cpp
        ${COMMON_DIR}/Lsm6I2cBus.cpp
        ${COMMON_DIR}/Lsm6ds3trc.cpp
    )

    set(SOURCES
        src/main.cpp
        src/camera_base.cpp 
        src/camera_factory.cpp 
        src/camera_imx708.cpp
        src/camera_manager.cpp
        src/thermal_camera.cpp
        src/lepton3.cpp
        src/camera_imx219.cpp
        src/tcp_server.cpp        
        src/command_handler.cpp
        src/command_router.cpp
        src/multi_camera_parameter_handler.cpp
        src/vl53l4cd_wrapper.cpp
        src/path_utils.cpp
        src/turbo_jpeg_encoder.cpp
        src/jpeg_turbo_impl.cpp
        src/jpeg_opencv_impl.cpp 
        src/jpeg_encoder_factory.cpp 
        src/jpeg_opencv_impl.cpp 
        src/as7331_wrapper.cpp 
        src/sensor_info.cpp
        src/sensor_timing_calculator.cpp
        src/diagnostic_capture.cpp
        src/led_gpio_controller.cpp
        src/param_extractor.cpp
        src/rgb_stream_worker.cpp
        src/thermal_stream_worker.cpp
        src/temp_monitor.cpp
        src/capture_session.cpp
        src/vblank_strobe_controller.cpp
        src/vd6283tx_wrapper.cpp
        src/lsm6ds3trc_wrapper.cpp
    )

    set(HEADERS
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/vl53l4cd
        ${CMAKE_SOURCE_DIR}/sdk
    )

    add_executable(sanuwave_imaging_server ${SOURCES} ${COMMON_SOURCES} ${VL53L4CD_SOURCES} ${LEPTON_SDK_SOURCES})

    target_include_directories(sanuwave_imaging_server PRIVATE
        ${COMMON_INCLUDES}
        ${OpenCV_INCLUDE_DIRS}
        ${LIBCAMERA_INCLUDE_DIRS}
        ${TURBOJPEG_INCLUDE_DIR}
        ${HEADERS}
    )

    target_link_libraries(sanuwave_imaging_server
        ${OpenCV_LIBS}
        ${LIBCAMERA_LIBRARIES}
        TurboJPEG::TurboJPEG
        pthread
    )

    # Installation Rules
    install(TARGETS sanuwave_imaging_server RUNTIME DESTINATION bin)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/99-sanuwave.rules"
        DESTINATION lib/udev/rules.d)
    install(FILES "${CMAKE_SOURCE_DIR}/tuning/imx219_noir_af.json"
        DESTINATION share/sanuwave/tuning)
    install(DIRECTORY packaging/dkms/dw9714
        DESTINATION share/sanuwave/dkms
        FILES_MATCHING
        PATTERN "*.c"
        PATTERN "*.h"
        PATTERN "Makefile"
        PATTERN "dkms.conf"
        PATTERN "*.patch"
        PATTERN "UPSTREAM_COMMIT"
     )
     install(DIRECTORY packaging/dkms/rp1-cfe
        DESTINATION share/sanuwave/dkms
        FILES_MATCHING
        PATTERN "*.c"
        PATTERN "*.h"
        PATTERN "Makefile"
        PATTERN "dkms.conf"
        PATTERN "*.patch"
        PATTERN "UPSTREAM_COMMIT"
    )  
        
    install(FILES
        packaging/overlays/imx219-b0190-vcm.dtbo
        DESTINATION share/sanuwave/overlays
    )

    # CPack Configuration
    set(CPACK_PACKAGE_NAME "sanuwave-imaging-server")
    set(CPACK_PACKAGE_VENDOR "Sanuwave")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Sanuwave Medical Imaging Server for Raspberry Pi")
    set(CPACK_PACKAGE_VERSION_MAJOR ${VERSION_MAJOR})
    set(CPACK_PACKAGE_VERSION_MINOR ${VERSION_MINOR})
    set(CPACK_PACKAGE_VERSION_PATCH ${VERSION_PATCH})
    set(CPACK_PACKAGE_VERSION_TWEAK ${VERSION_BUILD})
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION_FULL})
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Sanuwave")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS 
    "libc6, libstdc++6, libcamera0.6, libturbojpeg0, i2c-tools, dkms, build-essential, patch, linux-headers-rpi-2712")
    set(CPACK_DEBIAN_PACKAGE_CONFLICTS "sanuwave-server")
    set(CPACK_DEBIAN_PACKAGE_REPLACES "sanuwave-server")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA 
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/postinst"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/preinst"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/prerm")
    set(CPACK_DEBIAN_FILE_NAME "${CPACK_PACKAGE_NAME}_${PROJECT_VERSION_FULL}_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}.deb")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")
    include(CPack)
endif()

# ============================================================================
# Unit Tests (run on Ubuntu dev machine, no hardware dependencies)
# ============================================================================

if(BUILD_TESTS)
    enable_testing()

    # Fetch GoogleTest
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    # Prevent GoogleTest from overriding our compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    # Include test subdirectory
    add_subdirectory(tests)
endif()
