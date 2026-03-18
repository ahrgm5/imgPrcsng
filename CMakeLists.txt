cmake_minimum_required(VERSION 3.16)
project(imgPrcsng VERSION 1.0 LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
message(STATUS "Build type  : ${CMAKE_BUILD_TYPE}")

# ── fftw3 ─────────────────────────────────────────────────────────────────────
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

# ── static library ────────────────────────────────────────────────────────────
add_library(imgproc_lib STATIC
    src/bmp.c
    src/transforms.c
    src/imgproc.c
)

target_include_directories(imgproc_lib PUBLIC
    include src ${FFTW_INCLUDE_DIR})

target_link_libraries(imgproc_lib PUBLIC ${FFTW_LIB} m)

set_target_properties(imgproc_lib PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

target_compile_definitions(imgproc_lib PRIVATE _POSIX_C_SOURCE=200809L)

target_compile_options(imgproc_lib PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    $<$<CONFIG:Debug>:   -g3 -O0>
    $<$<CONFIG:Release>: -O3 -DNDEBUG>
)

# ── main executable ───────────────────────────────────────────────────────────
add_executable(imgPrcsng main.c)
target_link_libraries(imgPrcsng PRIVATE imgproc_lib)
set_target_properties(imgPrcsng PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

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
#   imgPrcsngC/kernel/          .in templates  (source tree, read by sed)
#   build/kernel/               generated files (written by sed in shell scripts)
#   ~/.local/.../c_imgproc/     stub            (copied by POST_BUILD below)

set(KERNEL_DIR  "${CMAKE_CURRENT_BINARY_DIR}/kernel")
set(KERNEL_STUB "$ENV{HOME}/.local/share/jupyter/kernels/c_imgproc")

# POST_BUILD: copy the stub kernel.json (written by the shell scripts) to the
# Jupyter kernels directory after every successful build of imgproc_lib.
# If the file does not exist yet the copy is skipped gracefully.
add_custom_command(TARGET imgproc_lib POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${KERNEL_STUB}"
    COMMAND ${CMAKE_COMMAND}
        -DKERNEL_JSON="${KERNEL_DIR}/kernel.json"
        -DKERNEL_STUB_JSON="${KERNEL_STUB}/kernel.json"
        -P "${CMAKE_CURRENT_BINARY_DIR}/copy_stub.cmake"
    COMMENT "Refreshing Jupyter stub: ${KERNEL_STUB}/kernel.json"
    VERBATIM
)

# Write the copy helper into the build dir at configure time —
# no source-tree cmake/ directory needed.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/copy_stub.cmake"
"if(EXISTS "\${KERNEL_JSON}")
  file(COPY_FILE "\${KERNEL_JSON}" "\${KERNEL_STUB_JSON}")
  message(STATUS "Copied stub: \${KERNEL_STUB_JSON}")
else()
  message(STATUS "Stub not yet generated (run install_kernel.sh): \${KERNEL_JSON}")
endif()
")

# ── convenience targets ───────────────────────────────────────────────────────
add_custom_target(build_kernel_lib DEPENDS imgproc_lib
    COMMENT "Building imgproc_lib for notebook use")

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
    COMMAND bash -c "command -v gcc     || (echo 'ERROR: gcc not found' && exit 1)"
    COMMAND bash -c "command -v gdb     || echo 'WARNING: gdb not found'"
    COMMAND bash -c "command -v gnuplot || echo 'WARNING: gnuplot not found'"
    COMMAND bash -c "command -v gnuplot && (echo 'set terminal pngcairo' | gnuplot -e '' 2>&1 | grep -qi 'unknown' && echo 'WARNING: gnuplot pngcairo missing' || echo 'OK: gnuplot pngcairo') || true"
    COMMENT "Checking gcc / gdb / gnuplot"
    VERBATIM
)

# ── summary ───────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "imgPrcsng configuration")
message(STATUS "  source dir  : ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "  build dir   : ${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "  library     : ${CMAKE_CURRENT_BINARY_DIR}/libimgproc_lib.a")
message(STATUS "  executable  : ${CMAKE_CURRENT_BINARY_DIR}/imgPrcsng")
message(STATUS "  kernel src  : ${CMAKE_CURRENT_SOURCE_DIR}/kernel/")
message(STATUS "  kernel dir  : ${KERNEL_DIR}/")
message(STATUS "  kernel stub : ${KERNEL_STUB}/kernel.json")
message(STATUS "")
message(STATUS "Targets: imgproc_lib  imgPrcsng  install_kernel  uninstall_kernel  check_tools")
message(STATUS "")