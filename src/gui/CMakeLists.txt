find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick)
qt_standard_project_setup()

# Backend facade as a static library so it can be tested without the QML app.
qt_add_library(lewitt_gui_backend STATIC DeviceController.cpp DeviceController.hpp)
target_include_directories(lewitt_gui_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lewitt_gui_backend PUBLIC lewitt_core Qt6::Core Qt6::Gui)

qt_add_executable(lewitt-control-center main.cpp)

set(LEWITT_QML Main Theme Hint GlyphIcon OnboardingOverlay GainKnob ToggleButton ToolTip_ ToggleSwitch LevelMeter Fader Fader_h
               InputStrip OutputStrip MonitorSection SettingsView AutosetupWizard DeviceStatusIndicator)
set(LEWITT_QML_FILES)
foreach(n IN LISTS LEWITT_QML)
    set_source_files_properties(qml/${n}.qml PROPERTIES QT_RESOURCE_ALIAS ${n}.qml)
    list(APPEND LEWITT_QML_FILES qml/${n}.qml)
endforeach()
set_source_files_properties(qml/Theme.qml qml/Hint.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(lewitt-control-center
    URI Lewitt
    VERSION 1.0
    NO_CACHEGEN  # Qt 6.4 AOT singleton lookup loops on the Theme singleton
    QML_FILES ${LEWITT_QML_FILES})

target_link_libraries(lewitt-control-center PRIVATE lewitt_gui_backend Qt6::Qml Qt6::Quick)

if(LEWITT_BUILD_TESTS)
    add_executable(lewitt_gui_tests ${CMAKE_SOURCE_DIR}/tests/gui/test_gui.cpp)
    target_link_libraries(lewitt_gui_tests PRIVATE lewitt_gui_backend Qt6::Qml Qt6::Quick GTest::gtest_main)
    target_compile_definitions(lewitt_gui_tests PRIVATE LEWITT_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
    # Qt needs a platform plugin; tests always run offscreen.
    add_test(NAME gui_integration COMMAND lewitt_gui_tests)
    set_tests_properties(gui_integration PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QML_IMPORT_PATH=$ENV{QML_IMPORT_PATH};LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tests/lsan.supp")
endif()

add_executable(lewitt_bench_gui EXCLUDE_FROM_ALL ${CMAKE_SOURCE_DIR}/tests/bench/bench_gui.cpp)
target_link_libraries(lewitt_bench_gui PRIVATE lewitt_gui_backend Qt6::Gui)
