# Install-time generation of the openwitty-volumed systemd user unit.
#
# Runs inside `cmake --install`, so CMAKE_INSTALL_PREFIX is the EFFECTIVE prefix (including
# `cmake --install --prefix ...`), and file(INSTALL) applies DESTDIR and records the file in
# install_manifest.txt. ExecStart therefore always names the executable that is actually installed,
# never a DESTDIR staging path. Inputs (set by CMakeLists.txt via install(CODE)):
#   OW_UNIT_TEMPLATE  OW_BINDIR  OW_DOCDIR  OW_UNITDIR  OW_GENERATED_DIR   (dirs: relative or absolute)
foreach(_v OW_BINDIR OW_DOCDIR OW_UNITDIR)
    if(IS_ABSOLUTE "${${_v}}")
        set(_${_v} "${${_v}}")
    else()
        set(_${_v} "${CMAKE_INSTALL_PREFIX}/${${_v}}")
    endif()
endforeach()
file(READ "${OW_UNIT_TEMPLATE}" _unit)
string(REPLACE "@CMAKE_INSTALL_FULL_BINDIR@" "${_OW_BINDIR}" _unit "${_unit}")
string(REPLACE "@CMAKE_INSTALL_FULL_DOCDIR@" "${_OW_DOCDIR}" _unit "${_unit}")
file(WRITE "${OW_GENERATED_DIR}/openwitty-volumed.service" "${_unit}")
file(INSTALL DESTINATION "${_OW_UNITDIR}" TYPE FILE PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
     FILES "${OW_GENERATED_DIR}/openwitty-volumed.service")
