#!/usr/bin/env bash
#
# Build / run helper for my_solutions/1/final_solution.c
#
# Usage:
#   ./final_solution.sh --compile   compile the C source into /tmp/leetcode_c_1_final_solution
#   ./final_solution.sh --run       execute the compiled binary
#

set -euo pipefail

# resolve paths relative to this script, not the caller's cwd
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${SCRIPT_DIR}/final_solution.c"
BIN="/tmp/leetcode_c_1_final_solution"

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
