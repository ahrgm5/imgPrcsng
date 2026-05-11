cmake_minimum_required(VERSION 3.16)
project(imgPrcsngC VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── C / C++ standards ────────────────────────────────────────────────────────
set(CMAKE_C_STANDARD          11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS        OFF)

set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(_POSIX_C_SOURCE=200809L)

# ── Build type ────────────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
message(STATUS "Build type  : ${CMAKE_BUILD_TYPE}")

# ── Find dependencies ─────────────────────────────────────────────────────────

# fftw3 — manual find so we get a useful error message on missing installs
find_library(FFTW_LIB NAMES fftw3
    HINTS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu)
if(NOT FFTW_LIB)
    message(FATAL_ERROR "fftw3 not found. Run: sudo apt-get install libfftw3-dev")
endif()

find_path(FFTW_INCLUDE_DIR NAMES fftw3.h
    HINTS /usr/include /usr/local/include)
if(NOT FFTW_INCLUDE_DIR)
    message(FATAL_ERROR "fftw3.h not found. Run: sudo apt-get install libfftw3-dev")
endif()

message(STATUS "fftw3 lib   : ${FFTW_LIB}")
message(STATUS "fftw3 inc   : ${FFTW_INCLUDE_DIR}")

# OpenCV — for the neural-network / aberration engine (C++ only)
find_package(OpenCV REQUIRED COMPONENTS core dnn imgproc)
message(STATUS "OpenCV ver  : ${OpenCV_VERSION}")
message(STATUS "OpenCV inc  : ${OpenCV_INCLUDE_DIRS}")

# ── PIPX_PYTHON ───────────────────────────────────────────────────────────────
set(PIPX_PYTHON "" CACHE STRING
    "Absolute path to the pipx jupyterlab venv Python interpreter")

if(PIPX_PYTHON STREQUAL "")
    find_program(_sys_python NAMES python3)
    if(NOT _sys_python)
        message(FATAL_ERROR "python3 not found and PIPX_PYTHON not set.")
    endif()
    set(PIPX_PYTHON "${_sys_python}" CACHE STRING
        "Absolute path to the pipx jupyterlab venv Python interpreter" FORCE)
    message(WARNING "PIPX_PYTHON not set - using system python3 (${PIPX_PYTHON})")
endif()
message(STATUS "PIPX_PYTHON : ${PIPX_PYTHON}")

# ── Source file lists ─────────────────────────────────────────────────────────

# Core C sources (unchanged from original project)
set(C_SOURCES
    src/bmp.c
    src/transforms.c
    src/imgproc.c
)

# New C source: ISP / aberration pipeline
set(ABERRATION_C_SOURCES
    src/aberration.c
)

# New C++ source: neural-network engine (OpenCV dnn)
set(ABERRATION_CXX_SOURCES
    src/aberration_net.cpp
)

# ── Static library: imgproc_core  (pure C) ────────────────────────────────────
#
# Contains all original image-processing code plus the aberration C pipeline.
# Downstream C++ targets pull this in through aberration_nn.
add_library(imgproc_core STATIC
    ${C_SOURCES}
    ${ABERRATION_C_SOURCES}
)

set_target_properties(imgproc_core PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

target_compile_definitions(imgproc_core PRIVATE _POSIX_C_SOURCE=200809L)

target_include_directories(imgproc_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${FFTW_INCLUDE_DIR}
)

target_link_libraries(imgproc_core PUBLIC
    ${FFTW_LIB}
    m
)

target_compile_options(imgproc_core PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-unused-parameter>
    $<$<CONFIG:Debug>:   -g3 -O0>
    $<$<CONFIG:Release>: -O3 -DNDEBUG>
)

# ── Static library: aberration_nn  (C++) ─────────────────────────────────────
#
# Kept separate so the C / C++ split stays clean.
# Pulls in imgproc_core + OpenCV so callers only need to link aberration_nn.
add_library(aberration_nn STATIC
    ${ABERRATION_CXX_SOURCES}
)

target_include_directories(aberration_nn PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${FFTW_INCLUDE_DIR}
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(aberration_nn PUBLIC
    imgproc_core
    ${OpenCV_LIBS}
)

target_compile_options(aberration_nn PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-unused-parameter>
    $<$<CONFIG:Debug>:   -g3 -O0>
    $<$<CONFIG:Release>: -O3 -DNDEBUG>
)

# ── Main executable ───────────────────────────────────────────────────────────
#
# main.c is C, but it transitively pulls in C++ (OpenCV) through aberration_nn.
# Setting LINKER_LANGUAGE to CXX tells CMake to use the C++ linker driver so
# the C++ standard library symbols (from OpenCV) are resolved correctly.
add_executable(imgPrcsng main.c)

set_target_properties(imgPrcsng PROPERTIES
    C_STANDARD          11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS        OFF
    LINKER_LANGUAGE     CXX   # required when a C main links C++ archives
)

target_link_libraries(imgPrcsng PRIVATE
    aberration_nn   # transitively brings in imgproc_core + OpenCV + fftw3 + m
)

# ── Jupyter kernel ────────────────────────────────────────────────────────────
#
# cmake does NOT use configure_file here. configure_file writes into
# build/kernel/ during every configure run, which updates that directory's
# mtime, which makes ninja think build.ninja is dirty, causing an infinite
# reconfigure loop ("manifest still dirty after 100 tries").
#
# Path substitution is handled entirely by the shell scripts (sed).
# cmake's only job here is to copy the already-generated stub after a build.
#
# File layout (shell scripts own this, not cmake):
#
#   imgPrcsngC/kernel/       .in templates  (source tree, read by sed)
#   build/kernel/            generated files (written by sed in shell scripts)
#   ~/.local/.../c_imgproc/  stub            (copied by POST_BUILD below)

set(KERNEL_DIR  "${CMAKE_CURRENT_BINARY_DIR}/kernel")
set(KERNEL_STUB "$ENV{HOME}/.local/share/jupyter/kernels/c_imgproc")

# POST_BUILD: copy the stub kernel.json into the Jupyter kernels directory
# after every successful build of imgproc_core.
# If the file does not exist yet the copy is skipped gracefully.
add_custom_command(TARGET imgproc_core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${KERNEL_STUB}"
    COMMAND ${CMAKE_COMMAND}
        -DKERNEL_JSON="${KERNEL_DIR}/kernel.json"
        -DKERNEL_STUB_JSON="${KERNEL_STUB}/kernel.json"
        -P "${CMAKE_CURRENT_BINARY_DIR}/copy_stub.cmake"
    COMMENT "Refreshing Jupyter stub: ${KERNEL_STUB}/kernel.json"
    VERBATIM
)

# Write the copy helper into the build dir at configure time.
# No source-tree cmake/ directory is needed.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/copy_stub.cmake"
"if(EXISTS \"\${KERNEL_JSON}\")
  file(COPY_FILE \"\${KERNEL_JSON}\" \"\${KERNEL_STUB_JSON}\")
  message(STATUS \"Copied stub: \${KERNEL_STUB_JSON}\")
else()
  message(STATUS \"Stub not yet generated (run install_kernel.sh): \${KERNEL_JSON}\")
endif()
")

# ── Convenience targets ───────────────────────────────────────────────────────
add_custom_target(build_kernel_lib DEPENDS imgproc_core
    COMMENT "Building imgproc_core for notebook use")

add_custom_target(install_kernel DEPENDS build_kernel_lib
    COMMAND ${CMAKE_COMMAND} -E make_directory "${KERNEL_STUB}"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${KERNEL_DIR}/kernel.json"
            "${KERNEL_STUB}/kernel.json"
    COMMENT "Registering c_imgproc stub with Jupyter"
    VERBATIM
)

add_custom_target(uninstall_kernel
    COMMAND "${PIPX_PYTHON}" -m jupyter kernelspec uninstall c_imgproc -y
    COMMENT "Removing c_imgproc kernel from Jupyter"
    VERBATIM
)

add_custom_target(check_tools
    COMMAND bash -c "command -v gcc     || (echo 'ERROR: gcc not found'      && exit 1)"
    COMMAND bash -c "command -v g++     || echo 'WARNING: g++ not found'"
    COMMAND bash -c "command -v gdb     || echo 'WARNING: gdb not found'"
    COMMAND bash -c "command -v gnuplot || echo 'WARNING: gnuplot not found'"
    COMMAND bash -c "command -v gnuplot && (echo 'set terminal pngcairo' | gnuplot -e '' 2>&1 | grep -qi 'unknown' && echo 'WARNING: gnuplot pngcairo missing' || echo 'OK: gnuplot pngcairo') || true"
    COMMENT "Checking gcc / g++ / gdb / gnuplot"
    VERBATIM
)

# ── Optional: unit tests ──────────────────────────────────────────────────────
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "imgPrcsngC configuration")
message(STATUS "  source dir  : ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "  build dir   : ${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "  core lib    : ${CMAKE_CURRENT_BINARY_DIR}/libimgproc_core.a")
message(STATUS "  nn lib      : ${CMAKE_CURRENT_BINARY_DIR}/libaberration_nn.a")
message(STATUS "  executable  : ${CMAKE_CURRENT_BINARY_DIR}/imgPrcsng")
message(STATUS "  kernel src  : ${CMAKE_CURRENT_SOURCE_DIR}/kernel/")
message(STATUS "  kernel dir  : ${KERNEL_DIR}/")
message(STATUS "  kernel stub : ${KERNEL_STUB}/kernel.json")
message(STATUS "")
message(STATUS "Targets: imgproc_core  aberration_nn  imgPrcsng")
message(STATUS "         install_kernel  uninstall_kernel  check_tools  build_kernel_lib")
message(STATUS "")
