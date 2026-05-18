#!/usr/bin/env bash
#
# Build / run helper for my_solutions/1/nested_loop.c
#
# Usage:
#   ./nested_loop.sh --compile   compile the C source into /tmp/leetcode_c_1_nested_loop
#   ./nested_loop.sh --run       execute the compiled binary
#

set -euo pipefail

# resolve paths relative to this script, not the caller's cwd
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${SCRIPT_DIR}/nested_loop.c"
BIN="/tmp/leetcode_c_1_nested_loop"

usage() {
	echo "Usage: $0 --compile | --run" >&2
	exit 1
}

if [[ $# -ne 1 ]]; then
	usage
fi

case "$1" in
	--compile)
		gcc -Wall -Wextra -O2 -g "${SRC}" -o "${BIN}"
		echo "Compiled: ${BIN}"
		;;
	--run)
		if [[ ! -x "${BIN}" ]]; then
			echo "Please run --compile first"
			exit 1
		fi
		"${BIN}"
		;;
	*)
		usage
		;;
esac
