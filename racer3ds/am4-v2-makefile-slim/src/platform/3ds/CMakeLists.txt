# Star Wars Episode I: Racer - Nintendo 3DS game build (rt64-3ds).
#
#   cmake -S platform/3ds -B build-3ds -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=lib/rt64-3ds/cmake/devkitARM-3ds.cmake
#   cmake --build build-3ds
#
# The ROM-derived sources under generated/ are produced on the host first by
# tools/3ds/recompile.sh; nothing ROM-derived is committed or redistributed.
# Racer has no RecompiledPatches and no baked mods, so this build is simpler
# than Banjo's: RecompiledFuncs + the aspMain microcode + the platform layer.
cmake_minimum_required(VERSION 3.20)
project(Racer3DS LANGUAGES C CXX ASM)

set(RACER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(RT64_3DS_DIR "${RACER_ROOT}/lib/rt64-3ds" CACHE PATH "rt64-3ds checkout")
# The fully patched N64ModernRuntime is vendored in-tree (no patches to apply):
# its N64Recomp submodule already carries the 3DS recompiler changes and
# ultramodern/librecomp carry the resolved 3DS runtime series.
set(RT64_3DS_NMR_DIR "${RACER_ROOT}/lib/N64ModernRuntime" CACHE PATH "Patched N64ModernRuntime checkout")
set(RACER_GEN "${CMAKE_CURRENT_LIST_DIR}/generated")

# No RecompiledPatches: leave the runtime's default 16 MB RDRAM model and
# 0x80A00000 heap start, and build without the mod system.
set(RT64_3DS_STATIC_MODS OFF)

# RECOMP_GPR32 makes every N64 register operation one ARM instruction instead
# of a 64-bit pair, but it is only sound when the generated code contains no
# 64-bit register instruction. Racer has not been audited for that yet, so the
# safe 64-bit register model is the default; turn RACER_GPR32 on to try it -
# the configure step fails and names the offending line if the code is ineligible.
option(RACER_GPR32 "32-bit N64 registers (RECOMP_GPR32)" OFF)
if(RACER_GPR32)
    add_compile_definitions(RECOMP_GPR32=1)
endif()

add_subdirectory("${RT64_3DS_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/rt64-3ds")
if(RACER_GPR32)
    rt64_3ds_check_gpr32("${RACER_GEN}/RecompiledFuncs")
endif()

# --- Generated game code (N64Recomp output; C, compiled at -O2) -------------
if(NOT EXISTS "${RACER_GEN}/RecompiledFuncs/funcs.h")
    message(FATAL_ERROR
        "Generated game code not found under ${RACER_GEN}/RecompiledFuncs.\n"
        "Run tools/3ds/recompile.sh with your baserom.z64 first (see BUILDING.md).")
endif()
file(GLOB FUNC_SOURCES CONFIGURE_DEPENDS "${RACER_GEN}/RecompiledFuncs/*.c")
add_library(RecompiledFuncs STATIC ${FUNC_SOURCES} "${RACER_GEN}/RecompiledFuncs/lookup.cpp")
target_include_directories(RecompiledFuncs PRIVATE
    "${RACER_GEN}/RecompiledFuncs"
    "${CMAKE_CURRENT_LIST_DIR}"
    "${RT64_3DS_NMR_DIR}/N64Recomp/include"
    "${RT64_3DS_NMR_DIR}/ultramodern/include"
    "${RT64_3DS_NMR_DIR}/librecomp/include")
target_compile_options(RecompiledFuncs PRIVATE
    -O2 -fno-strict-aliasing -w
    -Wno-implicit-function-declaration -Wno-error=implicit-function-declaration
    -include "${CMAKE_CURRENT_LIST_DIR}/racer_host_protos.h")

# --- The game executable -----------------------------------------------------
add_executable(Racer3DS
    main_3ds.cpp
    recomp_api_3ds.cpp
    "${RACER_GEN}/section_table.cpp"
    "${RACER_GEN}/rsp/aspMain.cpp")
target_include_directories(Racer3DS PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${RACER_GEN}/RecompiledFuncs"
    "${RT64_3DS_NMR_DIR}/N64Recomp/include"
    "${RT64_3DS_NMR_DIR}/ultramodern/include"
    "${RT64_3DS_NMR_DIR}/librecomp/include"
    "${RT64_3DS_NMR_DIR}/thirdparty"
    "${RT64_3DS_NMR_DIR}/thirdparty/miniz")
target_compile_options(Racer3DS PRIVATE -O2 -Wno-unused-parameter)
# The audio microcode is scalar C++ and the hottest file on the ARM11.
set_source_files_properties("${RACER_GEN}/rsp/aspMain.cpp"
    PROPERTIES COMPILE_OPTIONS "-O3;-fno-strict-aliasing")
target_link_libraries(Racer3DS PRIVATE
    recomp_3ds rt64_3ds RecompiledFuncs ultramodern librecomp miniz citro3d ctru m)
target_link_options(Racer3DS PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/Racer3DS.map")

# Bring-up: accept and parse the display lists but draw nothing.
option(RACER_NULL_RENDERER "Build with rt64-3ds's null renderer" OFF)
if(RACER_NULL_RENDERER)
    target_compile_definitions(Racer3DS PRIVATE RACER_NULL_RENDERER)
endif()

# --- .3dsx and the installable .cia -----------------------------------------
set(RACER_TITLE "Star Wars Racer 3DS")
set(RACER_DESCRIPTION "Star Wars Episode I: Racer (N64 recompilation)")
# The .3dsx needs 3dsxtool (+ smdhtool for the icon); toolchains that only
# carry the CIA tools skip it, while the .cia below is the required artifact.
if(TDSXTOOL_EXE)
    add_3dsx(Racer3DS TITLE "${RACER_TITLE}" DESCRIPTION "${RACER_DESCRIPTION}"
             ICON "${CMAKE_CURRENT_LIST_DIR}/icon.png")
else()
    message(STATUS "3dsxtool not found: skipping the .3dsx target (the .cia is still built)")
endif()
include("${RT64_3DS_DIR}/cmake/Cia.cmake")
# Unique IDs 0xF9042/44/46 belong to the ROM-less bring-up CIAs; the game port
# takes the next free one. The library's template.rsf already asks for 64 MB
# SystemMode (Old 3DS) and 124 MB SystemModeExt on a New 3DS.
add_cia(Racer3DS RSF "${RT64_3DS_DIR}/cia/template.rsf"
        TITLE "${RACER_TITLE}" PRODUCT "RACER-3DS" UNIQUEID 0xF9048
        ICON "${CMAKE_CURRENT_LIST_DIR}/icon.png"
        DESCRIPTION "${RACER_DESCRIPTION}")
