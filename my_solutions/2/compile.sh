#!/usr/bin/env bash
#
# compile.sh -- thin wrapper around nvcc for the leetgpu trials.
#
# Why this exists:
#   Arch ships GCC 16 as the default host compiler, but CUDA 13.2's nvcc only
#   officially supports up to GCC 14. Without -ccbin, nvcc pulls in GCC 16's
#   libstdc++ headers and dies in <type_traits>. We pin g++-14 here so every
#   build uses the right host toolchain without typing the flag each time.
#
# Usage:
#   ./compile.sh --compile <source.cu>
#
# Example:
#   ./compile.sh --compile humpty_dumpty.cu   # produces ./humpty_dumpty
#
# Behavior:
#   - Errors out if the source file doesn't exist.
#   - Always deletes any stale binary first so we never run cached code.

# Strict mode: fail on unset vars, errors, and pipeline failures.
set -euo pipefail

# ---- arg parsing -----------------------------------------------------------

usage() {
    echo "Usage: $0 --compile <source.cu>" >&2
    exit 1
}

# Expect exactly: --compile <file>
if [[ $# -ne 2 || "$1" != "--compile" ]]; then
    usage
fi

SRC="$2"

# ---- safety checks ---------------------------------------------------------

# Source file must actually exist.
if [[ ! -f "$SRC" ]]; then
    echo "Error: source file '$SRC' does not exist." >&2
    exit 1
fi

# Derive binary name by stripping the .cu suffix.
OUT="${SRC%.cu}"

# If stripping didn't change anything, the user didn't pass a .cu file.
if [[ "$OUT" == "$SRC" ]]; then
    echo "Error: '$SRC' is not a .cu file." >&2
    exit 1
fi

# Resolve the pinned host compiler. command -v returns nonzero if missing; we
# capture it with || true so set -e doesn't abort, then check explicitly to
# give a friendlier message.
HOST_CXX="$(command -v g++-14 || true)"
if [[ -z "$HOST_CXX" ]]; then
    echo "Error: g++-14 not found on PATH. Install gcc14 (pacman -S gcc14)." >&2
    exit 1
fi

# ---- build -----------------------------------------------------------------

# Always start from a clean slate -- guarantees the produced binary reflects
# THIS invocation, never a leftover from a previous failed build.
rm -f "$OUT"

# The actual compile. -ccbin picks the host compiler nvcc shells out to for
# the CPU-side code in our .cu file.
nvcc \
    -ccbin "$HOST_CXX" \
    "$SRC" \
    -o "$OUT"

echo "Built: $OUT"
