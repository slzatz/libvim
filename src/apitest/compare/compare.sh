#!/bin/bash
# compare.sh [--keep] <file of cases>...
#
# Run each file of cases through libvim (probe.exe) and through the system's
# vim (vim-oracle.sh), and print the cases that end differently: the cursor
# and the first six lines of the buffer. See README.md for what the cases
# are, and for the differences that are not libvim's.
#
# Both run in a temporary directory, with HOME set to another, so that a
# case which writes a file (zg, :w, :mkspell) leaves nothing behind. In a
# case file @TMP@ stands for that directory; the files in fixtures/ are
# copied there.
#
# --keep leaves the temporary directory, with the full output of both, and
# prints where it is.

here="$(cd "$(dirname "$0")" && pwd)"
runtime="$(cd "$here/../../../runtime" && pwd)"

keep=0
if [ "${1:-}" = "--keep" ]; then
	keep=1
	shift
fi
[ $# -ge 1 ] || { echo "usage: compare.sh [--keep] <file of cases>..." >&2; exit 2; }
[ -x "$here/probe.exe" ] || { echo "error: probe.exe not built; run $here/build.sh" >&2; exit 1; }

work="$(mktemp -d)"
[ "$keep" -eq 1 ] || trap 'rm -rf "$work"' EXIT
mkdir -p "$work/home/.vim/spell" "$work/cwd"
# ^ zg with 'spellfile' unset writes to the first spell directory on
#   'runtimepath'; without this one it would be the repository's runtime/spell
cp "$here"/fixtures/* "$work/cwd/"

total=0
differ=0
for file in "$@"; do
	[ -r "$file" ] || { echo "error: cannot read $file" >&2; exit 1; }
	name="$(basename "$file" .txt)"
	sed "s#@TMP@#$work/cwd#g" "$file" > "$work/$name.cases"

	(cd "$work/cwd" && HOME="$work/home" LIBVIM_RUNTIME="$runtime" \
		"$here/probe.exe" "$work/$name.cases" > "$work/$name.libvim" 2>&1)
	(cd "$work/cwd" && HOME="$work/home" \
		"$here/vim-oracle.sh" "$work/$name.cases" > "$work/$name.vim")

	# probe prints several lines for a case, vim-oracle.sh one, in the same
	# order. LC_ALL=C: what they print is not always valid UTF-8.
	LC_ALL=C awk -v vimfile="$work/$name.vim" -v name="$name" '
		function flush() {
			if (keys == "") return
			total++
			if ((getline theirs < vimfile) <= 0) theirs = "! missing"
			if (ours != theirs) {
				differ++
				print "[" name "] " keys
				print "  libvim " ours
				print "  vim    " theirs
				if (said != "") printf "%s", said
				print ""
			}
			keys = ""; ours = "! nothing printed"; said = ""
		}
		BEGIN { ours = "! nothing printed" }
		/^CASE / { flush(); keys = substr($0, 6); next }
		/^  = /  { ours = substr($0, 3); next }
		/^  ! /  { ours = substr($0, 3); next }
		/^  msg/ { said = said "  libvim said " substr($0, 3) "\n"; next }
		END { flush(); print total + 0, differ + 0 > "/dev/stderr" }
	' "$work/$name.libvim" 2> "$work/$name.count"

	read -r t d < "$work/$name.count"
	total=$((total + t))
	differ=$((differ + d))
done

echo "$total cases, $differ differ"
[ "$keep" -eq 1 ] && echo "kept: $work"
[ "$differ" -eq 0 ]
