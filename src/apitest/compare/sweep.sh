#!/bin/bash
# sweep.sh [--all] [--keep] <file of cases>...
#
# Run each file of cases through libvim alone (probe.exe) and print the
# cases that did not come back: "! HANG", "! CRASH signal <n>", "! EXIT <n>",
# "! exit() was called". For the commands with nothing in vim to compare:
# hundreds of ex commands, options and functions, run to see that each
# returns.
#
# As compare.sh: a temporary directory to run in, HOME set to another,
# @TMP@ in a case file for the first, and fixtures/ copied there.
#
# --all prints every case, with what it said and left; --keep leaves the
# temporary directory and prints where it is.

here="$(cd "$(dirname "$0")" && pwd)"
runtime="$(cd "$here/../../../runtime" && pwd)"

all=0
keep=0
while [ "${1:-}" = "--all" ] || [ "${1:-}" = "--keep" ]; do
	[ "$1" = "--all" ] && all=1
	[ "$1" = "--keep" ] && keep=1
	shift
done
[ $# -ge 1 ] || { echo "usage: sweep.sh [--all] [--keep] <file of cases>..." >&2; exit 2; }
[ -x "$here/probe.exe" ] || { echo "error: probe.exe not built; run $here/build.sh" >&2; exit 1; }

work="$(mktemp -d)"
[ "$keep" -eq 1 ] || trap 'rm -rf "$work"' EXIT
mkdir -p "$work/home/.vim/spell" "$work/cwd"
# ^ zg with 'spellfile' unset writes to the first spell directory on
#   'runtimepath'; without this one it would be the repository's runtime/spell
cp "$here"/fixtures/* "$work/cwd/"

total=0
bad=0
for file in "$@"; do
	[ -r "$file" ] || { echo "error: cannot read $file" >&2; exit 1; }
	name="$(basename "$file" .txt)"
	sed "s#@TMP@#$work/cwd#g" "$file" > "$work/$name.cases"

	(cd "$work/cwd" && HOME="$work/home" LIBVIM_RUNTIME="$runtime" \
		"$here/probe.exe" "$work/$name.cases" > "$work/$name.libvim" 2>&1 </dev/null)

	if [ "$all" -eq 1 ]; then
		cat "$work/$name.libvim"
	else
		# LC_ALL=C: what libvim prints is not always valid UTF-8
		LC_ALL=C awk -v name="$name" '
			/^CASE / { keys = substr($0, 6); shown = 0; next }
			/^  ! /  { if (!shown) print "[" name "] " keys; shown = 1; print "  " substr($0, 3) }
		' "$work/$name.libvim"
	fi
	total=$((total + $(LC_ALL=C grep -a -c '^CASE ' "$work/$name.libvim")))
	bad=$((bad + $(LC_ALL=C awk '/^CASE /{ if (b) n++; b = 0 } /^  ! /{ b = 1 } END { if (b) n++; print n + 0 }' "$work/$name.libvim")))
done

echo "$total cases, $bad did not come back"
[ "$keep" -eq 1 ] && echo "kept: $work"
[ "$bad" -eq 0 ]
