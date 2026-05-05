# FindTurboJPEG.cmake
# Finds the TurboJPEG library and headers
#
# This will define the following variables:
#   TURBOJPEG_FOUND        - True if TurboJPEG is found
#   TURBOJPEG_LIBRARY      - The TurboJPEG library
#   TURBOJPEG_INCLUDE_DIR  - The TurboJPEG include directory
#
# and the following imported targets:
#   TurboJPEG::TurboJPEG

# First attempt: standard find_library
find_library(TURBOJPEG_LIBRARY 
    NAMES turbojpeg
    HINTS 
        /usr/lib/aarch64-linux-gnu
        /usr/lib/x86_64-linux-gnu
        /usr/lib/arm-linux-gnueabihf
    PATHS 
        /usr/lib
        /usr/lib64
        /usr/local/lib
)

# Second attempt: if not found, try direct path checks
if(NOT TURBOJPEG_LIBRARY)
    if(EXISTS "/usr/lib/aarch64-linux-gnu/libturbojpeg.so")
        set(TURBOJPEG_LIBRARY "/usr/lib/aarch64-linux-gnu/libturbojpeg.so")
        message(STATUS "Found TurboJPEG via direct path: ${TURBOJPEG_LIBRARY}")
    elseif(EXISTS "/usr/lib/x86_64-linux-gnu/libturbojpeg.so")
        set(TURBOJPEG_LIBRARY "/usr/lib/x86_64-linux-gnu/libturbojpeg.so")
        message(STATUS "Found TurboJPEG via direct path: ${TURBOJPEG_LIBRARY}")
    elseif(EXISTS "/usr/lib/arm-linux-gnueabihf/libturbojpeg.so")
        set(TURBOJPEG_LIBRARY "/usr/lib/arm-linux-gnueabihf/libturbojpeg.so")
        message(STATUS "Found TurboJPEG via direct path: ${TURBOJPEG_LIBRARY}")
    endif()
endif()

# Find TurboJPEG header
find_path(TURBOJPEG_INCLUDE_DIR 
    NAMES turbojpeg.h
    PATHS
        /usr/include
        /usr/local/include
)

# Handle the QUIETLY and REQUIRED arguments and set TURBOJPEG_FOUND to TRUE
# if all listed variables are TRUE
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(TurboJPEG
    REQUIRED_VARS TURBOJPEG_LIBRARY
    FAIL_MESSAGE "TurboJPEG not found. Install with: sudo apt install libturbojpeg0-dev"
)

# Create imported target
if(TURBOJPEG_FOUND AND NOT TARGET TurboJPEG::TurboJPEG)
    add_library(TurboJPEG::TurboJPEG UNKNOWN IMPORTED)
    set_target_properties(TurboJPEG::TurboJPEG PROPERTIES
        IMPORTED_LOCATION "${TURBOJPEG_LIBRARY}"
    )
    if(TURBOJPEG_INCLUDE_DIR)
        set_target_properties(TurboJPEG::TurboJPEG PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${TURBOJPEG_INCLUDE_DIR}"
        )
    endif()
endif()

# Mark variables as advanced
mark_as_advanced(TURBOJPEG_LIBRARY TURBOJPEG_INCLUDE_DIR)