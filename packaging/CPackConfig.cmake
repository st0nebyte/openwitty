# CPack configuration (included at the end of the top-level CMakeLists.txt).
#   cmake --build build && (cd build && cpack -G DEB)        # no root needed
set(CPACK_PACKAGE_NAME "openwitty")
set(CPACK_PACKAGE_VERSION "${LEWITT_VERSION_DEBIAN}")
set(CPACK_PACKAGE_VENDOR "unofficial community project")
set(CPACK_PACKAGE_CONTACT "st0nebyte <noreply@users.noreply.github.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Unofficial, experimental control application for the LEWITT CONNECT 2")
set(CPACK_PACKAGE_DESCRIPTION
    "Independent Linux control application for the LEWITT CONNECT 2 USB audio\ninterface (tested with one device on one system): controls the device's\nsettings, keeps the desktop volume in step with the volume knob (optional\nuser service and tray icon) and includes a simulated device.\nUnofficial; not made by or affiliated with LEWITT.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/st0nebyte/openwitty")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_STRIP_FILES ON)
set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
set(CPACK_SET_DESTDIR OFF)
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_GENERATOR "DEB")
set(CPACK_SOURCE_GENERATOR "TGZ")

# ---- Debian / Ubuntu ----
set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_RELEASE "1")
# Shared-library dependencies are computed from the binaries (dpkg-shlibdeps); the QML modules,
# the Qt platform plugins and the Qt SVG image plugin (Lucide icons) are loaded at run time and must be
# declared by hand. The SVG plugin is in qt6-svg-plugins from Qt 6.8 on (Debian 13); Debian 12 and Ubuntu 24.04
# (Qt 6.4.2) still ship it inside libqt6svg6 - hence the alternative (checked in a Debian 13 container, Phase 9D). Debian/Ubuntu builds should also pass
#   -DLEWITT_SYSTEMD_USER_UNIT_DIR=/usr/lib/systemd/user   (the distribution's user unit directory).
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "qml6-module-qtquick, qml6-module-qtquick-window, qml6-module-qtquick-shapes, qt6-qpa-plugins, qt6-svg-plugins | libqt6svg6 (<< 6.5~)")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "usbutils, alsa-utils, pipewire")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_SOURCE_DIR}/packaging/debian/postinst;${CMAKE_SOURCE_DIR}/packaging/debian/postrm")
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)

# Debian wants the copyright file named "copyright" in the package's doc dir.
install(FILES ${CMAKE_SOURCE_DIR}/packaging/debian/copyright DESTINATION ${CMAKE_INSTALL_DOCDIR})

include(CPack)
