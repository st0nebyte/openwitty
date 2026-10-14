# CPack configuration (included at the end of the top-level CMakeLists.txt).
#   cmake --build build && (cd build && cpack -G DEB)        # no root needed
set(CPACK_PACKAGE_NAME "lewitt-connect2-unofficial")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "unofficial community project")
set(CPACK_PACKAGE_CONTACT "LEWITT CONNECT 2 Linux project <noreply@example.invalid>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Unofficial, experimental control application for the LEWITT CONNECT 2")
set(CPACK_PACKAGE_DESCRIPTION
    "Independent Linux control application and diagnostic tool for the\nLEWITT CONNECT 2 USB audio interface.\nNot affiliated with LEWITT. The control protocol was derived by static\nanalysis and has not been validated on real hardware; hardware writes are\ndisabled and a simulated device is included.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://example.invalid/lewitt-connect2-linux")
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
# Shared-library dependencies are computed from the binaries (dpkg-shlibdeps); the QML modules
# and the Qt platform plugins are loaded at run time and must be declared by hand.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "qml6-module-qtquick, qml6-module-qtquick-window, qt6-qpa-plugins")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "usbutils, alsa-utils")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_SOURCE_DIR}/packaging/debian/postinst;${CMAKE_SOURCE_DIR}/packaging/debian/postrm")
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)

# Debian wants the copyright file named "copyright" in the package's doc dir.
install(FILES ${CMAKE_SOURCE_DIR}/packaging/debian/copyright DESTINATION ${CMAKE_INSTALL_DOCDIR})

include(CPack)
