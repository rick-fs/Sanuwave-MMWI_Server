set(CMAKE_CXX_FLAGS_INIT "" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "" CACHE STRING "" FORCE)

cmake_minimum_required(VERSION 3.16)
project(SanuwaveClient VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/EHsc)
endif()

# =============================================================================
# Version information from Git
# =============================================================================

find_package(Git QUIET)
if(GIT_FOUND)
    # Short hash (7 chars)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    # Full hash
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_HASH_FULL
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    # Latest tag
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    # Commit count (total commits on current branch)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_COUNT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    # Check for uncommitted changes
    execute_process(
        COMMAND ${GIT_EXECUTABLE} status --porcelain
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_DIRTY
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(GIT_DIRTY)
        set(GIT_DIRTY_SUFFIX "-dirty")
    else()
        set(GIT_DIRTY_SUFFIX "")
    endif()
    
    # Get branch name
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
else()
    message(WARNING "Git not found - version info will be incomplete")
endif()

# Set defaults if Git info not available
if(NOT GIT_HASH OR "${GIT_HASH}" STREQUAL "")
    set(GIT_HASH "unknown")
    set(GIT_HASH_FULL "unknown")
endif()
if(NOT GIT_COMMIT_COUNT OR "${GIT_COMMIT_COUNT}" STREQUAL "")
    set(GIT_COMMIT_COUNT "0")
endif()
if(NOT GIT_BRANCH OR "${GIT_BRANCH}" STREQUAL "")
    set(GIT_BRANCH "unknown")
endif()

# Build version strings
set(VERSION_STRING "v${PROJECT_VERSION}.${GIT_COMMIT_COUNT}.${GIT_HASH}${GIT_DIRTY_SUFFIX}")
set(VERSION_DISPLAY "v${PROJECT_VERSION} (build ${GIT_COMMIT_COUNT})")
set(GIT_VERSION_NUM ${GIT_COMMIT_COUNT})

message(STATUS "==============================================")
message(STATUS "Version String:  ${VERSION_STRING}")
message(STATUS "Version Display: ${VERSION_DISPLAY}")
message(STATUS "Git Hash:        ${GIT_HASH}")
message(STATUS "Git Commits:     ${GIT_COMMIT_COUNT}")
message(STATUS "Git Branch:      ${GIT_BRANCH}")
message(STATUS "==============================================")

# =============================================================================
# Platform-specific setup
# =============================================================================

if(WIN32)
    set(CMAKE_CUDA_COMPILER "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.0/bin/nvcc.exe" CACHE FILEPATH "CUDA compiler")
    # Qt6 prefix path: resolved in priority order:
    #   1. Already set (e.g. via CMakeUserPresets.json or -DCMAKE_PREFIX_PATH=...)
    #   2. QT6_DIR environment variable
    #   3. Hard-coded fallback (last resort — do not rely on this in shared builds)
    if(NOT CMAKE_PREFIX_PATH)
        if(DEFINED ENV{QT6_DIR})
            set(CMAKE_PREFIX_PATH "$ENV{QT6_DIR}" CACHE PATH "Qt6 path (from QT6_DIR env)")
            message(STATUS "Qt6 path from QT6_DIR: $ENV{QT6_DIR}")
        else()
            message(WARNING "CMAKE_PREFIX_PATH not set and QT6_DIR env var not found. "
                            "Set QT6_DIR or add a CMakeUserPresets.json. "
                            "See CMakePresets.json for an example.")
        endif()
    endif()
else()
    set(CMAKE_CUDA_COMPILER "/usr/local/cuda/bin/nvcc")
    set(CMAKE_AUTOMOC_MOC_EXECUTABLE "/usr/lib/qt6/libexec/moc" CACHE FILEPATH "Qt6 moc" FORCE)
    set(CMAKE_PREFIX_PATH "/usr/lib/x86_64-linux-gnu/cmake/Qt6")
endif()





# =============================================================================
# Qt6 setup
# =============================================================================

set(QT_VERSION_MAJOR 6)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Network Concurrent)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# =============================================================================
# Find TurboJPEG
# =============================================================================

find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(TURBOJPEG libturbojpeg)
endif()

if(NOT TURBOJPEG_FOUND)
    find_path(TURBOJPEG_INCLUDE_DIR turbojpeg.h
        PATHS
            /usr/include
            /usr/local/include
            "C:/libjpeg-turbo64/include"
            "C:/Program Files/libjpeg-turbo64/include"
    )
    
    find_library(TURBOJPEG_LIBRARY
        NAMES turbojpeg turbojpeg-static
        PATHS
            /usr/lib
            /usr/local/lib
            /usr/lib/x86_64-linux-gnu
            "C:/libjpeg-turbo64/lib"
            "C:/Program Files/libjpeg-turbo64/lib"
    )
    
    if(TURBOJPEG_INCLUDE_DIR AND TURBOJPEG_LIBRARY)
        set(TURBOJPEG_FOUND TRUE)
        set(TURBOJPEG_INCLUDE_DIRS ${TURBOJPEG_INCLUDE_DIR})
        set(TURBOJPEG_LIBRARIES ${TURBOJPEG_LIBRARY})
        message(STATUS "Found TurboJPEG: ${TURBOJPEG_LIBRARY}")
    else()
        message(FATAL_ERROR "TurboJPEG not found. Please install libjpeg-turbo development package.")
    endif()
endif()

# =============================================================================
# Source files
# =============================================================================

set(SOURCES
    # Core
    main.cpp
    mainwindow.cpp
    settingsdialog.cpp
    aboutdialog.cpp
    calibration_viewer_dialog.cpp
    # Network
    server_connection.cpp
    # UI Widgets
    imageviewerwindow.cpp
    expandablegroupbox.cpp
    imagedisplaywidget.cpp
    cameratestpanel.cpp
    cameratestrunner.cpp
    # Image processing
    image_decoding.cpp
    turbo_jpeg_decoding.cpp
    raw_bayer_decoding.cpp
    dng_exporter.cpp
    stream_frame_decoder.cpp
    # Camera parameters
    camera_param_router.cpp
    camera_ui_controller.cpp
    sensor_calibration.cpp
    camera_settings_manager.cpp
    # Raw diagnostics
    bayer_image_widget.cpp
    channel_histogram_widget.cpp
    raw_diag_window.cpp
    # Lens calibration
    lens_calibration_dialog.cpp
    # Session management
    session_manager.cpp
    # UVBF 
    uvbf_capture_dialog.cpp
    uvbf_vblank_dialog.cpp
    imu_config_dialog.cpp 
)

set(HEADERS
    # Core
    mainwindow.h
    settingsdialog.h
    logger.h
    version.h
    aboutdialog.h
    calibration_viewer_dialog.h
    # Network
    server_connection.h
    # UI Widgets
    imageviewerwindow.h
    expandablegroupbox.h
    imagedisplaywidget.h
    cameratestpanel.h
    cameratestrunner.h
    # Image processing
    image_decoding.h
    turbo_jpeg_decoding.h
    raw_bayer_decoding.h
    dng_exporter.h
    stream_frame_decoder.h
    # Camera parameters
    camera_param_router.h
    camera_ui_controller.h
    sensor_calibration.h
    camera_settings_manager.h
    # Raw diagnostics
    bayer_image_widget.h
    raw_diag_window.h
    channel_histogram_widget.h
    # Lens calibration
    lens_calibration_dialog.h
    # Session management
    session_manager.h
    # UVBF
    uvbf_capture_dialog.h
    median_wirth.h
    uvbf_vblank_dialog.h
    imu_config_dialog.h 
)

qt6_add_resources(RESOURCE_FILES resources/resources.qrc)

# =============================================================================
# Windows version resource
# =============================================================================

if(WIN32)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/version.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/version.rc
        @ONLY
    )
    set(VERSION_RC ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
endif()

# =============================================================================
# Create executable
# =============================================================================

if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS} ${VERSION_RC} ${RESOURCE_FILES})
else()
    add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${RESOURCE_FILES})
endif()

# =============================================================================
# Compile definitions (version info)
# =============================================================================




target_compile_definitions(${PROJECT_NAME} PRIVATE
    VERSION_STRING="${VERSION_STRING}"
    VERSION_DISPLAY="${VERSION_DISPLAY}"
    GIT_HASH="${GIT_HASH}"
    GIT_HASH_FULL="${GIT_HASH_FULL}"
    GIT_COMMIT_COUNT=${GIT_COMMIT_COUNT}
    GIT_BRANCH="${GIT_BRANCH}"
    QT_DISABLE_DEPRECATED_BEFORE=0x060000
    $<$<BOOL:${ENABLE_TESTS}>:ENABLE_TESTS>
)

if(ENABLE_TESTS)
    target_sources(SanuwaveClient PRIVATE test_server_connection.cpp)
endif()

# =============================================================================
# Include directories and linking
# =============================================================================

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/include
    ${TURBOJPEG_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Concurrent
    ${TURBOJPEG_LIBRARIES}
)

# =============================================================================
# Windows-specific post-build
# =============================================================================

if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
    get_target_property(QMAKE_EXECUTABLE Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QMAKE_EXECUTABLE}" DIRECTORY)
    
    # Deploy Qt dependencies
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${QT_BIN_DIR}/windeployqt.exe"
            --no-compiler-runtime
            --no-translations
            "$<TARGET_FILE:${PROJECT_NAME}>"
        COMMENT "Deploying Qt dependencies..."
    )
    
    # Copy TurboJPEG DLLs
    if(EXISTS "${TURBOJPEG_LIBRARY}")
        get_filename_component(TURBOJPEG_LIB_DIR "${TURBOJPEG_LIBRARY}" DIRECTORY)
        get_filename_component(TURBOJPEG_ROOT "${TURBOJPEG_LIB_DIR}" DIRECTORY)
        set(TURBOJPEG_BIN_DIR "${TURBOJPEG_ROOT}/bin")

        file(GLOB TURBOJPEG_DLLS "${TURBOJPEG_BIN_DIR}/*.dll")
        foreach(DLL ${TURBOJPEG_DLLS})
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DLL}"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
                COMMENT "Copying ${DLL}..."
            )
        endforeach()
    endif()
endif()

# Output directory
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# =============================================================================
# Install rules
# =============================================================================

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)

if(WIN32)
    get_target_property(QMAKE_EXECUTABLE Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QMAKE_EXECUTABLE}" DIRECTORY)
    
    # Install TurboJPEG DLLs
    if(EXISTS "${TURBOJPEG_LIBRARY}")
        get_filename_component(TURBOJPEG_LIB_DIR "${TURBOJPEG_LIBRARY}" DIRECTORY)
        get_filename_component(TURBOJPEG_ROOT "${TURBOJPEG_LIB_DIR}" DIRECTORY)
        set(TURBOJPEG_BIN_DIR "${TURBOJPEG_ROOT}/bin")
        file(GLOB TURBOJPEG_DLLS "${TURBOJPEG_BIN_DIR}/*.dll")
        if(TURBOJPEG_DLLS)
            install(FILES ${TURBOJPEG_DLLS} DESTINATION bin)
        endif()
    endif()
    
    # Run windeployqt at install time
    install(CODE "
        execute_process(
            COMMAND \"${QT_BIN_DIR}/windeployqt.exe\"
                --no-compiler-runtime
                --no-translations
                \"\${CMAKE_INSTALL_PREFIX}/bin/${PROJECT_NAME}.exe\"
        )
    ")
endif()

# =============================================================================
# CPack configuration
# =============================================================================

set(CPACK_PACKAGE_NAME "SanuwaveClient")
set(CPACK_PACKAGE_VENDOR "Sanuwave")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Sanuwave Medical Imaging Client")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}.${GIT_COMMIT_COUNT}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Sanuwave Client")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}.${GIT_COMMIT_COUNT}-${GIT_HASH}${GIT_DIRTY_SUFFIX}")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
endif()

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "Sanuwave Client ${VERSION_DISPLAY}")
    set(CPACK_NSIS_PACKAGE_NAME "Sanuwave Client ${VERSION_DISPLAY}")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Sanuwave Client.lnk' '$INSTDIR\\\\bin\\\\${PROJECT_NAME}.exe'"
    )
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "Delete '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Sanuwave Client.lnk'"
    )
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
        "CreateShortCut '$DESKTOP\\\\Sanuwave Client.lnk' '$INSTDIR\\\\bin\\\\${PROJECT_NAME}.exe'"
    )
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
        "Delete '$DESKTOP\\\\Sanuwave Client.lnk'"
    )
elseif(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Sanuwave")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libqt6core6, libqt6widgets6, libqt6network6, libturbojpeg0")
endif()

include(CPack)
