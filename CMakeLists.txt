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
# Release version: CMake's numeric project version plus a pre-release label, kept in this one place.
#   LEWITT_VERSION          0.1.0-alpha.1   (--version, man pages, git tag v0.1.0-alpha.1)
#   LEWITT_VERSION_DEBIAN   0.1.0~alpha1    (dpkg: "~" sorts before 0.1.0)
#   LEWITT_VERSION_ARCH     0.1.0alpha1     (pacman pkgver: no hyphen; vercmp sorts it before 0.1.0)
set(LEWITT_VERSION_SUFFIX "alpha.1")
if(LEWITT_VERSION_SUFFIX)
    set(LEWITT_VERSION "${PROJECT_VERSION}-${LEWITT_VERSION_SUFFIX}")
    string(REPLACE "." "" _ow_suffix_compact "${LEWITT_VERSION_SUFFIX}")
    set(LEWITT_VERSION_DEBIAN "${PROJECT_VERSION}~${_ow_suffix_compact}")
    set(LEWITT_VERSION_ARCH "${PROJECT_VERSION}${_ow_suffix_compact}")
else()
    set(LEWITT_VERSION "${PROJECT_VERSION}")
    set(LEWITT_VERSION_DEBIAN "${PROJECT_VERSION}")
    set(LEWITT_VERSION_ARCH "${PROJECT_VERSION}")
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
    src/transport/DeviceService.cpp
    src/protocol/WriteGuard.cpp
    src/core/DeviceSession.cpp
    src/core/WriteVerification.cpp
    src/core/DeviceManager.cpp
    src/core/OutputControl.cpp
    src/core/VolumeSync.cpp
    src/core/OutputFollow.cpp
    src/core/ControlCapabilities.cpp
    src/core/SettingsComparison.cpp)
target_include_directories(lewitt_core PUBLIC src ${CMAKE_BINARY_DIR}/generated)
# The user settings file on its own (no USB, no ALSA): shared by the core and the tray companion.
add_library(lewitt_config STATIC src/core/UserConfig.cpp)
target_include_directories(lewitt_config PUBLIC src ${CMAKE_BINARY_DIR}/generated)
target_compile_options(lewitt_config PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(lewitt_core PUBLIC lewitt_config)
target_compile_options(lewitt_core PRIVATE -Wall -Wextra -Wpedantic)
if(LEWITT_ENABLE_USB)
    target_link_libraries(lewitt_core PUBLIC PkgConfig::LIBUSB)
    target_compile_definitions(lewitt_core PUBLIC LEWITT_HAVE_USB=1)
endif()

# Desktop (ALSA) volume integration: policy glue is always built and tested; the libasound backend
# and the openwitty-volumed daemon need alsa-lib and the USB transport.
add_library(lewitt_desktop STATIC src/desktop/DesktopMixer.cpp src/desktop/DesktopVolumeLink.cpp
            src/desktop/AudioSetupCheck.cpp src/desktop/RouteVolume.cpp src/desktop/DeviceSampleRate.cpp
            src/desktop/SplitSources.cpp src/desktop/SampleRates.cpp src/desktop/PortNames.cpp)
target_link_libraries(lewitt_desktop PUBLIC lewitt_core)
target_compile_options(lewitt_desktop PRIVATE -Wall -Wextra -Wpedantic)
option(LEWITT_ENABLE_ALSA "Build the ALSA desktop-volume backend and openwitty-volumed" ON)
if(LEWITT_ENABLE_ALSA AND LEWITT_ENABLE_USB)
    pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
    target_sources(lewitt_desktop PRIVATE src/desktop/AlsaDesktopMixer.cpp)
    target_link_libraries(lewitt_desktop PUBLIC PkgConfig::ALSA)
    target_compile_definitions(lewitt_desktop PUBLIC LEWITT_HAVE_ALSA=1)
    add_executable(openwitty-volumed src/daemon/main.cpp)
    target_link_libraries(openwitty-volumed PRIVATE lewitt_desktop)
    target_compile_options(openwitty-volumed PRIVATE -Wall -Wextra -Wpedantic)
    install(TARGETS openwitty-volumed RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    # systemd user unit: installed, never enabled by the build or install step. The default
    # <prefix>/share/systemd/user is on systemd's user unit search path for ~/.local (as
    # $XDG_DATA_HOME/systemd/user), /usr/local and /usr; distribution packages may point this at
    # pkg-config's systemduserunitdir instead. Generated at install time (see InstallUserUnit.cmake).
    set(LEWITT_SYSTEMD_USER_UNIT_DIR "${CMAKE_INSTALL_DATADIR}/systemd/user" CACHE STRING
        "systemd user unit destination (relative to the install prefix, or absolute)")
    # Build trees configured before this change cached the old default, which is not on the user
    # search path for ~/.local: migrate exactly that value (an explicit packager choice is kept).
    if(LEWITT_SYSTEMD_USER_UNIT_DIR STREQUAL "lib/systemd/user")
        set(LEWITT_SYSTEMD_USER_UNIT_DIR "${CMAKE_INSTALL_DATADIR}/systemd/user" CACHE STRING
            "systemd user unit destination (relative to the install prefix, or absolute)" FORCE)
    endif()
    install(CODE "set(OW_UNIT_TEMPLATE \"${CMAKE_SOURCE_DIR}/packaging/systemd/openwitty-volumed.service.in\")
                  set(OW_BINDIR \"${CMAKE_INSTALL_BINDIR}\")
                  set(OW_DOCDIR \"${CMAKE_INSTALL_DOCDIR}\")
                  set(OW_UNITDIR \"${LEWITT_SYSTEMD_USER_UNIT_DIR}\")
                  set(OW_GENERATED_DIR \"${CMAKE_BINARY_DIR}/packaging/install\")")
    install(SCRIPT packaging/systemd/InstallUserUnit.cmake)
    install(FILES packaging/modprobe/openwitty-connect2.conf
                  packaging/wireplumber/51-openwitty-connect2-login-screen.conf
                  packaging/wireplumber/52-openwitty-connect2-input.conf
            DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples)
    # Same names as the targets in ~/.config/alsa-card-profile/mixer/ (own subfolders: the modprobe example is also
    # called openwitty-connect2.conf).
    install(FILES packaging/alsa-card-profile/profile-sets/openwitty-connect2.conf
            DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples/alsa-card-profile/profile-sets)
    install(FILES packaging/alsa-card-profile/paths/openwitty-connect2-input.conf
            DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples/alsa-card-profile/paths)
    # Login-screen WirePlumber rule (example only): valid SPA-JSON, matches the CONNECT 2's card name pattern.
    find_program(LEWITT_SPA_JSON_DUMP spa-json-dump)
    if(LEWITT_BUILD_TESTS AND LEWITT_SPA_JSON_DUMP)
        add_test(NAME wireplumber_login_screen_rule_parses
                 COMMAND sh -c "out=\$(\"$1\" \"$2\") || exit 1; printf '%s' \"\$out\" | grep -q '\"device.disabled\": true' || exit 2; \
                                pat=\$(printf '%s' \"\$out\" | sed -n 's/.*\"device.name\": \"~\\(.*\\)\".*/\\1/p'); \
                                for n in alsa_card.usb-Lewitt_GmbH_CONNECT_2_X000XXX00000-00 alsa_card.usb-Lewitt_GmbH_CONNECT_2-00; do \
                                    printf '%s' \"\$n\" | grep -Eq \"\$pat\" || exit 3; done; \
                                for n in alsa_card.usb-Other_Vendor_Card-00 alsa_card.usb-Lewitt_GmbH_CONNECT_6_X0-00 alsa_card.usb-Lewitt_GmbH_CONNECT_20-00 alsa_card.pci-0000_0c_00.4; do \
                                    printf '%s' \"\$n\" | grep -Eq \"\$pat\" && exit 4; done; echo RULE OK"
                 sh ${LEWITT_SPA_JSON_DUMP} ${CMAKE_SOURCE_DIR}/packaging/wireplumber/51-openwitty-connect2-login-screen.conf)
        set_tests_properties(wireplumber_login_screen_rule_parses PROPERTIES PASS_REGULAR_EXPRESSION "RULE OK")
        # Input rule: valid SPA-JSON, matches the CONNECT 2 by USB id only, selects OpenWitty's profile set.
        add_test(NAME wireplumber_input_rule_parses
                 COMMAND sh -c "out=\$(\"$1\" \"$2\") || exit 1; \
                                printf '%s' \"\$out\" | grep -q '\"device.vendor.id\": \"0x29c2\"' || exit 2; \
                                printf '%s' \"\$out\" | grep -q '\"device.product.id\": \"0x0004\"' || exit 3; \
                                printf '%s' \"\$out\" | grep -q '\"device.profile-set\": \"openwitty-connect2.conf\"' || exit 4; \
                                printf '%s' \"\$out\" | grep -q '\"node.description\": \"CONNECT 2 (1 Mic' || exit 5; echo RULE OK"
                 sh ${LEWITT_SPA_JSON_DUMP} ${CMAKE_SOURCE_DIR}/packaging/wireplumber/52-openwitty-connect2-input.conf)
        set_tests_properties(wireplumber_input_rule_parses PROPERTIES PASS_REGULAR_EXPRESSION "RULE OK")
    endif()
    # The generated user unit must verify cleanly (keys in the right sections, valid ExecStart).
    find_program(LEWITT_SYSTEMD_ANALYZE systemd-analyze)
    if(LEWITT_BUILD_TESTS AND LEWITT_SYSTEMD_ANALYZE)
        add_test(NAME systemd_user_unit_verifies
                 COMMAND sh -c "d=\$(mktemp -d) && sed 's#@CMAKE_INSTALL_FULL_BINDIR@/openwitty-volumed#'\"$1\"'#' \"$2\" > \"\$d/openwitty-volumed.service\" && \"$3\" --user verify \"\$d/openwitty-volumed.service\"; r=\$?; rm -rf \"\$d\"; exit \$r"
                         sh $<TARGET_FILE:openwitty-volumed> ${CMAKE_BINARY_DIR}/packaging/openwitty-volumed.service.verify ${LEWITT_SYSTEMD_ANALYZE})
        set_tests_properties(systemd_user_unit_verifies PROPERTIES FAIL_REGULAR_EXPRESSION "Unknown key|ignoring|Failed|not found|Invalid")
        file(READ packaging/systemd/openwitty-volumed.service.in _ow_unit)
        file(WRITE ${CMAKE_BINARY_DIR}/packaging/openwitty-volumed.service.verify "${_ow_unit}")
    endif()
    # Disposable staging installs: user-local prefix override and a DESTDIR package build.
    if(LEWITT_BUILD_TESTS)
        add_test(NAME install_staging_user_unit
                 COMMAND sh ${CMAKE_SOURCE_DIR}/tools/packaging/check_install_staging.sh ${CMAKE_BINARY_DIR} ${CMAKE_COMMAND})
        set_tests_properties(install_staging_user_unit PROPERTIES PASS_REGULAR_EXPRESSION "INSTALL STAGING: PASS")
    endif()
endif()

add_executable(openwitty-cli src/cli/main.cpp)
if(LEWITT_BUILD_TESTS)
    add_test(NAME version_consistency
             COMMAND sh ${CMAKE_SOURCE_DIR}/tools/packaging/check_version_consistency.sh ${CMAKE_SOURCE_DIR}
                     ${LEWITT_VERSION} ${LEWITT_VERSION_DEBIAN} ${LEWITT_VERSION_ARCH} $<TARGET_FILE:openwitty-cli>)
    set_tests_properties(version_consistency PROPERTIES PASS_REGULAR_EXPRESSION "VERSION CONSISTENCY: PASS")
endif()
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
# Recorded in install_manifest.txt so an uninstall from the manifest does not leave a dangling link.
install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink openwitty-cli \
    \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/lewitt-cli)
    list(APPEND CMAKE_INSTALL_MANIFEST_FILES \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/lewitt-cli\")")

if(LEWITT_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_executable(lewitt_tests tests/cpp/test_protocol.cpp tests/cpp/test_session.cpp tests/cpp/test_outputs.cpp tests/cpp/test_recovered.cpp tests/cpp/test_hardening.cpp tests/cpp/test_write_verification.cpp)
    target_sources(lewitt_tests PRIVATE tests/cpp/test_hardware_scope.cpp)   # Phase 6E
    target_sources(lewitt_tests PRIVATE tests/cpp/test_interface_selection.cpp tests/cpp/test_retry_policy.cpp)   # Phase 7A
    target_sources(lewitt_tests PRIVATE tests/cpp/test_replay_transport.cpp)   # Phase 7B read-only fixtures
    target_sources(lewitt_tests PRIVATE tests/cpp/test_control_lease.cpp)       # Phase 7D process ownership
    target_sources(lewitt_tests PRIVATE tests/cpp/test_read_freshness.cpp)     # Phase 7E independent read ages
    target_sources(lewitt_tests PRIVATE tests/cpp/test_output_control.cpp tests/cpp/test_volume_sync.cpp
                                        tests/cpp/test_desktop_link.cpp        # Phase 8D volume integration
                                        tests/cpp/test_output_follow.cpp       # Phase 12C follow active output
                                        tests/cpp/test_route_volume.cpp        # Phase 9C route-volume proof of concept
                                        tests/cpp/test_trigger_policy.cpp      # Phase 10A trigger authorization
                                        tests/cpp/test_control_capabilities.cpp # Phase 10A per-control capabilities
                                        tests/cpp/test_device_sample_rate.cpp  # Phase 10B real sample rate
                                        tests/cpp/test_settings_comparison.cpp  # Phase 10B.1 hw-check, SET bookkeeping
                                        tests/cpp/test_permissions_11a.cpp  # Phase 11A grouped permissions
                                        tests/cpp/test_audio_setup_check.cpp   # Phase 8F setup diagnosis
                                        tests/cpp/test_split_sources.cpp       # Phase 11A.4 separate inputs
                                        tests/cpp/test_sample_rates.cpp        # SR: allowed rates for apps
                                        tests/cpp/test_port_names.cpp          # readable channel names
                                        tests/cpp/test_device_service.cpp      # single device owner
                                        tests/cpp/test_user_config.cpp)        # Phase 8G user configuration
    target_link_libraries(lewitt_tests PRIVATE lewitt_core lewitt_desktop GTest::gtest_main)
    if(LEWITT_ENABLE_USB)
        # Phase 8D CLI refusals. Every case is rejected before a transport exists (argument parsing or
        # the authorization check), so none of them can reach a device. Never add a case here that
        # sets every required variable to exactly 1 without --mock.
        set(_ow_cli $<TARGET_FILE:openwitty-cli>)
        set(_ow_unset --unset=LEWITT_ALLOW_HARDWARE_WRITE --unset=LEWITT_ALLOW_WRITE_SPEAKER_VOLUME LEWITT_NO_DEVICE_SERVICE=1)
        add_test(NAME cli_rejects_nan_volume COMMAND ${CMAKE_COMMAND} -E env ${_ow_unset} ${_ow_cli} write speaker-volume nan)
        add_test(NAME cli_rejects_inf_volume COMMAND ${CMAKE_COMMAND} -E env ${_ow_unset} ${_ow_cli} write speaker-volume -inf --execute)
        add_test(NAME cli_rejects_out_of_range_volume COMMAND ${CMAKE_COMMAND} -E env ${_ow_unset} ${_ow_cli} write speaker-volume 0.5 --execute)
        add_test(NAME cli_write_env_must_be_exactly_one COMMAND ${CMAKE_COMMAND} -E env ${_ow_unset}
                 LEWITT_ALLOW_HARDWARE_WRITE=true ${_ow_cli} write speaker-volume -40 --execute)
        add_test(NAME cli_write_env_trailing_space COMMAND ${CMAKE_COMMAND} -E env ${_ow_unset}
                 "LEWITT_ALLOW_HARDWARE_WRITE=1 " ${_ow_cli} write speaker-volume -40 --execute)
        add_test(NAME cli_field_env_must_be_exactly_one COMMAND ${CMAKE_COMMAND} -E env ${_ow_unset}
                 LEWITT_ALLOW_HARDWARE_WRITE=1 LEWITT_ALLOW_WRITE_SPEAKER_VOLUME=yes ${_ow_cli} write speaker-volume -40 --execute)
        add_test(NAME cli_speaker_mute_needs_field_env COMMAND ${CMAKE_COMMAND} -E env ${_ow_unset}
                 LEWITT_ALLOW_HARDWARE_WRITE=1 ${_ow_cli} write speaker-mute on --execute)
        set_tests_properties(cli_rejects_nan_volume cli_rejects_inf_volume PROPERTIES
                             PASS_REGULAR_EXPRESSION "takes a finite dB value")
        set_tests_properties(cli_rejects_out_of_range_volume PROPERTIES PASS_REGULAR_EXPRESSION "value out of range")
        set_tests_properties(cli_write_env_must_be_exactly_one cli_write_env_trailing_space PROPERTIES
                             PASS_REGULAR_EXPRESSION "refused: --execute against real hardware needs LEWITT_ALLOW_HARDWARE_WRITE=1")
        set_tests_properties(cli_field_env_must_be_exactly_one cli_speaker_mute_needs_field_env PROPERTIES
                             PASS_REGULAR_EXPRESSION "additionally needs LEWITT_ALLOW_WRITE_SPEAKER_VOLUME=1")
        if(TARGET openwitty-volumed)
            add_test(NAME volumed_writes_need_exact_env COMMAND ${CMAKE_COMMAND} -E env ${_ow_unset}
                     LEWITT_ALLOW_HARDWARE_WRITE=1 LEWITT_ALLOW_WRITE_SPEAKER_VOLUME=true
                     $<TARGET_FILE:openwitty-volumed> --vendor-fallback)
            add_test(NAME volumed_rejects_bad_arguments COMMAND $<TARGET_FILE:openwitty-volumed> --max-increase-db nan)
            set_tests_properties(volumed_writes_need_exact_env PROPERTIES PASS_REGULAR_EXPRESSION "refused: --vendor-fallback")
            set_tests_properties(volumed_rejects_bad_arguments PROPERTIES PASS_REGULAR_EXPRESSION "usage: openwitty-volumed")
            # exactly one mode; conflicting or misplaced flags are rejected before anything else happens,
            # even with valid write authorization in the environment (no stale or last-flag-wins writes)
            set(_ow_vd $<TARGET_FILE:openwitty-volumed>)
            set(_ow_auth LEWITT_ALLOW_HARDWARE_WRITE=1 LEWITT_ALLOW_WRITE_SPEAKER_VOLUME=1)
            add_test(NAME volumed_rejects_fallback_plus_dry_run COMMAND ${CMAKE_COMMAND} -E env ${_ow_auth} ${_ow_vd} --vendor-fallback --dry-run)
            add_test(NAME volumed_rejects_dry_run_plus_fallback COMMAND ${CMAKE_COMMAND} -E env ${_ow_auth} ${_ow_vd} --dry-run --vendor-fallback)
            add_test(NAME volumed_rejects_check_plus_fallback COMMAND ${CMAKE_COMMAND} -E env ${_ow_auth} ${_ow_vd} --check --vendor-fallback)
            add_test(NAME volumed_rejects_repeated_mode COMMAND ${_ow_vd} --dry-run --dry-run)
            add_test(NAME volumed_rejects_increase_without_fallback COMMAND ${_ow_vd} --max-increase-db 5)
            add_test(NAME volumed_rejects_options_with_check COMMAND ${_ow_vd} --check --poll-ms 500)
            set_tests_properties(volumed_rejects_fallback_plus_dry_run volumed_rejects_dry_run_plus_fallback
                                 volumed_rejects_check_plus_fallback volumed_rejects_repeated_mode
                                 volumed_rejects_increase_without_fallback volumed_rejects_options_with_check
                                 PROPERTIES PASS_REGULAR_EXPRESSION "Exactly one mode flag at most")
        endif()
    endif()
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
    # Lifecycle/cleanup state machine with fake links and local processes; no BPF load.
    set_source_files_properties(tools/diagnostics/test_fu2_boot_lifecycle.c
                                PROPERTIES LANGUAGE CXX)
    add_executable(fu2_boot_lifecycle_offline tools/diagnostics/test_fu2_boot_lifecycle.c)
    add_test(NAME fu2_boot_lifecycle_offline COMMAND fu2_boot_lifecycle_offline)
    # Boot-time USB EP0 trace decoder/lifecycle (synthetic traces and fake tracefs only).
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME usb_ep0_trace_offline
                 COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/diagnostics/test_usb_ep0_trace.py)
        add_test(NAME usb_ep0_live_offline
                 COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/diagnostics/test_usb_ep0_live.py)
        add_test(NAME usb_ep0_ab_offline
                 COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/diagnostics/test_usb_ep0_ab.py)
        add_test(NAME post_boot_health_offline
                 COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/diagnostics/test_post_boot_health.py)
        add_test(NAME deferred_probe_offline
                 COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/diagnostics/test_deferred_probe.py)
        add_test(NAME loaded_reconnect_offline
                 COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/diagnostics/test_loaded_reconnect.py)
        add_test(NAME fu2_recovery_probe_offline
                 COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/diagnostics/test_fu2_recovery_probe.py)
        add_test(NAME volume_bridge_offline
                 COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/volume_bridge/test_openwitty_volume_bridge.py)
    endif()
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
# User-facing documentation only (developer and hardware-test notes stay in the source tree).
install(FILES README.md docs/INSTALLATION.md docs/USB_PERMISSIONS.md docs/USB_ACCESS.md
              docs/THIRD_PARTY.md docs/VOLUME_INTEGRATION.md
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
    # ../CHANGELOG.md is the upstream changelog: installed as changelog.gz (the Debian/FHS doc name).
    foreach(_f debian/changelog ../CHANGELOG.md man/openwitty-cli.1 man/openwitty.1 man/openwitty-volumed.1 man/openwitty-tray.1)
        get_filename_component(_n ${_f} NAME)
        if(_n STREQUAL "CHANGELOG.md")
            set(_out ${_gzdir}/changelog.gz)
        elseif(_n STREQUAL "changelog")
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
    install(FILES ${_gzdir}/changelog.Debian.gz ${_gzdir}/changelog.gz DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(FILES ${_gzdir}/openwitty-cli.1.gz ${_gzdir}/openwitty.1.gz
            DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
    if(TARGET openwitty-volumed)
        install(FILES ${_gzdir}/openwitty-volumed.1.gz DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
    endif()
    if(LEWITT_BUILD_GUI)
        install(FILES ${_gzdir}/openwitty-tray.1.gz DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
    endif()
    install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink openwitty-cli.1.gz \
        \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_MANDIR}/man1/lewitt-cli.1.gz)
        list(APPEND CMAKE_INSTALL_MANIFEST_FILES \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_MANDIR}/man1/lewitt-cli.1.gz\")")
endif()
if(LEWITT_INSTALL_UDEV_RULES)
    install(FILES packaging/udev/70-openwitty.rules DESTINATION ${LEWITT_UDEV_RULES_DIR})
endif()
include(packaging/CPackConfig.cmake)
