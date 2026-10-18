find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick)
qt_standard_project_setup()

# Backend facade as a static library so it can be tested without the QML app.
qt_add_library(lewitt_gui_backend STATIC DeviceController.cpp DeviceController.hpp MeterPresentation.cpp MeterPresentation.hpp MeterDynamics.hpp)
target_include_directories(lewitt_gui_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lewitt_gui_backend PUBLIC lewitt_core Qt6::Core Qt6::Gui)

qt_add_executable(openwitty main.cpp)

set(LEWITT_QML Main Theme Hint GlyphIcon OnboardingOverlay GainKnob ToggleButton ToolTip_ ToggleSwitch LevelMeter Fader Fader_h
               InputStrip OutputStrip MonitorSection SettingsView AutosetupWizard DeviceStatusIndicator)
set(LEWITT_QML_FILES)
foreach(n IN LISTS LEWITT_QML)
    set_source_files_properties(qml/${n}.qml PROPERTIES QT_RESOURCE_ALIAS ${n}.qml)
    list(APPEND LEWITT_QML_FILES qml/${n}.qml)
endforeach()
set_source_files_properties(qml/Theme.qml qml/Hint.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(openwitty
    URI Lewitt
    VERSION 1.0
    NO_CACHEGEN  # Qt 6.4 AOT singleton lookup loops on the Theme singleton
    QML_FILES ${LEWITT_QML_FILES})

# Branding icon for development runs (installed builds use the hicolor theme icons).
set_source_files_properties(${CMAKE_SOURCE_DIR}/assets/icons/png/256x256/openwitty.png PROPERTIES QT_RESOURCE_ALIAS openwitty.png)
qt_add_resources(openwitty "branding" PREFIX "/" FILES ${CMAKE_SOURCE_DIR}/assets/icons/png/256x256/openwitty.png)

target_link_libraries(openwitty PRIVATE lewitt_gui_backend Qt6::Qml Qt6::Quick)
# Distribution packages must not carry an RPATH/RUNPATH (qt_standard_project_setup adds
# $ORIGIN/../lib); the system loader finds Qt and libusb through ld.so.cache.
set_target_properties(openwitty PROPERTIES INSTALL_RPATH "" INSTALL_RPATH_USE_LINK_PATH OFF)
install(TARGETS openwitty RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(LEWITT_BUILD_TESTS)
    add_executable(lewitt_meter_tests ${CMAKE_SOURCE_DIR}/tests/gui/test_meter_dynamics.cpp)
    target_link_libraries(lewitt_meter_tests PRIVATE lewitt_gui_backend GTest::gtest_main)
    gtest_discover_tests(lewitt_meter_tests)
    add_executable(lewitt_gui_tests ${CMAKE_SOURCE_DIR}/tests/gui/test_gui.cpp)
    target_link_libraries(lewitt_gui_tests PRIVATE lewitt_gui_backend Qt6::Qml Qt6::Quick GTest::gtest_main)
    target_compile_definitions(lewitt_gui_tests PRIVATE LEWITT_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
    # Qt needs a platform plugin; tests always run offscreen.
    add_test(NAME gui_integration COMMAND lewitt_gui_tests)
    set_tests_properties(gui_integration PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QML_IMPORT_PATH=$ENV{QML_IMPORT_PATH};LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tests/lsan.supp")
    add_test(NAME gui_rejects_48hz_mock COMMAND openwitty --mock --experimental-status-48hz)
    add_test(NAME gui_rejects_conflicting_status_rates
             COMMAND openwitty --experimental-status-8hz --experimental-status-48hz)
    set_tests_properties(gui_rejects_48hz_mock gui_rejects_conflicting_status_rates PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" WILL_FAIL TRUE TIMEOUT 2)
endif()

add_executable(lewitt_bench_gui EXCLUDE_FROM_ALL ${CMAKE_SOURCE_DIR}/tests/bench/bench_gui.cpp)
target_link_libraries(lewitt_bench_gui PRIVATE lewitt_gui_backend Qt6::Gui)
