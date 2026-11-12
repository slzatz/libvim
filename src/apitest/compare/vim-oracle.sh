#!/bin/bash
# vim-oracle.sh <file of cases>
#
# Run each case of a probe file in the system's vim and print, one line per
# case, what probe prints on its "=" line: the cursor and the first lines of
# the buffer. A case vim does not finish gives "! no result".
#
# vim is started fresh for every case (-u NONE -i NONE, silent Ex mode), with
# the buffer probe uses, and is given the keys with feedkeys(keys, 'tx'). A
# {EX} case is run with :execute. {TO} means nothing here: vim gives up on a
# half-typed mapping by itself.
#
# VIM names the vim to use (default: vim); ORACLE_TIMEOUT the seconds a case
# may take (default 10) before vim is killed -- one that waits for a key
# never ends by itself.

vim="${VIM:-vim}"
limit="${ORACLE_TIMEOUT:-10}"

[ $# -eq 1 ] && [ -r "$1" ] || { echo "usage: vim-oracle.sh <file of cases>" >&2; exit 2; }
command -v "$vim" >/dev/null 2>&1 || { echo "error: $vim not found" >&2; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

cat > "$work/case.vim" <<'VIM'
set nocompatible noswapfile viminfo= shortmess+=I
set columns=80 lines=42
enew!
call setline(1, ['alpha one', 'bravo two', 'charlie three', 'delta four', 'echo five'])
call cursor(1, 1)
let s:line = readfile(g:probe_case)[0]
try
  if s:line =~# '^{EX}'
    execute s:line[4:]
  else
    let s:line = substitute(s:line, '^{KEY}', '', '')
    let s:line = substitute(s:line, '{TO}', '', 'g')
    let s:keys = substitute(s:line, '<\([A-Za-z0-9_-]\+\)>', '\=eval("\"\\<" . submatch(1) . ">\"")', 'g')
    call feedkeys(s:keys, 'tx')
  endif
catch
endtry
let s:out = printf('= cursor=%d,%d n=%d |', line('.'), col('.') - 1, line('$'))
for s:i in range(1, min([line('$'), 6]))
  let s:out .= getline(s:i) . '|'
endfor
call writefile([s:out], g:probe_result)
qa!
VIM

while IFS= read -r line || [ -n "$line" ]; do
	[ -z "$line" ] && continue
	case "$line" in \#*) continue ;; esac

	printf '%s\n' "$line" > "$work/case.txt"
	rm -f "$work/result.txt"

	"$vim" -Nu NONE -i NONE -n -es \
		--cmd "let g:probe_case='$work/case.txt' | let g:probe_result='$work/result.txt'" \
		-S "$work/case.vim" </dev/null >/dev/null 2>&1 &
	pid=$!
	( sleep "$limit"; kill -9 "$pid" ) >/dev/null 2>&1 &
	watchdog=$!
	wait "$pid" 2>/dev/null
	pkill -P "$watchdog" 2>/dev/null
	kill "$watchdog" 2>/dev/null
	wait "$watchdog" 2>/dev/null

	if [ -f "$work/result.txt" ]; then
		cat "$work/result.txt"
	else
		echo "! no result"
	fi
done < "$1"
