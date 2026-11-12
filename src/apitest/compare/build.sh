#!/bin/bash
# Build probe.exe, trace.exe and api_edges.exe against src/libvim.a, with the
# compiler, flags and libraries the API tests are built with.
#
# libvim.a must exist: run ./build-macos.sh or ./build-linux.sh first.
set -e

here="$(cd "$(dirname "$0")" && pwd)"
src="$(cd "$here/../.." && pwd)"

[ -f "$src/libvim.a" ] || { echo "error: $src/libvim.a not found; build the library first" >&2; exit 1; }

extra=""
if [ "$(uname)" = "Darwin" ] && command -v brew >/dev/null 2>&1; then
	# as build-macos.sh: the Makefile does not add gettext's lib directory
	extra="-L$(brew --prefix gettext)/lib"
fi

cd "$src"
make -f Makefile -f apitest/compare/rules.mk compare-tools EXTRA_LIBS="$extra"
echo "Built: $here/probe.exe, trace.exe, api_edges.exe"
