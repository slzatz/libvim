# Comparing libvim with vim

The API tests in `src/apitest/` check what someone thought to check. These
programs look for what nobody thought of: they give libvim a great many key
sequences and commands and see whether it does what vim does, and whether it
comes back at all. A review with them on 2026-11-12 found the five bugs fixed
in `37762a4b1` and most of what is in [TODO.md](../../../TODO.md).

They are not part of the test suite and nothing runs them automatically: the
comparison needs a `vim` on the machine, and its result is a list to read,
not a pass or a fail.

## Use

```bash
./build-macos.sh                    # or ./build-linux.sh: libvim.a
cd src/apitest/compare
./build.sh                          # probe.exe, trace.exe, api_edges.exe

./compare.sh cases/normal.txt       # against vim: prints the cases that differ
./sweep.sh cases/sweep-ex.txt       # libvim alone: prints the cases that did not return
./api_edges.exe                     # the C API with arguments it does not expect
./trace.exe d v j                   # one sequence, a step at a time
```

`build.sh` uses the compiler, flags and libraries of the API tests
(`rules.mk`, read after `src/Makefile`), so the programs build under
`-Werror` like everything else. Build them again after rebuilding `libvim.a`.

## What is here

| | |
|---|---|
| `probe.c` | Runs a file of cases through libvim, each in a child process from the same five-line buffer, and prints the messages, the mode, the cursor and the first six lines. A case that hangs, crashes or exits is reported and the rest go on. |
| `vim-oracle.sh` | Runs the same file through the system's vim — started fresh for each case, `-u NONE -i NONE`, keys given with `feedkeys(keys, 'tx')` — and prints the cursor and lines in probe's format. |
| `compare.sh` | Runs both and prints the cases where they end differently, with what libvim said. Exit status 0 if none do. |
| `sweep.sh` | Runs probe alone and prints the cases that did not come back. `--all` prints everything. |
| `trace.c` | One sequence, the state after every step. For a case the other two disagree on. |
| `api_edges.c` | Calls the C API with bad line ranges, a buffer that is not current, a cursor the buffer has shrunk under. Prints; asserts nothing. |
| `cases/` | The cases. `fixtures/` are files they read. |

`compare.sh` and `sweep.sh` run everything in a temporary directory with
`HOME` set to another, so a case that writes a file (`zg`, `:w`, `:mkspell`,
`:mkview`) leaves nothing in the repository or the home directory. In a case
file `@TMP@` is that directory. `--keep` keeps it, with the full output of
both sides.

**They are not run in restricted mode**: `:!ls`, `K` and `job_start()` in the
cases do start a program.

## A case

One line. It is typed as a user would type it: `<name>` (`<esc>`, `<c-r>`,
`<lt>` for a `<`) goes to `vimKey()`, everything else to `vimInput()` a
character at a time.

| | |
|---|---|
| `{EX}cmd` at the start | the rest is given to `vimExecute()`, not typed |
| `{KEY}text` at the start | the rest is given to `vimKey()` in one call — for a key name that is not well formed, and for a case that begins with `#` |
| `{TO}` anywhere | `vimInputTimeout()`: `'timeoutlen'` has passed |
| `#` at the start | a comment |

The buffer is always

```
alpha one
bravo two
charlie three
delta four
echo five
```

with the cursor on the first character and mappings enabled.

To add cases, add lines. A new file needs nothing else.

## The case files

For `compare.sh`, with the number that differed on 2026-11-12 (macOS, vim
9.1, after `37762a4b1`):

| | cases | differ |
|---|---|---|
| `cases/normal.txt` | 337 | 23 |
| `cases/insert.txt` | 216 | 58 |
| `cases/cmdline.txt` | 215 | 21 |
| `cases/ex.txt` | 364 | 33 |
| `cases/ctrl-o-forced-motion.txt` | 155 | 2 |

For `sweep.sh`:

| | cases | did not come back |
|---|---|---|
| `cases/sweep-ex.txt` | 752 | 1 (`:cq`) |
| `cases/sweep-functions.txt` | 326 | 0 |
| `cases/sweep-keys.txt` | 201 | 3 (`<PasteStart>`) |

A number that goes up after a change is the thing to look at. So is a case
that no longer differs.

## Reading the differences

Most of what differs is not a bug in libvim. Before believing one, run it in
`trace.exe` and in a real vim.

**The comparison's own.** vim is run without a terminal and is given its keys
by `feedkeys()`, and it is a later vim than libvim's 8.1.

- A case that **ends in Insert mode**, or part-way through a command, differs
  in the cursor column: `feedkeys(..., 'x')` ends it with `<Esc>`, libvim is
  left where it was. End the case with `<esc>` if the column matters.
- **`<Esc>` on a command line** given by `feedkeys()` *runs* it. `/two<esc>D`
  and `i<c-o>:<esc>X<esc>` differ for that alone; libvim is right.
- Silent Ex mode has **no `'incsearch'`**, so CTRL-G, CTRL-T and CTRL-L on a
  search line are characters there.
- **vim's undo history includes the lines that set the buffer up**: `u`,
  `:undo`, `:earlier`, `g;`, `'.` on an unchanged buffer go back to an empty
  one in vim.
- libvim starts with an empty buffer before the one with the five lines:
  `<c-o>` and `<c-^>` as a first command go to it.
- vim has the system clipboard (`"*p`), its help files (`<f1>`), and on
  macOS no `'revins'` or `'arabic'`.
- vim 9.1 has `<Cmd>` in a mapping; libvim's 8.1 does not.
- `:42` in a buffer of five lines goes to the last line in libvim, as it
  does typed in vim; vim in silent Ex mode stays where it is.
- A case that makes vim quit (`ZQ`) gives `! no result`.
- The two mappings in `ex.txt` that are meant never to end take the whole
  timeout in libvim, and vim gets out of them.

**libvim's, on purpose.** Scrolling is the embedder's (CTRL-D, CTRL-F,
CTRL-E, `<PageDown>` do not move the cursor without a scroll callback), as
are windows (`CTRL-W n`). Completion (CTRL-N, CTRL-P, CTRL-X) and digraphs
(CTRL-K) are not compiled in. `:s` with the `c` flag is refused, and
`input()` returns nothing.

**libvim's, and in TODO.md.** `gq`, `gw` and `!` with no format callback;
`@:` and command-line history; `feedkeys()`; `gH`; `0 CTRL-D`;
`'smartindent'`; `'formatoptions'` with `a`; `<F1>` and `<Insert>` in Insert
mode; `:append` and `:function` typed; `:redir`.

## With AddressSanitizer

A bad read inside libvim shows only if `libvim.a` itself is instrumented;
TODO.md (entry 13) has the `make` line. Build that library somewhere else — a
copy of `src/` — and link these programs against it by hand: `build.sh`
links the one in `src/`.
