cmake_minimum_required(VERSION 3.16)
project(imgPrcsng VERSION 1.0 LANGUAGES C)

# ── Dependencies ──────────────────────────────────────────────────────────────
find_library(FFTW_LIB NAMES fftw3 REQUIRED)

# PIPX_PYTHON — path to the Python inside the pipx jupyterlab venv.
# Declared as a CACHE STRING so it survives cmake re-runs (e.g. when cmake
# detects a compiler change and re-configures internally without replaying
# the original -D flags). Once set by Jupyter_c_kernels.sh it is sticky.
set(PIPX_PYTHON "" CACHE STRING
    "Absolute path to the pipx jupyterlab venv Python interpreter")

if(PIPX_PYTHON STREQUAL "")
    # Not set yet — find system python3 as a fallback so cmake doesn't error,
    # but warn so the user knows the kernel.json will point to the wrong Python.
    find_program(_sys_python NAMES python3 REQUIRED)
    set(PIPX_PYTHON "${_sys_python}" CACHE STRING
        "Absolute path to the pipx jupyterlab venv Python interpreter" FORCE)
    message(WARNING
        "PIPX_PYTHON not set — using system python3 (${PIPX_PYTHON}).\n"
        "The c_imgproc kernel may not work in VSCode.\n"
        "Run Jupyter_c_kernels.sh to set the correct interpreter.")
endif()
message(STATUS "PIPX_PYTHON = ${PIPX_PYTHON}")

# ── Static library ────────────────────────────────────────────────────────────
# Notebook cells link against this instead of recompiling your sources.
add_library(imgproc_lib STATIC
    src/bmp.c
    src/transforms.c
    src/imgproc.c
)
target_include_directories(imgproc_lib PUBLIC include src)
target_link_libraries(imgproc_lib PUBLIC ${FFTW_LIB} m)
set_target_properties(imgproc_lib PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
# Debug symbols so GDB can step into your source files from VSCode
target_compile_options(imgproc_lib PRIVATE -g3 -O0)

# ── Main executable ───────────────────────────────────────────────────────────
add_executable(imgPrcsng main.c)
target_link_libraries(imgPrcsng PRIVATE imgproc_lib)
set_target_properties(imgPrcsng PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

# ── Jupyter kernel ────────────────────────────────────────────────────────────
set(KERNEL_SRC   "${CMAKE_CURRENT_SOURCE_DIR}/kernel")
set(KERNEL_BUILD "${CMAKE_CURRENT_BINARY_DIR}/kernel_install")

# Bake all paths into the kernel files at configure time.
# @VAR@ placeholders in .in files are replaced by cmake configure_file.
set(IMGPROC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(IMGPROC_SRC_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(IMGPROC_LIB_PATH    "${CMAKE_CURRENT_BINARY_DIR}/libimgproc_lib.a")
# PIPX_PYTHON is already set above — it gets baked into kernel.json.in too,
# so the registered kernel.json has the correct argv[0] from the start
# without needing any post-registration patching.

configure_file(
    "${KERNEL_SRC}/kernel.py.in"
    "${KERNEL_BUILD}/kernel.py"
    @ONLY
)
configure_file(
    "${KERNEL_SRC}/kernel.json.in"
    "${KERNEL_BUILD}/kernel.json"
    @ONLY
)
configure_file(
    "${KERNEL_SRC}/__main__.py"
    "${KERNEL_BUILD}/__main__.py"
    COPYONLY
)

# ── build_kernel_lib ──────────────────────────────────────────────────────────
# Convenience target: ensure the static lib is built before any kernel use.
add_custom_target(build_kernel_lib
    DEPENDS imgproc_lib
    COMMENT "Building imgproc_lib for notebook use"
)

# ── install_kernel ────────────────────────────────────────────────────────────
# Uses PIPX_PYTHON (not system python3) so:
#   • kernelspec install itself runs in the right venv
#   • the registered kernel.json already has the correct argv[0] via configure_file
# Note: --display-name is passed as a single cmake argument (no space after =)
#       which cmake forwards as one shell token — avoiding the split-arg bug.
add_custom_target(install_kernel
    DEPENDS build_kernel_lib
    COMMAND "${PIPX_PYTHON}" -m jupyter kernelspec install
            "${KERNEL_BUILD}"
            --name=c_imgproc
            --user
    COMMENT "Registering C+GDB kernel with Jupyter"
    VERBATIM
)

# ── uninstall_kernel ──────────────────────────────────────────────────────────
add_custom_target(uninstall_kernel
    COMMAND "${PIPX_PYTHON}" -m jupyter kernelspec uninstall c_imgproc -y
    COMMENT "Removing c_imgproc kernel from Jupyter"
    VERBATIM
)

# ── check_tools ───────────────────────────────────────────────────────────────
add_custom_target(check_tools
    COMMAND bash -c "command -v gcc || (echo 'ERROR: gcc not found' && exit 1)"
    COMMAND bash -c "command -v gdb || echo 'WARNING: gdb not found — no backtraces'"
    COMMENT "Checking gcc / gdb"
    VERBATIM
)