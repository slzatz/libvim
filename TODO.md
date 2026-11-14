# Not done yet

What a review on 2026-11-12 found and did not fix. Five things from the same
review were fixed that day (`37762a4b1`: `:normal`, CTRL-O, `v` after an
operator, CTRL-V on the command line); PATCHES.md has those. Everything here
was reproduced on the library as it stood before that commit, on macOS, and
nothing in the commit touches it. None of it is started.

Each entry says what happens, why where that is known, and what the fix
probably is. "Tried" means the change was made in a scratch copy and worked;
otherwise the fix is a proposal. When one is done it wants what every change
here wants: a section in PATCHES.md, a test in `src/apitest/`, and its entry
taken out of this file.

Roughly in the order worth doing.

## Wrong results

### 1. Search highlights follow the last pattern compiled, not the last search

`vimSearchGetHighlights()` and `vimSearchGetPattern()` (`libvim.c`) read
`get_search_pat()` (`search.c`), which returns `mr_pattern`: whatever
`search_regcomp()` compiled most recently. That is not vim's last search
pattern (`last_search_pat()`, which is `@/`).

- `:keeppatterns s/bravo/X/` — the highlights become those of `bravo`, though
  `@/` is unchanged. They come back at the next `n`.
- `:call search('three')`, `searchpos()` — the highlights follow the
  function's pattern.
- `:let @/ = 'charlie'` and `:let @/ = ''` — ignored until a search is run.
- With `'ignorecase'` and `'smartcase'`, `*` on `Alpha`: `n` visits `Alpha`,
  `alpha` and `ALPHA` (`*` does not use `'smartcase'`), and one match is
  reported, because the pattern is compiled again with `'smartcase'` applied.

Proposed: `vimSearchGetPattern()` returns `last_search_pat()`; the highlights
call `searchit()` with an empty pattern and `RE_LAST`, which makes
`search_regcomp()` use `spats[last_idx]` with the `no_scs` and `magic` that
were recorded with it — what vim's own `last_pat_prog()` does for
`'hlsearch'`. vimango calls both functions (`vim/search.go`). Tests belong in
`apitest/search_highlights.c`.

### 2. `vimBufferSetLines()` does not look after what the buffer has

It appends and deletes lines and nothing else. vim's own `setbufline()` and
`deletebufline()` save for undo, adjust marks and check the cursor.

- **The cursor is left where it was.** Replace five lines with one while the
  cursor is on line 5: the next command gives `E315: ml_get: invalid lnum: 5`
  (then "cannot yank; delete anyway", `E470`). In Insert mode it is a read
  past the end of a block — `utf_head_off()` from `getvcol()` from
  `curs_columns()` in `state_edit_execute()`; AddressSanitizer reports a
  heap-buffer-overflow. Proposed: `check_cursor()` for every window showing
  the buffer.
- **Marks are not adjusted**: `'a` afterwards gives `E19: Mark has invalid
  line number`. Proposed: `mark_adjust()`, or `deleted_lines_mark()` /
  `appended_lines_mark()`.
- **Nothing is saved for undo, and the undo that exists goes stale**: `x`,
  then `vimBufferSetLines()`, then `u` puts the old line back over the new
  text; `.` after it gave `E440: undo line missing`. This one needs a
  decision first. Saving for undo would let `u`, typed after a note is
  loaded, empty the buffer, which an embedder loading text does not want. So:
  clear the undo history, or make undo opt-in, and say which in `libvim.h`.
- **Arguments are not checked.** A negative `start` corrupts the buffer (a
  line pointer that is not a string). `end < start` inserts and deletes
  nothing. `start` past the end does nothing but still raises the changed
  tick and sets `b_changed`.
- **A buffer that has never been current takes nothing.** After
  `vimBufferNew()` without `vimBufferSetCurrent()` the memline is not open;
  the lines are dropped without a word, and the tick is raised.
- A line containing `\n` is stored as one line with a NUL in it, as vim
  stores a NUL read from a file. Worth a sentence in `libvim.h`.

### 3. CTRL-V, digits, then another key: the key is lost

`CTRL-V 6 5 x` gives `A`; vim gives `Ax`. In Insert mode and on the command
line. The literal state (`state_insert_literal.c`) returns
`COMPLETED_UNHANDLED` with the key that ended it, and the caller replaces
that key with the character. When the key was not part of the number it
should be handled as a key of its own afterwards — vim's `get_literal()`
does `vungetc(nc)`. The state knows which case it is at cleanup (`i > 0` and
`nc != 0`); it has no way to say so yet.

## Crash, exit, hang

### 4. `<PasteStart>` crashes

`vimKey("<PasteStart>")` in Normal, Insert or Command-line mode, or the key
on the right-hand side of a mapping, is a segfault: `bracketed_paste()`
(`edit.c`) takes `STRLEN()` of `find_termcode("PE")`, which is NULL, and
would then read keys from a terminal libvim does not have. In Visual mode it
deletes the selection and starts Insert mode. vimango pastes by its own
means and says so in `paste.go`. Proposed: make the key do nothing.

### 5. `:cq` ends the process

`ex_cquit()` (`ex_docmd.c`) calls `getout(1)`, which is `exit(1)`. The quit
callback is not called. `:q`, `:qa` and `:qa!` go to the callback. Reachable
by `:execute 'cq'` whatever the embedder filters on its command line.
Proposed: call the callback as a forced quit, or refuse.

### 6. Nothing can stop a command that does not end

`:while 1 | endwhile`. `:nmap x xx` then `x` (vim loops on this too, until
CTRL-C). `vimInput()` and `vimExecute()` do not return, and libvim has no
CTRL-C: a key cannot arrive during a call.

Setting vim's `got_int` from outside (a signal handler, another thread) does
stop `:while`. But nothing clears it afterwards — vim's `main_loop()` does,
every time round — so `vgetorpeek()` then answers CTRL-C to every request
without end: the next `vimInput()` never returns, and the message callback
is sent "Type :qa! and press <Enter> to abandon all changes and exit Vim"
as fast as it can take them.

Proposed: `vimInterrupt()`, which sets `got_int` and nothing else, so that it
may be called from a signal handler; and `got_int` cleared where a call into
libvim starts (`vimInputCore()`, `vimExecuteLines()`, `vimInputTimeout()`).
`vimInit()` installs no signal handlers (checked), so nothing else sets it
behind the embedder's back. A test: an alarm that calls `vimInterrupt()`
during `:while 1`, and a key that works afterwards.

### 7. Smaller

- `vimWindowSetWidth(100000)` then `vimWindowSetHeight(100000)` does not
  return in any reasonable time: `screenalloc()` for `Rows * Columns` cells.
  Clamp, or refuse.
- `:sh` and `:shell`, outside restricted mode, start an interactive shell
  and wait for it. Restricted mode refuses them.

## Missing

### 8. `gq`, `gw`, `=` and `!` do nothing unless the embedder formats

`do_pending_operator()` (`normal.c`) hands `OP_FORMAT`, `OP_FORMAT2`,
`OP_INDENT` and `OP_FILTER` to `formatCallback` and does nothing when there
is none. vimango sets none, so `gqap` in a note is silent. vim's formatter is
still in the tree (`op_format()`, `format_lines()` in `ops.c`), and
`'textwidth'` wrapping while typing already uses it.

Tried: `else op_format(oap, FALSE);` for `OP_FORMAT` and
`else op_format(oap, TRUE);` for `OP_FORMAT2`. `gqap`, `gqG`, `gwG`, undo and
`.` then work. Not tried: `=` falling back to `op_reindent()` and `!` to
`op_colon()` as in vim. (`!!` and `!{motion}` open no command line now; the
ex command they would fill in, `:{range}!cmd`, works.)

It changes what an embedder without the callback gets — something where
there was nothing. Decide whether that needs to be opt-in.

### 9. A typed command line is not remembered

`returncmd` in `state_cmdline_execute()` (`ex_getln.c`) leaves out what
vim's `getcmdline()` does at the same label: `add_to_history()`, and
`new_last_cmdline`. And `do_cmdline()` makes `new_last_cmdline` the last
command line only when its lines come from `getexline()`.

- `:` then `<Up>` or CTRL-P recalls nothing, and `:history` is empty. `/`
  then `<Up>` recalls nothing either.
- `@:` and `":p` give `E30: No previous command line`.

vimango takes `<CR>` itself and calls `histadd()`, which gives it a history.
Nothing sets the last command line, so `@:` has nothing to repeat.

### 10. `feedkeys()` does nothing

The whole body of `f_feedkeys()` (`evalfunc.c`) is commented out under
"TODO: Externalize feedkeys", so the keys are dropped whatever the flags.
Without `x` the keys belong at the end of the typeahead buffer, where the
state machine will find them — but a call from `vimExecute()` has no state
machine running after it, so something has to run them (`sm_execute()` with
no new keys would). With `x` they are to be run before `feedkeys()` returns,
which is what `sm_execute_normal()` does for `:normal`.

`:source! file` (keys from a file) does nothing either: that is the
`TODO: Wire-up to sm_execute_normal` in `openscript()` (`getchar.c`).

### 11. `g0`, `g^`, `gm` and `g$` do not ask the embedder

Added 2026-11-14, not from the review. `gj`, `gk`, `H`, `M` and `L` ask the
embedder where its screen lines are (`vimSetCursorMoveScreenPositionCallback()`,
`vimSetCursorMoveScreenLineCallback()`), because libvim's window matches
nothing the embedder draws. The motions to a place *in* a screen line were
left as vim has them in `nv_g_cmd()` (`normal.c`): `g0` (`g<Home>`), `g^`,
`gm` and `g$` (`g<End>`) are worked out from libvim's own window -- its
`w_width`, 80 unless `vimWindowSetWidth()` is called, less `win_col_off()`
for `'number'`, `'relativenumber'` and the fold and sign columns.

- `'wrap'` is off by default in libvim (upstream's default in `option.c`;
  vim's is on). They then go to the edges of libvim's horizontally scrolled
  view. On a line of 200 columns, at width 80: `150|g0` goes to column 109,
  `150|g$` to 188. On a line shorter than the width they are `0`, `^` and
  `$`, and `gm` goes to column 40 (or the end of the line) whatever width
  the embedder draws. A count does nothing, as in vim with `'nowrap'`.
  vimango sets neither the width nor `'wrap'`, so this is what it gets.
- With `'wrap'` on they wrap the line at any character, at that width: from
  column 100, `g0` goes to 80, `gm` to 120, `g$` to 159. An embedder that
  wraps at word boundaries, as vimango does, has screen lines that start and
  end elsewhere. `g$` with a count above 1 does ask the embedder, through
  `nv_screengo()`, as `gj` does.

Found by reading the code, and confirmed with a program against the
library. Proposed: a third callback, opt-in like the other two -- "the
first and last byte of the screen line holding this position" -- from which
`g0`, `g^` (first non-blank from there), `gm` (the middle, by virtual
column) and `g$` follow; with no callback set, vim's arithmetic as now.
vimango would answer from `wrapRow()`, as `ScreenPosition()` does
(`internal/editor/screen_motion.go`). Tests belong in
`apitest/motion_screen.c`, with the ones for `gj`, `gk`, `H`, `M` and `L`.

### 12. Smaller differences from vim

- `vimExecute("startinsert")` starts Insert mode only after the next Normal
  mode command. Typed, or in a mapping, it works since `37762a4b1`.
- `gh`, `gH` and `g CTRL-H` do not start Select mode. `v CTRL-G` does.
- `0 CTRL-D` and `^ CTRL-D` in Insert mode remove the indent and leave the
  `0` or `^` in the text (`ins_shift()` in `edit.c` tests the character
  before the cursor against `lastc`).
- `'smartindent'` and `'cindent'` do not indent the line after a `{`.
  `'autoindent'` works. Possibly meant for `vimSetAutoIndentCallback()`.
- `'formatoptions'` with `a`: a space is lost when a line is rewrapped while
  typing. `:set tw=10 fo+=a`, then `A` and `two three four five six` at the
  end of `alpha one`, ends with a line `fivesix`.
- `<F1>` in Insert mode leaves Insert mode (and says the help file is not
  found); the key typed next is a Normal mode command.
- `<Insert>` in Insert mode does not switch to Replace mode.
- `[I` says nothing, and `]I`, `[D` and `]D` will be the same when they
  have something to list. What they write is collected only while
  `do_cmdline()` runs (see "Listings" in CLAUDE.md); `:ilist` works.
- `:redir` records `:echo 'hi'` as `hi`; vim records `\nhi`. A script that
  drops the first character of the redirected text drops the `h`.
- `:append`, `:insert` and `:change`, and `:function`, `:if` and `:while`
  typed on their own line, read no further lines (see "Keys that wait for
  another key"). The lines typed next are then Normal mode commands.
  `vimExecuteLines()` is the way to give several lines; an error would be
  better than silence. vimango refuses all of these.
- CTRL-\ CTRL-N and CTRL-\ CTRL-G on a command line do not leave it, and
  the character typed next goes onto the line. "Known gaps" in CLAUDE.md
  has the Insert-mode half of this.
- CTRL-X in Insert mode is ignored (`FEAT_INS_EXPAND` is off), and the key
  after it does what it does alone: CTRL-X CTRL-U deletes what was typed.
- `:let @+ = 'x'` and `:let @* = 'x'` store nothing without a clipboard
  callback; `"+p` gives `E353`.

## Tests and tooling

### 13. The tests do not instrument libvim

The test rule compiles the *test file* with `-fsanitize=address
-fsanitize=undefined` and links the ordinary `libvim.a`, so a bad read
inside libvim is seen only if it happens to pass through an intercepted
libc call. The first bullet of entry 2 was found only with the library
instrumented.

An instrumented library builds as it is:

```bash
cd src
make libvim.a SANITIZER_CFLAGS="-g -O1 -fsanitize=address \
  -fno-omit-frame-pointer -Wno-deprecated-declarations"
```

The last flag is needed because AddressSanitizer turns `_FORTIFY_SOURCE`
off, and the macOS SDK then reports `sprintf()` (`buffer.c` and others) as
deprecated, under `-Werror`. All 67 tests passed against such a library on
2026-11-12, with no report. Proposed: an `--asan` option for the build
scripts that builds into a second objects directory and runs the suite
against it. UndefinedBehaviorSanitizer on the library was not tried; vim's
older code will have a good deal to say.

### 14. The fork has no CI

`azure-pipelines.yml` is upstream's and triggers on `master`, which this
repository no longer has. `.github/` holds issue templates only. Proposed: a
GitHub Actions workflow that runs `./build-linux.sh --tests` and
`./build-macos.sh --tests` on a push to `main`.

### 15. Loose ends

- `apitest/scroll.c` defines five tests that the suite never runs:
  `test_small_screen_scroll`, `test_h_m_l`,
  `test_no_scroll_after_setting_topline`, `test_scroll_left_at_boundary`,
  `test_no_scroll_after_setting_left`. Run them, or find out why upstream
  did not. (`test_ctrl_f` there, and `insert_count` in `autoindent.c`, are
  commented out.)
- `vimSearchGetHighlights()` with a NULL buffer crashes once there is a
  search pattern; `vimSpellGetErrors()` checks for it.
- `libvim.c`: `vimSearchGetHighlights()` tests `count == 0` twice in a row;
  the comment that describes `spellWindowForBuffer()` sits above
  `vimSetSpellSuggestCallback()`; `vimInputCore()` does not free what
  `replace_termcodes()` allocated when the result is empty.
- "Known gaps" in CLAUDE.md lists `` :runtime `cmd` `` as a segfault. Eight
  forms of it gave `E79: Cannot expand wildcards` or ran, and none crashed.
  Either it is fixed or it needs a trigger that was not tried; when it is
  found, a test.

## How these were found

Reading, and the programs in [src/apitest/compare](src/apitest/compare)
(its README says how to run them):

- **Against vim** (`compare.sh`). libvim and the system's vim are each given
  the same key sequence from the same five-line buffer, and the cursor and
  text they end with are compared. About 1,300 sequences: Normal, Insert and
  Command-line keys, ex commands, mappings, registers. Many differences are
  the comparison's own and the README lists them.
- **For crashes and hangs alone** (`sweep.sh`). About 750 ex commands and
  option settings, 330 vimscript functions and 200 keys, each in a process of
  its own, to see that it comes back.
- **The C API with arguments it does not expect** (`api_edges.exe`): line
  ranges that are negative, reversed or past the end; NULL; a buffer that is
  not current.

When an entry here is fixed, the cases that showed it are a first check, and
the numbers in that README want bringing up to date.
