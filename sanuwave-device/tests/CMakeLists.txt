# tests/CMakeLists.txt
# Unit tests for Sanuwave device components
# These run on the Ubuntu dev machine — no hardware required.

# Test-specific include paths (no libcamera, no OpenCV needed)
set(TEST_INCLUDES
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}                  # MockI2Cdev.h lives here
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/include
    ${CMAKE_BINARY_DIR}/generated
)

# --------------------------------------------------------------------------
# Test: I2CBus locking
# --------------------------------------------------------------------------
add_executable(test_i2c_bus
    test_i2c_bus.cpp
)

target_include_directories(test_i2c_bus PRIVATE ${TEST_INCLUDES})

target_link_libraries(test_i2c_bus
    GTest::gtest
    GTest::gtest_main
    pthread
)

add_test(NAME I2CBusTests COMMAND test_i2c_bus)

# --------------------------------------------------------------------------
# Test: AS7331 UV sensor driver (uses MockI2Cdev)
# --------------------------------------------------------------------------
add_executable(test_as7331
    test_as7331.cpp
    ${CMAKE_SOURCE_DIR}/src/as7331.cpp
)

target_include_directories(test_as7331 PRIVATE ${TEST_INCLUDES})

target_link_libraries(test_as7331
    GTest::gtest
    GTest::gtest_main
    pthread
)

add_test(NAME AS7331Tests COMMAND test_as7331)
