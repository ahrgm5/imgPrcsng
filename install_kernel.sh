#!/usr/bin/env bash
# install_kernel.sh
#
# Generates build/kernel/kernel.py and kernel.json from the .in templates,
# then copies the stub to the Jupyter kernels directory.
#
# Run from the project root:
#   cd ~/Projects/imgPrcsngC
#   bash install_kernel.sh

set -euo pipefail

# ── self-locate ───────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

IMGPROC_SOURCE_DIR="$SCRIPT_DIR"
IMGPROC_BUILD_DIR="$IMGPROC_SOURCE_DIR/build"
IMGPROC_INCLUDE_DIR="$IMGPROC_SOURCE_DIR/include"
IMGPROC_SRC_DIR="$IMGPROC_SOURCE_DIR/src"
IMGPROC_LIB="$IMGPROC_BUILD_DIR/libimgproc_lib.a"

KERNEL_SRC="$IMGPROC_SOURCE_DIR/kernel"
KERNEL_DIR="$IMGPROC_BUILD_DIR/kernel"
KERNEL_STUB_DIR="$HOME/.local/share/jupyter/kernels/c_imgproc"

echo "Source dir : $IMGPROC_SOURCE_DIR"
echo "Build dir  : $IMGPROC_BUILD_DIR"
echo "Kernel src : $KERNEL_SRC"
echo "Kernel dir : $KERNEL_DIR"

# ── guards ────────────────────────────────────────────────────────────────────
[[ -f "$IMGPROC_SOURCE_DIR/CMakeLists.txt" ]] \
    || { echo "ERROR: CMakeLists.txt not found."; exit 1; }

[[ -f "$KERNEL_SRC/kernel.py.in" ]] \
    || { echo "ERROR: $KERNEL_SRC/kernel.py.in not found."; exit 1; }

[[ -f "$KERNEL_SRC/kernel.json.in" ]] \
    || { echo "ERROR: $KERNEL_SRC/kernel.json.in not found."; exit 1; }

# ── find the pipx Python ──────────────────────────────────────────────────────
find_pipx_python() {
    for candidate in \
        "$HOME/.local/share/pipx/venvs/jupyterlab/bin/python" \
        "$HOME/.local/pipx/venvs/jupyterlab/bin/python"
    do
        [[ -x "$candidate" ]] && echo "$candidate" && return 0
    done
    local p
    p="$(command -v python3 2>/dev/null || true)"
    [[ -n "$p" ]] && echo "$p" && return 0
    echo "ERROR: no Python interpreter found." >&2
    return 1
}

PIPX_PYTHON="$(find_pipx_python)"
echo "Python     : $PIPX_PYTHON"

# ── 1. substitute paths into kernel files ────────────────────────────────────
# cmake no longer does this (configure_file caused an infinite reconfigure
# loop by writing into build/ during every cmake run).
# sed does the @PLACEHOLDER@ substitution here instead.
echo "--- Generating kernel files ---"
mkdir -p "$KERNEL_DIR"

sed \
    -e "s|@IMGPROC_INCLUDE_DIR@|$IMGPROC_INCLUDE_DIR|g" \
    -e "s|@IMGPROC_SRC_DIR@|$IMGPROC_SRC_DIR|g" \
    -e "s|@IMGPROC_LIB_PATH@|$IMGPROC_LIB|g" \
    "$KERNEL_SRC/kernel.py.in" > "$KERNEL_DIR/kernel.py"
echo "  Written: $KERNEL_DIR/kernel.py"

sed \
    -e "s|@PIPX_PYTHON@|$PIPX_PYTHON|g" \
    -e "s|@KERNEL_DIR@|$KERNEL_DIR|g" \
    "$KERNEL_SRC/kernel.json.in" > "$KERNEL_DIR/kernel.json"
echo "  Written: $KERNEL_DIR/kernel.json"

# ── 2. cmake configure ────────────────────────────────────────────────────────
echo "--- Configuring ---"
cmake -S "$IMGPROC_SOURCE_DIR" -B "$IMGPROC_BUILD_DIR" \
    -DPIPX_PYTHON="$PIPX_PYTHON" \
    -GNinja

# ── 3. build ──────────────────────────────────────────────────────────────────
echo "--- Building imgproc_lib ---"
cmake --build "$IMGPROC_BUILD_DIR" --target imgproc_lib

# ── 4. copy stub directly (don't rely on cmake POST_BUILD) ────────────────────
echo "--- Installing stub ---"
mkdir -p "$KERNEL_STUB_DIR"
cp "$KERNEL_DIR/kernel.json" "$KERNEL_STUB_DIR/kernel.json"
echo "  Copied: $KERNEL_STUB_DIR/kernel.json"

# ── 5. verify ─────────────────────────────────────────────────────────────────
echo ""
[[ -f "$KERNEL_DIR/kernel.py"        ]] \
    && echo "OK : $KERNEL_DIR/kernel.py"        \
    || echo "MISSING: $KERNEL_DIR/kernel.py"
[[ -f "$KERNEL_DIR/kernel.json"      ]] \
    && echo "OK : $KERNEL_DIR/kernel.json"      \
    || echo "MISSING: $KERNEL_DIR/kernel.json"
[[ -f "$KERNEL_STUB_DIR/kernel.json" ]] \
    && echo "OK : $KERNEL_STUB_DIR/kernel.json" \
    || echo "MISSING: $KERNEL_STUB_DIR/kernel.json"

# ── 6. gnuplot check ──────────────────────────────────────────────────────────
REAL_GNUPLOT="$(command -v gnuplot 2>/dev/null || true)"
if [[ -z "$REAL_GNUPLOT" ]]; then
    echo ""
    echo "WARNING: gnuplot not found — install: sudo apt-get install gnuplot"
elif echo "set terminal pngcairo" | "$REAL_GNUPLOT" -e "" 2>&1 | grep -qi "unknown terminal"; then
    echo ""
    echo "WARNING: gnuplot lacks pngcairo — reinstall: sudo apt-get install --reinstall gnuplot"
else
    echo "OK : gnuplot pngcairo ($REAL_GNUPLOT)"
fi

echo ""
echo "Done.  Restart the c_imgproc kernel in JupyterLab:"
echo "  Kernel -> Shut Down All Kernels -> reopen notebook -> run a cell"
