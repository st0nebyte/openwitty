cmake_minimum_required(VERSION 3.24)
project(lewitt-connect2 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LEWITT_BUILD_GUI "Build the Qt 6 / QML application" ON)
option(LEWITT_BUILD_TESTS "Build tests" ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

# Qt-free core: protocol + transports + session
add_library(lewitt_core STATIC
    src/protocol/Messages.cpp
    src/transport/MockTransport.cpp
    src/transport/UsbTransport.cpp
    src/core/DeviceSession.cpp)
target_include_directories(lewitt_core PUBLIC src)
target_compile_options(lewitt_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(lewitt_core PUBLIC PkgConfig::LIBUSB)

add_executable(lewitt-cli src/cli/main.cpp)
target_link_libraries(lewitt-cli PRIVATE lewitt_core)

if(LEWITT_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_executable(lewitt_tests tests/cpp/test_protocol.cpp tests/cpp/test_session.cpp tests/cpp/test_outputs.cpp tests/cpp/test_recovered.cpp)
    target_link_libraries(lewitt_tests PRIVATE lewitt_core GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(lewitt_tests)
endif()

if(LEWITT_BUILD_GUI)
    add_subdirectory(src/gui)
endif()

# Legacy Python prototype tests (reverse-engineering utility, not production)
if(LEWITT_BUILD_TESTS)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME python_legacy_protocol
                 COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_SOURCE_DIR}/tools/py)
    endif()
endif()
