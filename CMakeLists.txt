cmake_minimum_required(VERSION 3.24)
project(lewitt-connect2 VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

# Packagers get an optimised build unless they say otherwise.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LEWITT_BUILD_GUI "Build the Qt 6 / QML application" ON)
option(LEWITT_BUILD_TESTS "Build tests (needs GoogleTest)" ON)
option(LEWITT_ENABLE_USB "Build the libusb hardware transport (OFF = mock-only build)" ON)
option(LEWITT_INSTALL_UDEV_RULES "Install the udev rule (system prefixes only; needs admin)" ON)
option(LEWITT_REPRODUCIBLE "Strip build/source paths from binaries (-ffile-prefix-map)" ON)
set(LEWITT_UDEV_RULES_DIR "lib/udev/rules.d" CACHE STRING
    "udev rule destination (relative to the install prefix, or absolute)")
# Debian-style doc directory named after the package
if(NOT DEFINED CMAKE_INSTALL_DOCDIR OR CMAKE_INSTALL_DOCDIR MATCHES "lewitt-connect2$")
    set(CMAKE_INSTALL_DOCDIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/lewitt-connect2-unofficial")
endif()
set(LEWITT_BUILD_FLAVOUR "full")
if(NOT LEWITT_ENABLE_USB)
    set(LEWITT_BUILD_FLAVOUR "mock-only")
endif()
if(LEWITT_REPRODUCIBLE)
    add_compile_options(-ffile-prefix-map=${CMAKE_SOURCE_DIR}=. -ffile-prefix-map=${CMAKE_BINARY_DIR}=.build)
endif()
configure_file(src/version.hpp.in ${CMAKE_BINARY_DIR}/generated/lewitt/version.hpp @ONLY)
# -DLEWITT_SANITIZE=address|undefined|thread|address,undefined (development builds only)
set(LEWITT_SANITIZE "" CACHE STRING "Sanitizers to enable")
if(LEWITT_SANITIZE)
    add_compile_options(-fsanitize=${LEWITT_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${LEWITT_SANITIZE})
endif()

if(LEWITT_ENABLE_USB)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
    set(LEWITT_USB_SOURCES src/transport/UsbTransport.cpp)
else()
    set(LEWITT_USB_SOURCES src/transport/UsbTransportStub.cpp)
endif()

# Qt-free core: protocol + transports + session
add_library(lewitt_core STATIC
    src/protocol/Messages.cpp
    src/transport/MockTransport.cpp
    ${LEWITT_USB_SOURCES}
    src/transport/UsbErrors.cpp
    src/transport/UsbDiagnostics.cpp
    src/transport/SerializedTransport.cpp
    src/protocol/WriteGuard.cpp
    src/core/DeviceSession.cpp
    src/core/DeviceManager.cpp)
target_include_directories(lewitt_core PUBLIC src ${CMAKE_BINARY_DIR}/generated)
target_compile_options(lewitt_core PRIVATE -Wall -Wextra -Wpedantic)
if(LEWITT_ENABLE_USB)
    target_link_libraries(lewitt_core PUBLIC PkgConfig::LIBUSB)
    target_compile_definitions(lewitt_core PUBLIC LEWITT_HAVE_USB=1)
endif()

add_executable(lewitt-cli src/cli/main.cpp)
target_link_libraries(lewitt-cli PRIVATE lewitt_core)
install(TARGETS lewitt-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(LEWITT_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_executable(lewitt_tests tests/cpp/test_protocol.cpp tests/cpp/test_session.cpp tests/cpp/test_outputs.cpp tests/cpp/test_recovered.cpp tests/cpp/test_hardening.cpp)
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

# Visual geometry regression (needs Pillow/numpy from the project venv; skipped otherwise)
if(LEWITT_BUILD_TESTS AND LEWITT_BUILD_GUI AND EXISTS ${CMAKE_SOURCE_DIR}/.venv/bin/python)
    add_test(NAME visual_geometry
             COMMAND ${CMAKE_SOURCE_DIR}/.venv/bin/python ${CMAKE_SOURCE_DIR}/tools/visual/check_geometry.py
                     $<TARGET_FILE:lewitt-control-center>)
    set_tests_properties(visual_geometry PROPERTIES ENVIRONMENT "QML_IMPORT_PATH=$ENV{QML_IMPORT_PATH}")
endif()

# Static guard over every source location that can reach USB hardware.
if(LEWITT_BUILD_TESTS AND UNIX)
    add_test(NAME audit_usb_call_sites
             COMMAND ${CMAKE_SOURCE_DIR}/tools/audit/check_usb_call_sites.sh ${CMAKE_SOURCE_DIR})
endif()

# Packaging check: install into an isolated DESTDIR and verify layout, RPATH, dev-path
# leakage, desktop/appstream metadata and a headless simulated start (no host install).
if(LEWITT_BUILD_TESTS AND LEWITT_BUILD_GUI AND UNIX AND NOT LEWITT_SANITIZE)   # sanitizer binaries are not package candidates
    add_test(NAME packaging_staging
             COMMAND ${CMAKE_SOURCE_DIR}/tools/packaging/check_staging.sh ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR})
    set_tests_properties(packaging_staging PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QML_IMPORT_PATH=$ENV{QML_IMPORT_PATH}" TIMEOUT 180)
endif()

# Benchmarks (not part of ctest): cmake --build build --target lewitt_bench
add_executable(lewitt_bench EXCLUDE_FROM_ALL tests/bench/bench_core.cpp)
target_link_libraries(lewitt_bench PRIVATE lewitt_core)

# ---- installation ---------------------------------------------------------------
# Everything is relocatable: nothing refers to the build or source tree. Use
#   cmake --install build --prefix ~/.local          (user-local, no root)
#   DESTDIR=/tmp/stage cmake --install build         (staging for packages)
set(LEWITT_ICON_DIR ${CMAKE_INSTALL_DATADIR}/icons/hicolor)
install(FILES packaging/linux/icons/lewitt-control-center.svg
        DESTINATION ${LEWITT_ICON_DIR}/scalable/apps)
foreach(sz 48 64 128 256)
    install(FILES packaging/linux/icons/lewitt-control-center-${sz}.png
            DESTINATION ${LEWITT_ICON_DIR}/${sz}x${sz}/apps
            RENAME lewitt-control-center.png)
endforeach()
install(FILES packaging/linux/lewitt-control-center.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/linux/lewitt-control-center.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES README.md docs/INSTALLATION.md docs/USB_PERMISSIONS.md docs/USB_ACCESS.md
              docs/HARDWARE_TEST_PLAN.md PROTOCOL.md FEATURES.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES packaging/udev/70-lewitt-connect2.rules DESTINATION ${CMAKE_INSTALL_DOCDIR}/udev)

# Debian changelog and man pages are compressed at configure time with a timestamp-free
# gzip (-n) so the output is reproducible.
find_program(LEWITT_GZIP gzip)
if(LEWITT_GZIP)
    set(_gzdir ${CMAKE_BINARY_DIR}/packaging-gz)
    file(MAKE_DIRECTORY ${_gzdir})
    foreach(_f debian/changelog man/lewitt-cli.1 man/lewitt-control-center.1)
        get_filename_component(_n ${_f} NAME)
        if(_n STREQUAL "changelog")
            set(_out ${_gzdir}/changelog.Debian.gz)
        else()
            set(_out ${_gzdir}/${_n}.gz)
        endif()
        execute_process(COMMAND ${LEWITT_GZIP} -9nc ${CMAKE_SOURCE_DIR}/packaging/${_f}
                        OUTPUT_FILE ${_out} RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "gzip failed for ${_f}")
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/packaging/${_f})
    endforeach()
    install(FILES ${_gzdir}/changelog.Debian.gz DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(FILES ${_gzdir}/lewitt-cli.1.gz ${_gzdir}/lewitt-control-center.1.gz
            DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
endif()
if(LEWITT_INSTALL_UDEV_RULES)
    install(FILES packaging/udev/70-lewitt-connect2.rules DESTINATION ${LEWITT_UDEV_RULES_DIR})
endif()
include(packaging/CPackConfig.cmake)
