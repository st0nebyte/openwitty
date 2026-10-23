cmake_minimum_required(VERSION 3.24)
project(openwitty VERSION 0.1.0 LANGUAGES CXX)

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
if(NOT DEFINED CMAKE_INSTALL_DOCDIR OR CMAKE_INSTALL_DOCDIR MATCHES "lewitt-connect2$|lewitt-connect2-unofficial$")
    set(CMAKE_INSTALL_DOCDIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/openwitty")
endif()
set(LEWITT_BUILD_FLAVOUR "full")
if(NOT LEWITT_ENABLE_USB)
    set(LEWITT_BUILD_FLAVOUR "mock-only")
endif()
if(LEWITT_REPRODUCIBLE)
    add_compile_options(-ffile-prefix-map=${CMAKE_SOURCE_DIR}=. -ffile-prefix-map=${CMAKE_BINARY_DIR}=.build)
    # A build-tree RPATH (e.g. the rootless .sysroot) would be rewritten in place at install
    # time and leave directory-length dependent padding in .dynstr, making the installed
    # binaries differ between build directories. Development runs use LD_LIBRARY_PATH.
    set(CMAKE_SKIP_BUILD_RPATH ON)
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
    src/transport/ReplayTransport.cpp
    src/transport/ControlLease.cpp
    ${LEWITT_USB_SOURCES}
    src/transport/UsbErrors.cpp
    src/transport/InterfaceSelection.cpp
    src/transport/UsbDiagnostics.cpp
    src/transport/SerializedTransport.cpp
    src/protocol/WriteGuard.cpp
    src/core/DeviceSession.cpp
    src/core/WriteVerification.cpp
    src/core/DeviceManager.cpp)
target_include_directories(lewitt_core PUBLIC src ${CMAKE_BINARY_DIR}/generated)
target_compile_options(lewitt_core PRIVATE -Wall -Wextra -Wpedantic)
if(LEWITT_ENABLE_USB)
    target_link_libraries(lewitt_core PUBLIC PkgConfig::LIBUSB)
    target_compile_definitions(lewitt_core PUBLIC LEWITT_HAVE_USB=1)
endif()

add_executable(openwitty-cli src/cli/main.cpp)
target_link_libraries(openwitty-cli PRIVATE lewitt_core)
install(TARGETS openwitty-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(LEWITT_ENABLE_USB)
    # Approval-gated, short read-only status sampling; not part of the GUI polling policy.
    add_executable(openwitty-meter-probe tools/diagnostics/meter_probe.cpp)
    target_link_libraries(openwitty-meter-probe PRIVATE lewitt_core)
    if(LEWITT_BUILD_TESTS)
        add_test(NAME meter_probe_mock_4hz
                 COMMAND openwitty-meter-probe --mock --rate 4 --duration 1)
        add_test(NAME meter_probe_mock_8hz
                 COMMAND openwitty-meter-probe --mock --rate 8 --duration 1)
        add_test(NAME meter_probe_mock_40hz
                 COMMAND openwitty-meter-probe --mock --rate 40 --duration 1)
        add_test(NAME meter_probe_mock_48hz
                 COMMAND openwitty-meter-probe --mock --rate 48 --duration 1)
        add_test(NAME meter_probe_real_dry_run
                 COMMAND openwitty-meter-probe --rate 40 --duration 10)
        add_test(NAME meter_probe_48hz_dry_run
                 COMMAND openwitty-meter-probe --rate 48 --duration 10)
        set_tests_properties(meter_probe_mock_4hz PROPERTIES PASS_REGULAR_EXPRESSION "status_gets=4 errors=0")
        set_tests_properties(meter_probe_mock_8hz PROPERTIES PASS_REGULAR_EXPRESSION "status_gets=8 errors=0")
        set_tests_properties(meter_probe_mock_40hz PROPERTIES PASS_REGULAR_EXPRESSION "status_gets=40 errors=0")
        set_tests_properties(meter_probe_mock_48hz PROPERTIES PASS_REGULAR_EXPRESSION "status_gets=48 errors=0")
        set_tests_properties(meter_probe_real_dry_run PROPERTIES PASS_REGULAR_EXPRESSION "at most 400 status GETs")
        set_tests_properties(meter_probe_48hz_dry_run PROPERTIES PASS_REGULAR_EXPRESSION "at most 480 status GETs")
    endif()
endif()
# Compatibility alias for the pre-rebranding command name (docs, scripts, muscle memory).
install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink openwitty-cli \
    \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/lewitt-cli)")

if(LEWITT_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_executable(lewitt_tests tests/cpp/test_protocol.cpp tests/cpp/test_session.cpp tests/cpp/test_outputs.cpp tests/cpp/test_recovered.cpp tests/cpp/test_hardening.cpp tests/cpp/test_write_verification.cpp)
    target_sources(lewitt_tests PRIVATE tests/cpp/test_hardware_scope.cpp)   # Phase 6E
    target_sources(lewitt_tests PRIVATE tests/cpp/test_interface_selection.cpp tests/cpp/test_retry_policy.cpp)   # Phase 7A
    target_sources(lewitt_tests PRIVATE tests/cpp/test_replay_transport.cpp)   # Phase 7B read-only fixtures
    target_sources(lewitt_tests PRIVATE tests/cpp/test_control_lease.cpp)       # Phase 7D process ownership
    target_sources(lewitt_tests PRIVATE tests/cpp/test_read_freshness.cpp)     # Phase 7E independent read ages
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
                     $<TARGET_FILE:openwitty>)
    set_tests_properties(visual_geometry PROPERTIES ENVIRONMENT "QML_IMPORT_PATH=$ENV{QML_IMPORT_PATH}")
endif()

# CLI write scope (mock only): exactly two single-field write commands, each separately gated.
if(LEWITT_BUILD_TESTS AND UNIX AND LEWITT_ENABLE_USB)
    add_test(NAME cli_write_scope
             COMMAND ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_write_scope.sh $<TARGET_FILE:openwitty-cli>)
endif()

# Static guard over every source location that can reach USB hardware.
if(LEWITT_BUILD_TESTS AND UNIX)
    add_test(NAME audit_usb_call_sites
             COMMAND ${CMAKE_SOURCE_DIR}/tools/audit/check_usb_call_sites.sh ${CMAKE_SOURCE_DIR})

    # Pure synthetic FU2 observer policy tests; no BPF load, USB, or audio APIs.
    set_source_files_properties(tools/diagnostics/test_fu2_observer_logic.c
                                PROPERTIES LANGUAGE CXX)
    add_executable(fu2_observer_offline tools/diagnostics/test_fu2_observer_logic.c)
    add_test(NAME fu2_observer_offline COMMAND fu2_observer_offline)
    set_source_files_properties(tools/diagnostics/test_fu2_usb_core_offline.c
                                PROPERTIES LANGUAGE CXX)
    add_executable(fu2_usb_core_offline tools/diagnostics/test_fu2_usb_core_offline.c)
    add_test(NAME fu2_usb_core_offline COMMAND fu2_usb_core_offline)
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
# Branding: the project's own OpenWitty icons (assets/icons), installed unmodified.
install(FILES assets/icons/openwitty.svg DESTINATION ${LEWITT_ICON_DIR}/scalable/apps)
foreach(sz 16 24 32 48 64 128 256 512)
    install(FILES assets/icons/png/${sz}x${sz}/openwitty.png
            DESTINATION ${LEWITT_ICON_DIR}/${sz}x${sz}/apps)
endforeach()
install(FILES packaging/linux/io.github.st0nebyte.openwitty.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/linux/io.github.st0nebyte.openwitty.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES README.md docs/INSTALLATION.md docs/USB_PERMISSIONS.md docs/USB_ACCESS.md
              docs/HARDWARE_TEST_PLAN.md docs/HANDOFF_HARDWARE_TESTING.md docs/THIRD_PARTY.md
              PROTOCOL.md FEATURES.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES packaging/udev/70-openwitty.rules DESTINATION ${CMAKE_INSTALL_DOCDIR}/udev)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME LICENSE)
install(FILES assets/icons/lucide/LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME LICENSE-LUCIDE)

# Debian changelog and man pages are compressed at configure time with a timestamp-free
# gzip (-n) so the output is reproducible.
find_program(LEWITT_GZIP gzip)
if(LEWITT_GZIP)
    set(_gzdir ${CMAKE_BINARY_DIR}/packaging-gz)
    file(MAKE_DIRECTORY ${_gzdir})
    foreach(_f debian/changelog man/openwitty-cli.1 man/openwitty.1)
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
    install(FILES ${_gzdir}/openwitty-cli.1.gz ${_gzdir}/openwitty.1.gz
            DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
    install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink openwitty-cli.1.gz \
        \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_MANDIR}/man1/lewitt-cli.1.gz)")
endif()
if(LEWITT_INSTALL_UDEV_RULES)
    install(FILES packaging/udev/70-openwitty.rules DESTINATION ${LEWITT_UDEV_RULES_DIR})
endif()
include(packaging/CPackConfig.cmake)
