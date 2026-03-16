#!/usr/bin/env bash
# Jupyter_c_kernels.sh
# Clean install of JupyterLab + two kernels:
#   • C (imgPrcsng)  — custom kernel: auto-includes, auto-main, GDB magic
#   • C              — generic jupyter-c-kernel for standalone cells
#
# Run as:  bash Jupyter_c_kernels.sh
# Never:   source Jupyter_c_kernels.sh   (set -euo pipefail would kill your shell)

# ── sourced-vs-executed guard ─────────────────────────────────────────────────
if (return 0 2>/dev/null); then
    echo "ERROR: Do not source this script — run it as:  bash Jupyter_c_kernels.sh"
    return 1
fi

set -euo pipefail

main() {

# ── config ────────────────────────────────────────────────────────────────────
export DEBIAN_FRONTEND=noninteractive

IMGPROC_SOURCE_DIR="${IMGPROC_SOURCE_DIR:-$HOME/Projects/imgPrcsngC}"
IMGPROC_BUILD_DIR="${IMGPROC_BUILD_DIR:-$IMGPROC_SOURCE_DIR/build}"
IMGPROC_INCLUDE_DIR="$IMGPROC_SOURCE_DIR/include"
IMGPROC_SRC_DIR="$IMGPROC_SOURCE_DIR/src"
IMGPROC_LIB="$IMGPROC_BUILD_DIR/libimgproc_lib.a"

# The installed kernelspec dir — kernel.json argv[1] points here directly
# so both JupyterLab and VSCode find kernel.py without any placeholder magic
KERNEL_INSTALL_DIR="$HOME/.local/share/jupyter/kernels/c_imgproc"

export PATH="$HOME/.local/bin:$PATH"
if ! grep -q 'local/bin' "$HOME/.bashrc" 2>/dev/null; then
    echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$HOME/.bashrc"
    echo "  Added ~/.local/bin to .bashrc"
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
    echo "       Run: pipx list   to find the actual path." >&2
    return 1
}

# ── 1. purge ──────────────────────────────────────────────────────────────────
echo "--- Purging previous Jupyter installations ---"

PIPX_JUPYTER_PACKAGES=(
    jupyterlab jupyter jupyter-core jupyter-client jupyter-server
    notebook jupyter-c-kernel jupyterlab-git jupyterlab-debugger
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
else
    echo "  pipx not found — skipping (fresh system)"
fi

rm -rf \
    ~/.local/share/jupyter/kernels/c_*       \
    ~/.local/share/jupyter/kernels/c_imgproc \
    ~/.local/share/jupyter/kernels/python2*  \
    ~/.jupyter                               \
    ~/.local/share/jupyter                   \
    ~/.local/etc/jupyter

echo "--- Purge complete ---"

# ── 2. system deps ────────────────────────────────────────────────────────────
sudo apt-get update -qq
sudo apt-get install -y --no-install-recommends \
    build-essential cmake git \
    gcc gdb \
    libfftw3-dev \
    pkg-config \
    python3-dev \
    ninja-build pipx

pipx ensurepath
export PATH="$HOME/.local/bin:$PATH"

# ── 3. JupyterLab ─────────────────────────────────────────────────────────────
echo "--- Installing JupyterLab ---"
pipx install jupyterlab --include-deps

PIPX_VENV=$(find_pipx_venv jupyterlab) || return 1
PIPX_PYTHON="$PIPX_VENV/bin/python"
PIPX_BIN_DIR="$PIPX_VENV/bin"
export PATH="$PIPX_BIN_DIR:$PATH"

if [[ ! -x "$PIPX_PYTHON" ]]; then
    echo "ERROR: $PIPX_PYTHON is not executable."
    return 1
fi
echo "  pipx venv  : $PIPX_VENV"
echo "  pipx Python: $PIPX_PYTHON"

# ── 4. jupyter-c-kernel (generic C) ──────────────────────────────────────────
echo "--- Installing jupyter-c-kernel ---"
pipx inject jupyterlab jupyter-c-kernel --include-apps

if ! "$PIPX_PYTHON" -c "import jupyter_c_kernel" 2>/dev/null; then
    echo "ERROR: jupyter_c_kernel not importable — pipx inject may have failed."
    return 1
fi

if [[ -x "$PIPX_BIN_DIR/install_c_kernel" ]]; then
    "$PIPX_BIN_DIR/install_c_kernel" --user
elif [[ -x "$HOME/.local/bin/install_c_kernel" ]]; then
    "$HOME/.local/bin/install_c_kernel" --user
else
    echo "ERROR: install_c_kernel not found."
    return 1
fi

# Patch argv[0] to pipx Python — install_c_kernel writes system python3 by default
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
if [[ -z "$GENERIC_C_KERNEL_DIR" ]]; then
    echo "ERROR: Could not locate the generic C kernelspec."
    return 1
fi
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
echo "  Generic C kernel registered: $GENERIC_C_KERNEL_DIR"

# ── 5. imgPrcsng kernel ───────────────────────────────────────────────────────
# Strategy:
#   a) Write kernel.py directly into the final install location FIRST,
#      before cmake runs — so the file is always current regardless of
#      whether cmake's configure_file has run before or after.
#   b) Write kernel.json directly into the install location using the
#      absolute path to kernel.py — no {resource_dir} placeholder, which
#      VSCode does not support.
#   c) Run cmake only to build imgproc_lib. Kernel file management is
#      owned entirely by this script.

if [[ ! -f "$IMGPROC_SOURCE_DIR/CMakeLists.txt" ]]; then
    echo "WARNING: CMakeLists.txt not found at $IMGPROC_SOURCE_DIR"
    echo "         Skipping imgPrcsng build and c_imgproc kernel."
    echo "         Re-run with: IMGPROC_SOURCE_DIR=/path/to/project bash Jupyter_c_kernels.sh"
else
    # Create the kernelspec directory up front so we can write into it directly
    mkdir -p "$KERNEL_INSTALL_DIR"

    # ── kernel.json ──────────────────────────────────────────────────────────
    # argv[1] is the absolute path to kernel.py in the install dir.
    # This works in both JupyterLab and VSCode — no placeholder substitution needed.
    cat > "$KERNEL_INSTALL_DIR/kernel.json" <<EOF
{
  "display_name": "C (imgPrcsng)",
  "argv": [
    "$PIPX_PYTHON",
    "$KERNEL_INSTALL_DIR/kernel.py",
    "-f", "{connection_file}"
  ],
  "language": "c"
}
EOF
    echo "  kernel.json written: $KERNEL_INSTALL_DIR/kernel.json"

    # ── kernel.py ────────────────────────────────────────────────────────────
    # Written directly to the install dir with project paths substituted by
    # bash (not cmake). cmake is only used to build the static library.
    cat > "$KERNEL_INSTALL_DIR/kernel.py" <<EOF
from ipykernel.kernelbase import Kernel
import subprocess, tempfile, os, re, shutil

GDB_AVAILABLE = shutil.which("gdb") is not None
GCC_AVAILABLE = shutil.which("gcc") is not None

# Project paths — substituted by Jupyter_c_kernels.sh at install time
INCLUDE_DIR = "$IMGPROC_INCLUDE_DIR"
SRC_DIR     = "$IMGPROC_SRC_DIR"
LIB_PATH    = "$IMGPROC_LIB"
FFTW_LIB    = "$(find /usr -name 'libfftw3.so*' -o -name 'libfftw3.a' 2>/dev/null | head -1)"

COMPILE_FLAGS = [
    "-g3", "-O0", "-Wall",
    f"-I{INCLUDE_DIR}",
    f"-I{SRC_DIR}",
    "-std=c11",
]
LINK_FLAGS = [LIB_PATH, "-lfftw3", "-lm"]

# Prepended to every cell — no manual #include needed
AUTO_INCLUDES = [
    "#include <stdio.h>",
    "#include <stdlib.h>",
    "#include <string.h>",
    "#include <math.h>",
    '#include "bmp.h"',
    '#include "imgproc.h"',
    '#include "transforms.h"',
]

HELP_TEXT = """
┌──────────────────────────────────────────────────────────────┐
│           C (imgPrcsng) Kernel — Debug Commands              │
├──────────────────────────────────────────────────────────────┤
│  %break <N>   pause at line N                                │
│  %break <fn>  pause when function fn() is called             │
│  %print <x>   print value of x when paused                   │
│  %watch <x>   pause whenever variable x changes              │
│  %clear       remove all breakpoints and watches             │
│  %help        show this message                              │
├──────────────────────────────────────────────────────────────┤
│  bmp.h, imgproc.h, transforms.h included automatically.      │
│  Bare statements wrapped in main() automatically.            │
│  Crashes shown with GDB backtrace automatically.             │
└──────────────────────────────────────────────────────────────┘
"""


def _has_main(src):
    return bool(re.search(r'\bmain\s*\(', src))

def _has_include(src, header):
    bare = header.strip('<>"')
    return bool(re.search(r'#\s*include\s*[<"]' + re.escape(bare) + r'[>"]', src))

def _build_source(src):
    lines      = src.splitlines()
    pre_lines  = [l for l in lines if l.strip().startswith('#')]
    code_lines = [l for l in lines if not l.strip().startswith('#')]
    include_block = [inc for inc in AUTO_INCLUDES
                     if not _has_include(src, inc.split()[1])]
    include_block.extend(pre_lines)
    if _has_main(src):
        return "\n".join(include_block) + "\n\n" + src
    else:
        indented = "\n".join("    " + l if l.strip() else "" for l in code_lines)
        return (
            "\n".join(include_block) + "\n\n"
            + "int main(void) {\n"
            + indented
            + "\n    return 0;\n}\n"
        )


class CGDBKernel(Kernel):
    implementation         = "c_imgproc"
    implementation_version = "1.0"
    language               = "c"
    language_version       = "C11"
    language_info          = {"name": "c", "mimetype": "text/x-csrc", "file_extension": ".c"}
    banner = (
        "C (imgPrcsng) + GDB Kernel\n"
        f"  headers : {INCLUDE_DIR}\n"
        f"  library : {LIB_PATH}\n"
        "  bmp.h, imgproc.h, transforms.h included automatically\n"
        "  bare statements wrapped in main() automatically\n"
        "  type %help for debug commands"
    )

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._breakpoints = []
        self._watches     = []

    def _send(self, text, stream="stdout"):
        self.send_response(self.iopub_socket, "stream", {"name": stream, "text": text})

    def _ok(self):
        return {"status": "ok", "execution_count": self.execution_count,
                "payload": [], "user_expressions": {}}

    def _compile(self, src_path, binary_path):
        cmd = ["gcc"] + COMPILE_FLAGS + [src_path, "-o", binary_path] + LINK_FLAGS
        r = subprocess.run(cmd, capture_output=True, text=True)
        return r.returncode == 0, r.stderr

    def _format_gdb_output(self, raw):
        out = []
        re_frame   = re.compile(r'#(\d+)\s+(?:0x\w+ in )?(.+?) \(.*?\) at (.+):(\d+)')
        re_local   = re.compile(r'^(\w+) = (.+)$')
        re_bp_hit  = re.compile(r'Breakpoint \d+,')
        re_watchpt = re.compile(r'(Old|New) value of \`?(.+?)\`? =')
        re_signal  = re.compile(r'Program received signal (\w+)')
        re_exited  = re.compile(r'exited (normally|with code (\d+))')
        re_value   = re.compile(r'^\\\$\d+ = (.+)$')
        re_source  = re.compile(r'^(\d+)\s+(.+)$')
        shown_locals = False
        for line in raw.splitlines():
            s = line.strip()
            if not s: continue
            if any(s.startswith(p) for p in (
                "Reading symbols", "(gdb)", "[Inferior", "warning:", "GNU gdb", "No stack."
            )): continue
            m = re_signal.search(s)
            if m:
                explanation = {
                    "SIGSEGV": "Segfault — bad pointer or out-of-bounds index.",
                    "SIGABRT": "Abort — assert() failed or abort() called.",
                    "SIGFPE":  "Arithmetic error — e.g. division by zero.",
                    "SIGBUS":  "Bus error — misaligned memory access.",
                }.get(m.group(1), f"Signal {m.group(1)}.")
                out.append(f"\n\U0001f4a5  CRASH: {explanation}"); shown_locals = False; continue
            m = re_exited.search(s)
            if m:
                out.append("\n\u2705  Program finished successfully." if "normally" in m.group(1)
                           else f"\n\u26a0\ufe0f   Program exited with code {m.group(2)}."); continue
            if re_bp_hit.search(s):
                out.append("\n\U0001f4cd  Breakpoint hit:"); shown_locals = False; continue
            m = re_watchpt.search(s)
            if m:
                if m.group(1) == "Old":
                    out.append(f"\n\U0001f441\ufe0f   Variable '{m.group(2)}' changed:")
                    out.append(f"     Old: {s.split('=',1)[1].strip()}")
                else:
                    out.append(f"     New: {s.split('=',1)[1].strip()}")
                continue
            m = re_frame.match(s)
            if m:
                label = "  \u25b6  Here:" if m.group(1) == "0" else f"  #{m.group(1)}"
                out.append(f"{label}  {m.group(2)}()  \u2014  line {m.group(4)}"); continue
            m = re_value.match(s)
            if m: out.append(f"\n\U0001f50d  Value: {m.group(1)}"); continue
            if s == "No locals.": out.append("     (no local variables)"); continue
            m = re_source.match(s)
            if m and len(m.group(1)) <= 4:
                out.append(f"     line {m.group(1)}: {m.group(2)}"); continue
            m = re_local.match(s)
            if m and not s.startswith("#"):
                if not shown_locals: out.append("\n\U0001f4e6  Local variables:"); shown_locals = True
                out.append(f"     {m.group(1)} = {m.group(2)}"); continue
            out.append(s)
        return "\n".join(out) + "\n"

    def _run_plain(self, binary):
        return subprocess.run([binary], capture_output=True, text=True, timeout=15)

    def _run_gdb_batch(self, binary, print_exprs=None):
        args = ["gdb", "--batch", "--quiet"]
        for bp in self._breakpoints: args += ["-ex", f"break {bp}"]
        for w  in self._watches:     args += ["-ex", f"watch {w}"]
        args += ["-ex", "run"]
        if self._breakpoints or self._watches:
            args += ["-ex", "info locals", "-ex", "bt 5"]
        for expr in (print_exprs or []): args += ["-ex", f"print {expr}"]
        args += ["-ex", "bt", binary]
        r = subprocess.run(args, capture_output=True, text=True, timeout=15)
        return r.stdout + r.stderr

    def _apply_magic(self, magic_lines):
        print_exprs = []
        for line in magic_lines:
            parts = line.split(None, 1)
            cmd   = parts[0]
            arg   = parts[1].strip() if len(parts) > 1 else ""
            if cmd == "%help":
                self._send(HELP_TEXT)
            elif cmd == "%break":
                if not arg: self._send("\u26a0\ufe0f  %break needs a line number or function name.\n")
                else:
                    self._breakpoints.append(arg)
                    label = f"line {arg}" if arg.isdigit() else f"function '{arg}'"
                    self._send(f"\U0001f4cd  Breakpoint set at {label}  (total: {len(self._breakpoints)})\n")
            elif cmd == "%watch":
                if not arg: self._send("\u26a0\ufe0f  %watch needs a variable name.\n")
                else:
                    self._watches.append(arg)
                    self._send(f"\U0001f441\ufe0f   Watching '{arg}'  (total: {len(self._watches)})\n")
            elif cmd == "%print":
                if not arg: self._send("\u26a0\ufe0f  %print needs an expression.\n")
                else: print_exprs.append(arg)
            elif cmd == "%clear":
                self._breakpoints.clear(); self._watches.clear()
                self._send("\U0001f5d1\ufe0f   All breakpoints and watches cleared.\n")
            else:
                self._send(f"\u26a0\ufe0f  Unknown command: {cmd}  (try %help)\n")
        return print_exprs

    def do_execute(self, code, silent, store_history=True,
                   user_expressions=None, allow_stdin=False):
        if not GCC_AVAILABLE:
            self._send("\u274c  gcc not found.\n", "stderr")
            return self._ok()
        lines       = code.strip().splitlines()
        magic_lines = [l.strip() for l in lines if l.strip().startswith("%")]
        src_lines   = [l        for l in lines if not l.strip().startswith("%")]
        src         = "\n".join(src_lines).strip()
        if not src:
            self._apply_magic(magic_lines); return self._ok()
        print_exprs = self._apply_magic(magic_lines)
        full_src    = _build_source(src)
        with tempfile.NamedTemporaryFile(suffix=".c", delete=False, dir="/tmp", mode="w") as f:
            f.write(full_src); src_path = f.name
        binary_path = src_path.replace(".c", ".out")
        ok, err = self._compile(src_path, binary_path)
        if not ok:
            self._send("\u274c  Compilation failed:\n\n" + err, "stderr"); return self._ok()
        use_gdb = self._breakpoints or self._watches or print_exprs
        if use_gdb:
            if not GDB_AVAILABLE:
                self._send("\u26a0\ufe0f   GDB not installed \u2014 running without debugger.\n")
                self._send(self._run_plain(binary_path).stdout)
            else:
                self._send(self._format_gdb_output(
                    self._run_gdb_batch(binary_path, print_exprs)))
        else:
            result = self._run_plain(binary_path)
            if result.stdout: self._send(result.stdout)
            if result.stderr: self._send(result.stderr, "stderr")
            if result.returncode < 0:
                if GDB_AVAILABLE:
                    self._send("\n\U0001f4a5  Program crashed \u2014 running GDB\u2026\n")
                    self._send(self._format_gdb_output(self._run_gdb_batch(binary_path)))
                else:
                    self._send(f"\n\U0001f4a5  Crash (signal {-result.returncode}). "
                               "Install GDB for a backtrace.\n", "stderr")
        return self._ok()


if __name__ == "__main__":
    from ipykernel.kernelapp import IPKernelApp
    IPKernelApp.launch_instance(kernel_class=CGDBKernel)
EOF
    echo "  kernel.py written: $KERNEL_INSTALL_DIR/kernel.py"

    # ── cmake: build imgproc_lib only ────────────────────────────────────────
    # cmake no longer owns kernel file management — it only builds the lib.
    # We still pass PIPX_PYTHON so CMakeLists.txt doesn't warn, and so
    # VSCode's CMake Tools extension gets the right value via settings.json.
    echo "--- Configuring imgPrcsng ---"
    rm -rf "$IMGPROC_BUILD_DIR"
    cmake -S "$IMGPROC_SOURCE_DIR" -B "$IMGPROC_BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DPIPX_PYTHON="$PIPX_PYTHON" \
        -GNinja

    echo "--- Building imgproc_lib ---"
    cmake --build "$IMGPROC_BUILD_DIR" --target imgproc_lib

    if [[ ! -f "$IMGPROC_LIB" ]]; then
        echo "ERROR: $IMGPROC_LIB not found after build."
        return 1
    fi
    echo "  Built: $IMGPROC_LIB"
    echo "  c_imgproc kernel ready: $KERNEL_INSTALL_DIR"
fi

# ── 6. jupyterlab-git ─────────────────────────────────────────────────────────
pipx inject jupyterlab jupyterlab-git

# ── 7. VSCode configs ─────────────────────────────────────────────────────────
# settings.json is always overwritten so PIPX_PYTHON is never stale.
# launch.json and tasks.json are only written once — delete to regenerate.
if [[ -f "$IMGPROC_SOURCE_DIR/CMakeLists.txt" ]]; then
    VSCODE_DIR="$IMGPROC_SOURCE_DIR/.vscode"
    mkdir -p "$VSCODE_DIR"

    # Always overwrite settings.json — stale PIPX_PYTHON causes cmake warning
    cat > "$VSCODE_DIR/settings.json" <<EOF
{
  "cmake.configureArgs": [
    "-DPIPX_PYTHON=$PIPX_PYTHON"
  ],
  "cmake.buildDirectory": "$IMGPROC_BUILD_DIR",
  "C_Cpp.default.compileCommands": "$IMGPROC_BUILD_DIR/compile_commands.json"
}
EOF
    echo "  Written: $VSCODE_DIR/settings.json"

    LAUNCH_JSON="$VSCODE_DIR/launch.json"
    if [[ -f "$LAUNCH_JSON" ]]; then
        echo "  $LAUNCH_JSON exists — skipping (delete to regenerate)"
    else
cat > "$LAUNCH_JSON" <<EOF
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
      ],
      "sourceFileMap": { "$IMGPROC_SOURCE_DIR": "$IMGPROC_SOURCE_DIR" }
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
      ],
      "sourceFileMap": { "$IMGPROC_SOURCE_DIR": "$IMGPROC_SOURCE_DIR" }
    }
  ]
}
EOF
        echo "  Written: $LAUNCH_JSON"
    fi

    TASKS_JSON="$VSCODE_DIR/tasks.json"
    if [[ -f "$TASKS_JSON" ]]; then
        echo "  $TASKS_JSON exists — skipping (delete to regenerate)"
    else
cat > "$TASKS_JSON" <<EOF
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
      "label": "cmake: build imgproc_lib",
      "type": "shell",
      "command": "cmake --build $IMGPROC_BUILD_DIR --target imgproc_lib",
      "group": "build",
      "presentation": { "reveal": "silent", "panel": "shared" },
      "problemMatcher": "\$gcc"
    },
    {
      "label": "cmake: build current test",
      "type": "shell",
      "command": "gcc -g3 -O0 -std=c11 -I$IMGPROC_INCLUDE_DIR -I$IMGPROC_SRC_DIR \${file} $IMGPROC_LIB -lfftw3 -lm -o $IMGPROC_BUILD_DIR/_vscode_test",
      "group": "build",
      "presentation": { "reveal": "silent", "panel": "shared" },
      "problemMatcher": "\$gcc"
    }
  ]
}
EOF
        echo "  Written: $TASKS_JSON"
    fi
    echo "  VSCode configs ready."
fi

# ── 8. desktop entry ──────────────────────────────────────────────────────────
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
echo "  Desktop entry written."

# ── 9. verify ─────────────────────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════"
echo " Registered kernels"
echo "════════════════════════════════════════"
jupyter kernelspec list

echo ""
echo "kernel.json argv check:"
for spec_name in c c_imgproc; do
    spec_dir=$(jupyter kernelspec list --json \
        | python3 -c "
import sys, json
info = json.loads(sys.stdin.read()).get('kernelspecs', {}).get('$spec_name')
if info: print(info['resource_dir'])
" 2>/dev/null || true)
    if [[ -n "$spec_dir" && -f "$spec_dir/kernel.json" ]]; then
        argv0=$(python3 -c "
import json, pathlib
print(json.loads(pathlib.Path('$spec_dir/kernel.json').read_text())['argv'][0])
")
        argv1=$(python3 -c "
import json, pathlib
a = json.loads(pathlib.Path('$spec_dir/kernel.json').read_text())['argv']
print(a[1] if len(a) > 1 else '(none)')
")
        echo "  $spec_name  argv[0]: $argv0"
        echo "  $spec_name  argv[1]: $argv1"
    fi
done

echo ""
echo "Done. Start JupyterLab:  jupyter lab"
echo "      Or from the app menu: JupyterLab"
echo ""
echo "Kernels:"
echo "  • C (imgPrcsng) — auto-includes bmp.h/imgproc.h/transforms.h,"
echo "                    auto-wraps bare statements in main(),"
echo "                    GDB magic: %break, %watch, %print, %clear"
echo "  • C             — generic C kernel for standalone cells"
echo ""
echo "VSCode debugging (F5):"
echo "  • Debug imgPrcsng          — full executable under gdb"
echo "  • Debug imgPrcsng (attach) — attach to running process by PID"
echo "  • Debug current test       — open any .c file and press F5"
echo ""
echo "To use a different project path:"
echo "  IMGPROC_SOURCE_DIR=~/other/path bash Jupyter_c_kernels.sh"

} # end main()

# ── entry point ───────────────────────────────────────────────────────────────
if ! main "$@"; then
    echo ""
    echo "Installation failed — see the error above."
    exit 1
fi
exit 0
