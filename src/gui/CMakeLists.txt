find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick)
qt_standard_project_setup()

qt_add_executable(lewitt-control-center
    main.cpp
    DeviceController.cpp
    DeviceController.hpp)

set(LEWITT_QML Main Theme GainKnob ToggleSwitch FeatureButton LevelMeter
               InputChannel OutputSection DeviceStatusIndicator)
set(LEWITT_QML_FILES)
foreach(n IN LISTS LEWITT_QML)
    set_source_files_properties(qml/${n}.qml PROPERTIES QT_RESOURCE_ALIAS ${n}.qml)
    list(APPEND LEWITT_QML_FILES qml/${n}.qml)
endforeach()
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(lewitt-control-center
    URI Lewitt
    VERSION 1.0
    NO_CACHEGEN  # Qt 6.4 AOT singleton lookup loops on the Theme singleton
    QML_FILES ${LEWITT_QML_FILES})

target_link_libraries(lewitt-control-center PRIVATE lewitt_core Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
