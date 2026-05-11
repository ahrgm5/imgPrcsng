#!/usr/bin/env bash
# Jupyter_c_kernels.sh
#
# Full clean install of JupyterLab + two C kernels.
# Run from the project root:
#
#   cd ~/Projects/Programming/imgPrcsng
#   bash Jupyter_c_kernels.sh
#
# Layout
# ──────────────────────────────────────────────────────────────────────────────
#   imgPrcsng/
#   ├── CMakeLists.txt
#   ├── Jupyter_c_kernels.sh     ← this file
#   ├── install_kernel.sh
#   ├── include/
#   ├── src/
#   └── build/
#       ├── libimgproc_core.a
#       ├── imgPrcsng
#       └── kernel/
#           ├── kernel.py.in     template  (edit this one)
#           ├── kernel.json.in   template
#           ├── kernel.py        cmake-generated
#           ├── kernel.json      cmake-generated
#           └── _shims/
#               └── gnuplot      (written at first kernel launch)
#
#   ~/.local/share/jupyter/kernels/c_imgproc/
#   └── kernel.json              stub (cmake POST_BUILD keeps this in sync)
#
# Never source this script.

if (return 0 2>/dev/null); then
    echo "ERROR: Do not source this script — run it as:  bash Jupyter_c_kernels.sh"
    return 1
fi

set -euo pipefail

usage() {
    echo "Usage: bash Jupyter_c_kernels.sh [--make | --ninja]"
    echo ""
    echo "  --make   Force Unix Makefiles generator"
    echo "  --ninja  Force Ninja generator (default when ninja is installed)"
    echo ""
    echo "If no flag is given, Ninja is used when available, Make otherwise."
    exit 0
}

main() {

export DEBIAN_FRONTEND=noninteractive

# ── parse arguments ───────────────────────────────────────────────────────────
FORCE_GENERATOR=""
for arg in "$@"; do
    case "$arg" in
        --make)  FORCE_GENERATOR="Unix Makefiles" ;;
        --ninja) FORCE_GENERATOR="Ninja" ;;
        --help|-h) usage ;;
        *) echo "ERROR: Unknown argument: $arg"; usage ;;
    esac
done

# ── self-locate ───────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

IMGPROC_SOURCE_DIR="$SCRIPT_DIR"
IMGPROC_BUILD_DIR="$IMGPROC_SOURCE_DIR/build"
KERNEL_SRC="$IMGPROC_SOURCE_DIR/kernel"
KERNEL_DIR="$IMGPROC_BUILD_DIR/kernel"
KERNEL_STUB_DIR="$HOME/.local/share/jupyter/kernels/c_imgproc"
IMGPROC_INCLUDE_DIR="$IMGPROC_SOURCE_DIR/include"
IMGPROC_SRC_DIR="$IMGPROC_SOURCE_DIR/src"
IMGPROC_LIB="$IMGPROC_BUILD_DIR/libimgproc_core.a"

# ── build tool detection ──────────────────────────────────────────────────────
# Use forced generator if provided, otherwise prefer Ninja, fall back to Make.
if [[ -n "$FORCE_GENERATOR" ]]; then
    CMAKE_GENERATOR="$FORCE_GENERATOR"
    echo "Build tool : $CMAKE_GENERATOR (forced via flag)"
elif command -v ninja &>/dev/null; then
    CMAKE_GENERATOR="Ninja"
    echo "Build tool : ninja ($(ninja --version))  — pass --make to use Make instead"
else
    CMAKE_GENERATOR="Unix Makefiles"
    echo "Build tool : make (ninja not found)"
fi

echo "Source dir : $IMGPROC_SOURCE_DIR"
echo "Build dir  : $IMGPROC_BUILD_DIR"
echo "Kernel dir : $KERNEL_DIR"

export PATH="$HOME/.local/bin:$PATH"
if ! grep -q 'local/bin' "$HOME/.bashrc" 2>/dev/null; then
    echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$HOME/.bashrc"
fi

# ── helpers ───────────────────────────────────────────────────────────────────
find_pipx_venv() {
    local name="$1"
    for candidate in \
        "$HOME/.local/share/pipx/venvs/$name" \
        "$HOME/.local/pipx/venvs/$name"
    do
        [[ -d "$candidate" ]] && echo "$candidate" && return 0
    done
    echo "ERROR: pipx venv '$name' not found." >&2
    return 1
}

# ── 1. purge Jupyter ──────────────────────────────────────────────────────────
echo "--- Purging previous Jupyter installations ---"

PIPX_JUPYTER_PACKAGES=(
    jupyterlab jupyter jupyter-core jupyter-client jupyter-server
    notebook jupyter-c-kernel jupyterlab-git
    nbclassic voila
)

if command -v pipx &>/dev/null; then
    for pkg in "${PIPX_JUPYTER_PACKAGES[@]}"; do
        pipx uninstall "$pkg" 2>/dev/null && echo "  removed: $pkg" || true
    done
    pipx list --short 2>/dev/null \
        | awk '{print $1}' | grep -i jupyter \
        | xargs -r -I{} pipx uninstall {} 2>/dev/null || true
    if command -v jupyter &>/dev/null; then
        jupyter kernelspec list --json 2>/dev/null \
            | python3 -c "
import sys, json
raw = sys.stdin.read().strip()
if not raw: sys.exit(0)
for name in json.loads(raw).get('kernelspecs', {}):
    if name != 'python3': print(name)
" | xargs -r -I{} jupyter kernelspec uninstall {} -y 2>/dev/null || true
    fi
fi

rm -rf \
    ~/.local/share/jupyter/kernels/c_* \
    ~/.local/share/jupyter/kernels/python2* \
    ~/.jupyter \
    ~/.local/share/jupyter \
    ~/.local/etc/jupyter

echo "--- Purge complete ---"

# ── 2. system deps ────────────────────────────────────────────────────────────
sudo apt-get update -qq
sudo apt-get install -y --no-install-recommends \
    build-essential cmake git \
    gcc gdb \
    libfftw3-dev \
    gnuplot \
    pkg-config \
    python3-dev \
    pipx

# Install ninja if available in the package repo; non-fatal if absent
sudo apt-get install -y --no-install-recommends ninja-build 2>/dev/null || true

# Re-evaluate build tool after apt run unless a generator was forced
if [[ -z "$FORCE_GENERATOR" ]]; then
    if command -v ninja &>/dev/null; then
        CMAKE_GENERATOR="Ninja"
        echo "Build tool : ninja ($(ninja --version))  — pass --make to use Make instead"
    else
        CMAKE_GENERATOR="Unix Makefiles"
        echo "Build tool : make (ninja unavailable, using make)"
    fi
fi

pipx ensurepath --force
export PATH="$HOME/.local/bin:$PATH"

# ── 3. JupyterLab ─────────────────────────────────────────────────────────────
echo "--- Installing JupyterLab ---"
pipx install jupyterlab --include-deps

PIPX_VENV=$(find_pipx_venv jupyterlab) || return 1
PIPX_PYTHON="$PIPX_VENV/bin/python"
PIPX_BIN_DIR="$PIPX_VENV/bin"
export PATH="$PIPX_BIN_DIR:$PATH"

[[ -x "$PIPX_PYTHON" ]] || { echo "ERROR: $PIPX_PYTHON not executable."; return 1; }

# ── 4. generic jupyter-c-kernel ───────────────────────────────────────────────
echo "--- Installing jupyter-c-kernel (generic C) ---"
pipx inject jupyterlab jupyter-c-kernel --include-apps

"$PIPX_PYTHON" -c "import jupyter_c_kernel" 2>/dev/null \
    || { echo "ERROR: jupyter_c_kernel not importable."; return 1; }

if [[ -x "$PIPX_BIN_DIR/install_c_kernel" ]]; then
    "$PIPX_BIN_DIR/install_c_kernel" --user
elif [[ -x "$HOME/.local/bin/install_c_kernel" ]]; then
    "$HOME/.local/bin/install_c_kernel" --user
else
    echo "ERROR: install_c_kernel not found."; return 1
fi

GENERIC_C_KERNEL_DIR=$(jupyter kernelspec list --json \
    | python3 -c "
import sys, json
specs = json.loads(sys.stdin.read()).get('kernelspecs', {})
for name, info in specs.items():
    if ('c' in name.lower()
            and 'cpp'     not in name.lower()
            and 'clang'   not in name.lower()
            and 'imgproc' not in name.lower()):
        print(info['resource_dir']); break
")
[[ -n "$GENERIC_C_KERNEL_DIR" ]] \
    || { echo "ERROR: generic C kernelspec not found."; return 1; }

cat > "$GENERIC_C_KERNEL_DIR/kernel.json" <<EOF
{
  "display_name": "C",
  "argv": [
    "$PIPX_PYTHON",
    "-m", "jupyter_c_kernel",
    "-f", "{connection_file}"
  ],
  "language": "c"
}
EOF
echo "  Generic C kernel: $GENERIC_C_KERNEL_DIR"

# ── 5. cmake: configure + build ───────────────────────────────────────────────
[[ -f "$IMGPROC_SOURCE_DIR/CMakeLists.txt" ]] \
    || { echo "ERROR: CMakeLists.txt not found at $IMGPROC_SOURCE_DIR"; return 1; }

[[ -f "$KERNEL_SRC/kernel.py.in" ]] \
    || { echo "ERROR: $KERNEL_SRC/kernel.py.in not found."; \
         echo "       Place kernel.py.in and kernel.json.in in $KERNEL_SRC/"; \
         return 1; }
[[ -f "$KERNEL_SRC/kernel.json.in" ]] \
    || { echo "ERROR: $KERNEL_SRC/kernel.json.in not found."; return 1; }

# ── wipe stale build directory before configuring ─────────────────────────
echo "--- Cleaning build directory ---"
rm -rf "$IMGPROC_BUILD_DIR"
mkdir -p "$KERNEL_DIR"

# ── generate kernel files from .in templates (sed, not cmake) ─────────────
# Done after clean so there is only one generation pass and the files
# are never wiped by the rm -rf above.
echo "--- Generating kernel files ---"
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

# ── copy stub immediately after generation, before build ──────────────────
# The stub only contains paths — it does not depend on the compiled library.
# Copying here means the kernel is visible to Jupyter/VS Code even if the
# build fails later, and re-running the script after fixing a build error
# does not require a separate manual copy step.
echo "--- Installing stub ---"
mkdir -p "$KERNEL_STUB_DIR"
cp "$KERNEL_DIR/kernel.json" "$KERNEL_STUB_DIR/kernel.json"
echo "  Copied: $KERNEL_STUB_DIR/kernel.json"

echo "--- Configuring imgPrcsng (generator: $CMAKE_GENERATOR) ---"
cmake -S "$IMGPROC_SOURCE_DIR" -B "$IMGPROC_BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DPIPX_PYTHON="$PIPX_PYTHON" \
    -G "$CMAKE_GENERATOR"

echo "--- Building imgproc_core ---"
cmake --build "$IMGPROC_BUILD_DIR" --target imgproc_core

[[ -f "$IMGPROC_LIB" ]] \
    || { echo "ERROR: $IMGPROC_LIB not found after build."; return 1; }
echo "  Built: $IMGPROC_LIB"

# ── 6. verify ─────────────────────────────────────────────────────────────────
echo "--- Verifying kernel files ---"
[[ -f "$KERNEL_DIR/kernel.py"        ]] || { echo "ERROR: $KERNEL_DIR/kernel.py missing.";        return 1; }
[[ -f "$KERNEL_DIR/kernel.json"      ]] || { echo "ERROR: $KERNEL_DIR/kernel.json missing.";      return 1; }
[[ -f "$KERNEL_STUB_DIR/kernel.json" ]] || { echo "ERROR: $KERNEL_STUB_DIR/kernel.json missing."; return 1; }
echo "  OK: $KERNEL_DIR/kernel.py"
echo "  OK: $KERNEL_DIR/kernel.json"
echo "  OK: $KERNEL_STUB_DIR/kernel.json"

# ── 7. jupyterlab-git ─────────────────────────────────────────────────────────
pipx inject jupyterlab jupyterlab-git

# ── 8. VSCode configs ─────────────────────────────────────────────────────────
VSCODE_DIR="$IMGPROC_SOURCE_DIR/.vscode"
mkdir -p "$VSCODE_DIR"

cat > "$VSCODE_DIR/settings.json" <<EOF
{
  "cmake.configureArgs": [
    "-DPIPX_PYTHON=$PIPX_PYTHON"
  ],
  "cmake.buildDirectory": "$IMGPROC_BUILD_DIR",
  "C_Cpp.default.compileCommands": "$IMGPROC_BUILD_DIR/compile_commands.json"
}
EOF

[[ -f "$VSCODE_DIR/launch.json" ]] || cat > "$VSCODE_DIR/launch.json" <<EOF
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug imgPrcsng",
      "type": "cppdbg",
      "request": "launch",
      "program": "$IMGPROC_BUILD_DIR/imgPrcsng",
      "args": [],
      "stopAtEntry": false,
      "cwd": "$IMGPROC_SOURCE_DIR",
      "environment": [],
      "externalConsole": false,
      "MIMode": "gdb",
      "miDebuggerPath": "/usr/bin/gdb",
      "preLaunchTask": "cmake: build imgPrcsng",
      "setupCommands": [
        { "description": "Pretty-print STL",     "text": "-enable-pretty-printing",                     "ignoreFailures": true },
        { "description": "Skip glibc internals", "text": "-interpreter-exec console \"skip -rfu ^__\"", "ignoreFailures": true }
      ]
    },
    {
      "name": "Debug imgPrcsng (attach)",
      "type": "cppdbg",
      "request": "attach",
      "program": "$IMGPROC_BUILD_DIR/imgPrcsng",
      "processId": "\${command:pickProcess}",
      "MIMode": "gdb",
      "miDebuggerPath": "/usr/bin/gdb",
      "setupCommands": [
        { "description": "Pretty-print STL", "text": "-enable-pretty-printing", "ignoreFailures": true }
      ]
    },
    {
      "name": "Debug current test",
      "type": "cppdbg",
      "request": "launch",
      "program": "$IMGPROC_BUILD_DIR/_vscode_test",
      "args": [],
      "stopAtEntry": false,
      "cwd": "$IMGPROC_SOURCE_DIR",
      "environment": [],
      "externalConsole": false,
      "MIMode": "gdb",
      "miDebuggerPath": "/usr/bin/gdb",
      "preLaunchTask": "cmake: build current test",
      "setupCommands": [
        { "description": "Pretty-print STL", "text": "-enable-pretty-printing", "ignoreFailures": true }
      ]
    }
  ]
}
EOF

[[ -f "$VSCODE_DIR/tasks.json" ]] || cat > "$VSCODE_DIR/tasks.json" <<EOF
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "cmake: build imgPrcsng",
      "type": "shell",
      "command": "cmake --build $IMGPROC_BUILD_DIR --target imgPrcsng",
      "group": { "kind": "build", "isDefault": true },
      "presentation": { "reveal": "silent", "panel": "shared" },
      "problemMatcher": "\$gcc"
    },
    {
      "label": "cmake: build imgproc_core",
      "type": "shell",
      "command": "cmake --build $IMGPROC_BUILD_DIR --target imgproc_core",
      "group": "build",
      "presentation": { "reveal": "silent", "panel": "shared" },
      "problemMatcher": "\$gcc"
    },
    {
      "label": "cmake: build current test",
      "type": "shell",
      "command": "gcc -g3 -O0 -std=c11 -I$IMGPROC_SOURCE_DIR/include -I$IMGPROC_SOURCE_DIR/src \${file} $IMGPROC_LIB -lfftw3 -lm -o $IMGPROC_BUILD_DIR/_vscode_test",
      "group": "build",
      "presentation": { "reveal": "silent", "panel": "shared" },
      "problemMatcher": "\$gcc"
    }
  ]
}
EOF
echo "  VSCode configs ready."

# ── 9. desktop entry ──────────────────────────────────────────────────────────
mkdir -p ~/.local/share/applications
cat > ~/.local/share/applications/jupyterlab.desktop <<EOF
[Desktop Entry]
Name=JupyterLab
Comment=Interactive notebook environment
Exec=bash -c '$PIPX_BIN_DIR/jupyter lab --no-browser & sleep 2 && xdg-open http://localhost:8888'
Icon=utilities-terminal
Terminal=false
Type=Application
Categories=Development;Science;Education;
Keywords=jupyter;notebook;c;
EOF
update-desktop-database ~/.local/share/applications 2>/dev/null || true

# ── 10. summary ───────────────────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " Registered kernels"
echo "════════════════════════════════════════"
jupyter kernelspec list

echo ""
echo "File layout:"
echo "  $KERNEL_DIR/kernel.py.in     (template — edit this)"
echo "  $KERNEL_DIR/kernel.json.in   (template)"
echo "  $KERNEL_DIR/kernel.py        (generated)"
echo "  $KERNEL_DIR/kernel.json      (generated)"
echo "  $KERNEL_DIR/_shims/          (written at first kernel launch)"
echo "  $KERNEL_STUB_DIR/kernel.json (stub)"
echo ""
echo "════════════════════════════════════════"
echo " PATH setup"
echo "════════════════════════════════════════"

# Ensure ~/.local/bin is in .bashrc (idempotent)
if ! grep -q 'local/bin' "$HOME/.bashrc" 2>/dev/null; then
    echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$HOME/.bashrc"
    echo "  Added ~/.local/bin to ~/.bashrc"
fi

# Also patch .zshrc if zsh is present
if command -v zsh &>/dev/null && [[ -f "$HOME/.zshrc" ]]; then
    if ! grep -q 'local/bin' "$HOME/.zshrc" 2>/dev/null; then
        echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$HOME/.zshrc"
        echo "  Added ~/.local/bin to ~/.zshrc"
    fi
fi

# Create a system-wide symlink so 'jupyter' works immediately in any
# terminal without requiring the user to source .bashrc or open a new shell.
# /usr/local/bin is on PATH by default on all Debian/Ubuntu systems.
JUPYTER_BIN="$PIPX_BIN_DIR/jupyter"
if [[ -x "$JUPYTER_BIN" ]]; then
    sudo ln -sf "$JUPYTER_BIN" /usr/local/bin/jupyter
    echo "  Symlinked: /usr/local/bin/jupyter -> $JUPYTER_BIN"
else
    echo "  WARNING: jupyter not found at $JUPYTER_BIN — symlink not created"
fi

echo ""
echo "════════════════════════════════════════"
echo " Registered kernels"
echo "════════════════════════════════════════"
jupyter kernelspec list

echo ""
echo "File layout:"
echo "  $KERNEL_DIR/kernel.py.in     (template — edit this)"
echo "  $KERNEL_DIR/kernel.json.in   (template)"
echo "  $KERNEL_DIR/kernel.py        (generated)"
echo "  $KERNEL_DIR/kernel.json      (generated)"
echo "  $KERNEL_DIR/_shims/          (written at first kernel launch)"
echo "  $KERNEL_STUB_DIR/kernel.json (stub)"
echo ""
echo "Done.  Start JupyterLab:"
echo "    jupyter lab"
echo ""
echo "To update the kernel after editing kernel.py.in:"
echo "    bash install_kernel.sh"

} # end main()

if ! main "$@"; then

    echo ""
    echo "Installation failed — see the error above."
    exit 1
fi
exit 0