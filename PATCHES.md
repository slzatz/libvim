# How this fork differs from onivim/libvim

This repository is a permanent fork of
[onivim/libvim](https://github.com/onivim/libvim). Upstream is unmaintained and
nothing here is intended to go back to it, so changes land as ordinary commits
on `main` (called `master` until 2026-11-09) rather than as a patch queue.

If you are embedding this library, read this first: one change removes behavior
that upstream libvim advertises in its public API.

## Visual-mode `I` and `A` are vim's operators, not multiple cursors

Commits `a354ec4bc`, `eb3bb39b8` (Jan 2026).

**Upstream behavior.** In stock vim, `I` and `A` in Visual mode are operators:
`nv_edit()` routes them through `v_visop()` to `OP_INSERT` / `OP_APPEND`, and
for a blockwise selection the text you type is replicated onto every line of the
block — `ctrl-v jjjI- <esc>` prefixes four lines with `- `.

libvim replaced that with a multiple-cursor model for Onivim 2: it reported one
position per line of the block through the `vimSetCursorAddCallback()` hook,
ended Visual mode, and did a single ordinary insert at the cursor, leaving
replication to the embedder. Registering that callback is optional, so an
embedder that did not register it silently got a one-line insert — no error, no
beep. libvim's own `apitest/visual_mode.c` recorded the gap, committing
`test_insert_block_mode_change` commented out with the note "this test fails and
will be commented out". Non-blockwise `I`/`A` were affected too: in the
non-block branch of `nv_edit()` the `v_visop()` call was simply commented out.

**This fork.** `nv_edit()` calls `v_visop()` again, so Visual `I`/`A` behave as
they do in vim, blockwise and otherwise.

That required more than restoring one call, because `edit()` cannot block here —
insert mode is a pushdown state machine driven one key at a time, so an operator
that must run code *after* Insert mode ends has to split in two and put that
code in a cleanup callback. libvim had already done this for the change
operator (`state_change_*`), which is why blockwise `c` worked while blockwise
`I` did not. The insert operator now has the same shape:
`state_insert_initialize` / `state_insert_execute` / `state_insert_cleanup` in
`ops.c`, pushed from `do_pending_operator()` via `sm_push_insert_operator()`,
with the `block_insert()` replication in the cleanup half. See the "Visual Block
Operations" and "Recent Changes" sections of [CLAUDE.md](CLAUDE.md) for the
architectural detail.

**Breaking change for embedders.** `cursorAddCallback` is no longer invoked for
a blockwise `I`/`A`. `vimSetCursorAddCallback()` and the global still exist and
are still wired up in `libvim.c`; nothing calls them on that path any more. If
your application implements multiple cursors and relied on this, revert the
commits above. The two behaviors are mutually exclusive rather than additive: if
libvim replicates the text *and* the embedder applies it at each reported
cursor, every line but the first gets it twice.

The six `test_add_cursors_*` tests in `apitest/cursor.c` are kept behind `#if 0`
as the specification of what the callback used to report — they are the fastest
route back if you need it.

**Tests.** `apitest/visual_mode.c` covers block `I`, block `A`, block `$A`, and
a line too short to reach the block's start column (`I` skips it, `A` pads it
with spaces). `test_insert_block_mode_change` is upstream's own disabled test,
re-enabled: its expectations were right all along, it just needed
`vimInput("<c-v>")` / `vimInput("<esc>")` corrected to `vimKey()`, since
`vimInput()` passes its argument verbatim instead of parsing termcodes.

```sh
cd src
make libvim.a
make apitest/visual_mode.test.exe
cd apitest && ./visual_mode.test.exe
```

Expectations were taken from stock vim, which is worth doing for anything in
this area rather than reasoning from the source:

```sh
printf 'alpha\nbravo\ncharlie\ndelta\n' > /tmp/f.txt
vim -u NONE -N -es -c 'exe "normal! gg0\<C-v>jjI- \<esc>"' -c 'wq!' /tmp/f.txt
cat /tmp/f.txt
```

## Mappings can be expanded: `vimSetMappingsEnabled()`

**Upstream behavior.** `vimInit()` raises `no_mapping` and `no_zero_mapping`
once ("We expect the consumer to handle key-bindings and mappings"), so
`:map`, `:nnoremap` and the rest store their mapping, report it through
`vimSetInputMapCallback()`, and never expand it. Onivim 2 implemented
keybindings itself; an embedder without such a layer just gets mappings that
do nothing. vim's expansion code in `getchar.c` is untouched, and keys from
`vimInput()`/`vimKey()` already go through `ins_typebuf(..., REMAP_YES)` and
`vgetc()`, so that one counter is the whole of the block.

**This fork.** `vimSetMappingsEnabled(TRUE)` removes `vimInit`'s raise and
`vimSetMappingsEnabled(FALSE)` restores it; `vimGetMappingsEnabled()` reports
the state. The default is unchanged — disabled — so existing embedders see no
difference. The counters are adjusted by a delta rather than assigned, since
vim raises and lowers them itself around single keys (CTRL-V, digraphs,
counts). `vimInit` now *assigns* the baseline of 1 instead of incrementing, so
calling it more than once (test harnesses do) no longer stacks raises that one
enable could not undo.

An embedder that sends keys of its own as well as the user's can enable
mappings only around forwarding a user keystroke — vimango does — so a user's
`nnoremap s dd` cannot rewrite the embedder's internal `s`.

**Multi-key left-hand sides.** A key that begins a longer mapping
(`inoremap jk <Esc>`, after `j`) waits in the typeahead buffer for the next
one. `sm_execute()` used to insert each new key at offset 0 — *ahead* of the
one waiting — so the pair never completed and the first key was never seen.
It now appends (offset `typebuf.tb_len`, as `feedkeys()` does), so keys are
taken in the order they were sent, by every route: a key sent with mappings
off takes a waiting key with it, in front. With mappings disabled — the
default — nothing ever waits, so nothing changes for an embedder that has not
enabled them.

vim gives up on a half-typed mapping after `'timeoutlen'`. libvim has no
clock, so the embedder keeps the time: `vimInputPending()` says how many bytes
are waiting, and `vimInputTimeout()` is the timeout — the waiting keys are
taken as they are, or as the longest mapping they do complete (`j` mapped as
well as `jk`). Inside, `vgetorpeek()` starts with its `timedout` flag set
while the timeout is delivered; nothing else in it changed.

**Tests.** `apitest/mapping.c`: not expanded by default, normal and visual
mappings expanded when enabled (including an ex right-hand side), disabling
stops expansion, enable is idempotent, and counts and `0` are unaffected;
two- and three-key mappings in Insert and Normal mode, the first key kept
when the second is not the mapping's, the timeout with and without a shorter
mapping, and a waiting key going ahead of keys sent with mappings off.

## Spell checking is back

Commits `3564c5ba2`, `41cb09c3d`, `97fd21f77`, `3b4b34ee5` (Nov 2026).

**Upstream behavior.** libvim removed vim's spell checker in three commits
(`60046fe02`, `ee5010af6`, `fff5615cb`): `spell.c`, `spellfile.c`, the
`'spell'` options and every `FEAT_SPELL` block. `has('spell')` is 0, and an
embedder that wants spell checking has to bring its own (vimango used
hunspell).

**This fork.** The engine is restored, with an API in place of vim's screen
highlighting and its blocking `z=` prompt:

- `vimSpellGetErrors(buf, start, end, &n, &errors)` returns the misspellings
  in a line range as `spellError_T { lnum, col, len, type }`, with byte
  offsets and `type` one of bad, caps, rare or local. It follows vim's display
  rules: nothing unless the buffer is shown in a window with `'spell'` set and
  a dictionary loaded.
- `]s`, `[s`, `zg`, `zw`, `zG`, `zW` and `zug`/`zuw`/`zuG`/`zuW` work. `zu`
  needs a third key, which vim read with a blocking `plain_vgetc()`; it now
  goes through the normal-mode state machine (`NORMAL_THIRD_CHARACTER`, into
  `cmdarg_T.extra_char`).
- `{n}z=` applies suggestion n, as in vim. Plain `z=` calls the
  `vimSetSpellSuggestCallback()` hook instead of the numbered prompt; the
  embedder shows `vimSpellGetSuggestions()` and sends `{n}z=` back. Without a
  callback it beeps.
- The suggestion search is capped. Its cost is exponential in the word's
  length (uncapped, `llllllllllll` takes over two minutes), and vim 8.1 had no
  bound. `'spellsuggest'` accepts vim 9's `timeout:{msec}` (default 5000), but
  here it is an amount of work rather than a deadline, so a repeated search
  gives the same list and `{n}z=` applies the suggestion that was shown.
- `'spelloptions'` (`'spo'`, buffer-local, default empty), named after vim
  9's option but with this fork's own values. Each stops a kind of unknown
  word from being an error; words marked bad with `zw`/`zW` still are.
  - `noallcaps`: two or more capitals and no lower-case letter (`NASA`,
    `MP3`, `B2B`).
  - `nomixedcase`: a capital after the first character and a lower-case
    letter (`iOS`, `GitHub`, `IPv6`, `NASA's`, `PDFs`). It also passes a
    Shift typo such as `THe`, which can't be told from `IDs` by shape.

  A word capitalised only at its start (`Smith`) is checked as usual. The rule is in `spell_check()`, which every
  caller goes through, so `vimSpellGetErrors`, `]s`/`[s`, `z=` and
  `spellbadword()` agree: `]s` never stops on a word the embedder does not
  highlight. Vim 9's own values (`camel`, `noplainbuffer`) are not
  implemented and are rejected.

Not restored: spell highlighting in `screen.c` (the embedder draws) and
Insert-mode spell completion (`FEAT_INS_EXPAND` is off). The per-window `w_s`
synblock pointer was not brought back either: libvim has no `:ownsyntax`, so
the spell code uses `w_buffer->b_s` directly.

**Not a breaking change.** Nothing happens until an embedder sets `'spell'`
and `'spelllang'` and has a dictionary on `'runtimepath'`
(`runtime/spell/en.utf-8.spl` is in this repo). Set `'spellfile'` explicitly
before `zg` is used: when it is empty vim writes to the first `spell/`
directory on `'runtimepath'`, which may be inside this checkout.

**Tests.** `apitest/spell.c`: the engine through `spellbadword()` and
`spellsuggest()`, `vimSpellGetErrors` (order, byte columns, the capital check
across line breaks, range clamping, regional words), the normal-mode
commands including `zg` writing the spellfile, and `{n}z=`, the callback and
Visual mode for suggestions, and `'spelloptions'`. See the "Spell Checking Restored" section of
[CLAUDE.md](CLAUDE.md) for the implementation notes.

## Search highlights: every match, and whether to show them

Nov 2026.

**Upstream behavior.** `vimSearchGetHighlights()` walked the buffer with
`searchit()`, which looks for a match *after* the position it is given, as `n`
does from the cursor. It started at column 0 of `start_lnum` and resumed one
column past the end of each match, so it left out a match at the very first
column of `start_lnum` and any match starting where the previous one ended or
one column later: in `foo foo foo` it returned the first and third `foo`, in
`foofoofoo` the first and third, for `/o` one `o` of each `oo`. An embedder
drawing highlights from it showed every other match. `apitest/search_large_file.c`
had the miscount as its expected value: 15420 matches of `e` in a file with
16405.

**This fork.** Every match is returned. The search accepts a match at its
starting position (`SEARCH_START`) on the first call and whenever it resumes at
the end of a non-empty match; only after an empty match (`^`) does it move on
first, so it cannot loop. `start_lnum` 0 still means the first line.

`vimSearchGetHighlightEnabled()` is new: TRUE when `'hlsearch'` is set and
`:nohlsearch` has not hidden the matches since the last search command — vim's
own state, what `v:hlsearch` reports. The matches are returned regardless, so
an embedder asks this to decide whether to draw them, instead of tracking `/`,
`n`, `*` and `:noh` itself.

**If you relied on the old behavior.** Counts from `vimSearchGetHighlights()`
go up wherever matches sit close together. Nothing is removed.

Tests: `apitest/search_highlights.c` (`test_adjacent_and_first_column_highlights`,
`test_highlight_enabled`), `apitest/search_large_file.c`.

## vim's ordinary messages reach the message callback

Nov 2026.

**Upstream behavior.** libvim has no screen, so messages go to the callback set
with `vimSetMessageCallback()`. Upstream sent errors (`emsg`) and a handful of
commands it had converted one at a time (`:echo`, `:set opt?`, `:marks`,
`:jumps`, `:changes`, `:ls`). Everything that went through vim's `msg()` family
fell through to `msg_puts_attr_len()`, which ended in a `TODO` and dropped it: "5
lines yanked", "4 fewer lines", "5 more lines", "3 lines >ed 1 time", "search
hit BOTTOM, continuing at TOP", what undo and redo did, the count from `:s`,
`ga`, `g CTRL-G`.

**This fork.** `msg_attr_keep()`, where `msg()`, `smsg()`, `msg_attr()` and
`give_warning()` all end up, sends the message to the callback: `MSG_INFO`, or
`MSG_WARNING` when it came through `give_warning()`. It is sent whole, before
the truncation vim does for a screen, and not at all under `:silent`. An error
is still sent once, as `MSG_ERROR`: `emsg_core()` displays the error through the
same path after sending it, and that second pass is skipped.

`set_keep_msg()` keeps nothing. A kept message is one vim redisplays after its
next redraw and then forgets; with no redraw it stayed set, and while one is
set `msgmore()` holds back "N more lines" / "N fewer lines" so as not to
overwrite it. After a `:s` had reported its count, `4dd` said nothing.

`check_visual_highlight()` no longer warns "terminal cannot highlight" the
first time Visual mode starts. There is no terminal.

**If you relied on the old behavior.** A callback that counted every message
now counts these too — a file load reports `"name" 3L, 115C`, for instance. The
callback gets the priority, so count `MSG_ERROR` if errors are what you mean.


Tests: `apitest/messages_reports.c`.

## What a command lists reaches the message callback

Nov 2026.

**Upstream behavior.** vim writes a listing — `:registers`, `:map`, `:print`,
`:let` — a piece at a time: a title with `msg_puts_title()`, then a line per
item with `msg_putchar()`, `msg_outtrans()` and `msg_advance()`, which pads
with blanks up to a column. All of them come down to `msg_puts_attr_len()`,
and upstream dropped the text there (the previous section). Two results:

- **The commands said nothing**: `:registers`, `:display`, `:map` and its
  relatives, `:abbreviate`, `:command`, `:print`, `:number`, `:list`, `:#`,
  `:g/pat/p`, `:history`, `:highlight`, `:autocmd`, `:tags`, `:echon`. Only
  the few upstream had converted one by one (`:marks`, `:ls`, `:jumps`,
  `:set`) had output.
- **Some never returned.** The screen code that advanced `msg_col` went with
  the screen, and `msg_advance()` writes blanks *until `msg_col` reaches the
  column*. `:let`, `:let g:var` and `:highlight` looped in it for ever.

`:version` and `:messages` were handed to the embedder
(`vimSetDisplayVersionCallback()`, `vimSetGotoCallback()`) and did nothing
without one.

**This fork.** In `msg_puts_attr_len()`:

- `msg_col` and `msg_didout` are kept as the screen code kept them, so
  padding ends and columns line up.
- While an ex command runs — between `msg_listing_begin()` and
  `msg_listing_end()`, which `do_cmdline()` calls — the text is collected. It
  is sent as **one `MSG_INFO` message, with line breaks in it**, when the
  outermost command ends, or before any other message goes out (`msg2_send()`
  flushes first), so the callback receives things in the order vim wrote them:
  `:echo 1 | registers | echo 2` is three messages. Blank lines before and
  after a listing are dropped, and a listing that is nothing else is not sent.
- Not under `:silent`; and `execute('registers')` still returns the listing to
  the script, as it always did, without sending it.
- Keys typed outside an ex command write there too — the pattern `n` echoes on
  vim's command line. That is not sent.
- `msg_scroll` is set while an ex command runs. It is what makes `msg_start()`
  begin a new line where it would otherwise go back and write over the last
  message; vim sets it for a typed command, and without it the lines of
  `:1,3print` came out as one.

A message vim also *displays* after sending it (`msg_attr_keep()`), and the
commands upstream converted that still write to the screen as well (`:echo`,
`:marks`), are not collected a second time: `msg_listing_pause()` /
`msg_listing_resume()`.

Smaller things, found on the way:

- `:version` with no callback lists vim's version and features.
- `:messages` with no goto callback lists vim's message history, and
  `:messages clear` with no clear callback clears it.
- `:echomsg` is vim's `msg_attr()` again: sent to the callback as before, and
  now also kept in the message history, which is its purpose. Under `:silent`
  it is no longer sent.
- `:echoerr` sent its error twice; once now.
- A bare `:history` is the command history, as documented. vim leaves that to
  the kind of command line last typed, which under `vimExecute()` is anything.

**If you relied on the old behavior.** Commands that sent nothing now send a
message. A callback that treats every `MSG_INFO` as one line of text will meet
line breaks.

**Known gaps.** Columns are laid out for `Columns` cells (`:registers`
truncates there). `:s` with the `c` flag still has no prompt.

**Tests.** `apitest/listings.c`.

## A typed search is one search, from where it started

Nov 2026.

**Upstream behavior.** While `/pattern` is typed, `'incsearch'` moves the
cursor to the match. vim's `getcmdline()` puts it back as it returns
(`finish_incsearch_highlighting()`), and `nv_search()` then runs the search.
libvim's command-line state did not put it back; instead, normal mode searched
twice from wherever the cursor had been left — forward, then back — "so
everything is set for N and n". For a plain pattern typed with `'incsearch'`
on, that ends on the right match. Otherwise:

- **Wrap warnings were those of the two extra searches**, not of the real
  one: `/second` from line 1, with one match in the buffer, said "search hit
  BOTTOM" *and* "search hit TOP"; `?second` from line 1, which does wrap, said
  nothing.
- **An offset was an error**: `/second/e` went to the right place and then
  reported `E486: Pattern not found: second/e`.
- A pattern that is not there was reported twice.
- **A failed search moved the cursor**: with `'nowrapscan'`, `/first` from the
  last line gave `E385` and left the cursor on line 1, where `'incsearch'` had
  put it.
- **With `'noincsearch'` it found the wrong match**: forward then back from
  the starting position is the *previous* match.
- An empty `/` did not repeat the last search, and the `''` mark was not where
  the search started.

**This fork.** `state_cmdline_execute()` calls
`finish_incsearch_highlighting()` on its way out, and normal mode runs vim's
one `normal_search()`, with `SEARCH_MARK` unless CTRL-G or CTRL-T moved the
start (then the mark is already set, at the original position). Everything
above behaves as in vim, and a wrap is reported once, when it happens.

**Tests.** `apitest/search_typed.c`.

## CTRL-R works, and the expression register does not hang

Nov 2026.

**Upstream behavior.** vim reads the register name after CTRL-R with
`plain_vgetc()`, and for `=` reads a whole command line with `getcmdline()`.
Both wait for keys. libvim is handed its keys and cannot wait, and upstream
dealt with that in two ways:

- In **Insert mode** and on the **command line** the CTRL-R code was taken
  out. In Insert mode CTRL-R was inserted as a character (`^R`), followed by
  the register's name. On the command line it was dropped and the name typed:
  `:echo "<C-r>a"` ran `echo "a"`.
- In **Normal mode** it was left in: `"=` and `@=` called
  `get_expr_register()`, and **never returned**.

**This fork.** Each state remembers that it is owed a key, and takes it on
its next call.

| | |
|---|---|
| Insert: `CTRL-R {reg}` | the register, as if typed |
| Insert: `CTRL-R CTRL-R {reg}`, `CTRL-R CTRL-O {reg}`, `CTRL-R CTRL-P {reg}` | literally; without auto-indent; with the indent fixed |
| Insert: `CTRL-R = {expr} <CR>` | the value of an expression |
| Command line: `CTRL-R {reg}`, `CTRL-R CTRL-R {reg}` | the register; literally |
| Command line: `CTRL-R CTRL-W`, `CTRL-A`, `CTRL-L`, `CTRL-F`, `CTRL-P` | the word, WORD, line, file name, path under the cursor |
| Command line: `CTRL-R = {expr} <CR>` | the value of an expression |
| Normal: `"= {expr} <CR>` then `p`, `P` … | the expression register |
| Normal: `@= {expr} <CR>` | execute the value as keys |

The register name is not mapped (`no_mapping` is raised until it arrives),
and `<Esc>` in its place cancels the CTRL-R and nothing else.

**The expression is typed on a command line.** The command-line state is
pushed on top of the state that asked, so while it is typed `vimGetMode()` is
`CMDLINE` and `vimCommandLineGetType()` is `'='`; `vimCommandLineGetText()` is
the expression. Its `<CR>` gives the value to the state underneath and its
`<Esc>` gives nothing — Insert mode goes on, and a command line that was
being typed is still there, as it was (`save_cmdline()` puts it aside
meanwhile). An embedder that draws the command line, or that acts on `<CR>`
itself, should look at the type. An empty expression is the one used last.
Inside an expression's own line `CTRL-R =` is refused, as in vim.

**If you relied on the old behavior.** A literal CTRL-R in the text is
`CTRL-V CTRL-R`.

**Tests.** `apitest/ctrl_r.c`.

## Nothing waits for a key

Nov 2026.

**Upstream behavior.** vim asks the user things from deep inside a command:
`input()`, `inputsecret()`, `inputdialog()` and `inputlist()` read a line or
a number, the debugger (`:debug`, a breakpoint) reads a command. Each ends in
`vgetc()` waiting for a key. In libvim a key cannot arrive during a call —
keys come between calls, through `vimInput()`, and `ui_inchar()` reads
nothing — so the wait was a loop that never ended. `:echo input('x')` hung
the embedder, as did a mapping or a function that called it.

**This fork.** vim has the same problem inside `:normal`, where the keys
given to it can run out, and an answer for it: `vgetorpeek()` returns the key
that makes the reader give up — `<Esc>`, or CTRL-C on a command line. That
answer is now given whenever a key is asked for and none is waiting. The
state machine is not affected: it takes a key only after `vpeekc()` has seen
one, so it never asks for a key that is not there.

- `input()`, `inputsecret()` and `inputdialog()` return `''`; `inputlist()`
  returns `0`. It is not an interrupt: the script goes on.
- Keys already in the typeahead are still the answer, as in vim: the rest of
  a mapping after the call, or of a `:normal`. (`inputsave()` is vim's remedy
  when that is not wanted.)
- The debugger, finding no command, continues as `cont` does. vim would ask
  again.
- `get_keystroke()`, which reads `ui_inchar()` itself, returns `<Esc>`
  (`ask_yesno()` takes that for "n").

**What it means for an embedder.** A script cannot ask the user anything. An
embedder that wants `input()` to work has to supply the answer another way;
there is no callback for it yet.

**Tests.** `apitest/no_waiting.c`. A regression there is a test that never
finishes.

## An embedder can stop vim writing files, and restricted mode starts no shell

Nov 2026.

**Upstream behavior.** Vim writes files in many more ways than `:w`:
`:saveas`, `:w` with a range, `:redir > name`, `:mkexrc`, `:mksession`,
`:mkview`, `:mkspell`, `writefile()`, `'verbosefile'`, a swap file once
`'swapfile'` is set. An embedder whose buffers are not files can refuse the
commands a user types, but each is one `:execute`, mapping, autocommand or
sourced script away, and vim's restricted mode (`restricted = 1`, what
`vim -Z` sets) does not cover writing: `rvim` saves files.

Restricted mode is meant to stop vim starting programs, and each command that
runs the shell checked for it. Two routes reached `call_shell()` without
asking: `:diffpatch`, which ran `patch`, and a diff with `internal` taken out
of `'diffopt'`.

**This fork.** `vimSetFileWritesEnabled(FALSE)` (`libvim.h`): vim writes no
file. The check is where vim opens one — `mch_open()` and `mch_fopen()` are
now `libvim_open()` and `libvim_fopen()` (`vim.h`, `libvim.c`), which fail
with `EACCES` for any mode that can create or change a file — so it does not
depend on a list of commands, and covers the ones nobody thought of. The
commands that exist to write a file (`buf_write()`, which is every `:w`,
`:saveas`, `:wq` and `:update`; `open_exfile()`, which is `:redir` and the
`:mk…` family; `writefile()`; `:mkspell`) check first, with
`check_file_writes()`, and say `Writing files is not allowed here`.
`buf_write()` checks before anything else, so an existing file is not backed
up, renamed or truncated on the way to failing.

One file is still written: the word list in `'spellfile'` and the `.spl` built
from it (`zg`, `zw`, `:spellgood` …), and the temporary list `zG`/`zW` use.
`spell_add_word()` brackets its work with `libvim_own_file_write()`. Note that
`'spellfile'` is an option: a script that sets it chooses where those words
go, though not what is written beyond a word list.

Writes are on by default, as upstream, and `vimInit` turns them back on.
Reading is not affected.

`call_shell()` itself now calls `check_restricted()`, so restricted mode holds
for every route to the shell, present and future (`E145`). One consequence:
vim expands a file pattern containing `` ` ``, `'` or `{` by handing it to the
shell, and in restricted mode that now fails rather than runs — including a
`'runtimepath'` entry with one of those characters in it.

**If you relied on the old behavior.** Nothing changes unless you call
`vimSetFileWritesEnabled(FALSE)` or set `restricted`.

Tests: `apitest/file_writes.c` — sixteen ways to write a file, each shown to
write one with writes on and none with them off; and a `'shell'` that leaves a
mark, to show that restricted mode no longer runs it for `:diffpatch`.

## Other changes

- `3f9d02b14` — GCC 14+ warning suppressions in `src/Makefile`'s `WARN_FLAGS`,
  needed to build on current toolchains.
- GCC 16 build fixes: `do_wqall()` (`src/ex_cmds.c`) dropped its `error`
  counter, dead since libvim replaced vim's exit-status `getout()` with
  `quitCallback` and fatal under `-Werror=unused-but-set-variable`;
  `src/Makefile` gained header dependencies for `libvim.o`, `state_machine.o`
  and `state_insert_literal.o`, without which `make -j` could compile them
  before `auto/osdef.h` was generated; and `build-linux.sh` passes
  `-Wno-error=implicit-int -Wno-error=implicit-function-declaration` to
  `./configure`, whose probes GCC 14+ rejects.
- `src/Makefile` header dependencies for `spell.o` and `spellfile.o`, which the
  spell-checking restore left out: without them a `structs.h` change rebuilt
  neither, and `spellfile.o`, still compiled against the old `synblock_T`
  after `'spelloptions'` added `b_p_spo`, read a NULL `'spellfile'` and
  crashed on `zg`. A pre-existing build needs `--clean` or this commit.

## Downstream

Built for [vimango](https://github.com/slzatz/vimango), which links `libvim.a`
statically and wants stock vim keybindings rather than a multi-cursor editor.
Nothing here is vimango-specific — the result is simply closer to vim than
upstream libvim is.
