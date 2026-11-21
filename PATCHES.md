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
mapping, and a waiting key going ahead of keys sent with mappings off;
`<Leader>` and `<LocalLeader>` (vim's own code, unchanged): a backslash when
unset, a space leader in Normal and Visual mode with a lone space still a
space after the timeout, and the leader read when the mapping is defined.

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

## `:normal` runs all of its keys, and ends what they leave unfinished

Nov 2026.

**Upstream behavior.** Two things in `sm_execute_normal()`, which is
`:normal`:

- **The last command was lost if vim translates it.** `x` is `dl`, `X` is
  `dh`, `D` is `d$`, `C` is `c$`, `s` is `cl`, `S` is `cc`, `Y` is `yy` and
  `&` is `:s\r`: vim puts the translation in the stuff buffer and reads it
  next. The loop ran while the *typeahead* buffer had keys, so the
  translation of the last key was never read. `:normal x` did nothing;
  `:normal ifoo<Esc>x` inserted and did not delete; and with a range each
  line ran what the line before had left — `:%normal x` deleted the first
  character of every line but the first.
- **An unfinished command was thrown away, not ended.** `:normal Afoo` is
  still in Insert mode when the keys run out. vim ends it: `vgetc()`, with
  nothing to give, gives `<Esc>` (CTRL-C on a command line). libvim freed the
  state instead. The text was there, but the insert had never ended: the
  cursor was one column too far right, and `.` afterwards — repeating an
  insert with no `<Esc>` at its end — stayed in Insert mode, where the keys
  typed next went into the text.

**This fork.** The loop runs while there is a key in the typeahead buffer or
the stuff buffer, or one handed back by a state that ended. When they are used
up and a state is still above the Normal state `:normal` started in, that
state is given the key `vgetc()` gives — `<Esc>`, or CTRL-C on a command line —
until it is gone. So, as in vim: `:%normal A;` leaves Normal mode, with the
cursor on the `;` and `.` repeating the append; `:normal :s/a/b` (no `<CR>`)
and `:normal /pattern` are abandoned; `:normal d` leaves no operator pending.

**If you relied on the old behavior.** Nothing sensible could: the commands
above did nothing or ran on the wrong line.

**Tests.** `apitest/ex_normal.c`.

## CTRL-O in Insert mode is one command

Nov 2026.

**Upstream behavior.** After CTRL-O, libvim went back to Insert mode when a
key had been run and the stuff buffer was empty. vim's test has more to it,
and without the rest:

- **`CTRL-O :cmd<CR>` and `CTRL-O /pattern<CR>` did not return to Insert
  mode.** Nothing looked when the command line came back. The *next key* was
  run as a Normal mode command, and Insert mode resumed after that: `x`
  typed after `<C-o>:w<CR>` deleted a character. The same in a mapping:
  `inoremap <F2> <C-o>:set list!<CR>`.
- **`CTRL-O v`** was back in Insert mode at once, with Visual mode still on;
  the motion and operator that followed were typed as text.
- **`CTRL-O "a`** likewise: `<C-o>"ayy` named the register and typed `yy`.
- After a command vim translates (`CTRL-O x`, which is `dl`), `vimGetMode()`
  reported `OP_PENDING` once Insert mode was left, until the next key.
- **From Replace mode it came back in Insert mode**: `R`, CTRL-O, a command,
  and what was typed next was inserted, not written over the text.

**This fork.** Insert mode is restarted as at the end of vim's `normal_cmd()`:
not while an operator waits for its motion, a register has been named for the
command that follows (`"a`, or `"=` with its expression typed), or Visual mode
is on; and it is restarted when a command line returns, run or abandoned.
`vimGetMode()` after `CTRL-O :` is `CMDLINE`, and `INSERT` again at the `<CR>`.
The mode restarted is the one CTRL-O was typed in — Insert, Replace (`R`) or
Virtual Replace (`gR`).

`:startinsert`, `:startinsert!`, `:startreplace` and `:startgreplace` ask for
Insert mode by the same means, and so now work when typed or run from a
mapping: Insert mode starts at the `<CR>`. Before, the next key was run as a
Normal mode command first. Given to `vimExecute()` they are as they were —
no key is being handled, so Insert mode starts after the next Normal mode
command.

**If you relied on the old behavior.** An embedder that sent a key after
`CTRL-O :cmd<CR>` expecting Normal mode now types it. Send `<Esc>` first.

**Tests.** `apitest/insert_ctrl_o.c`.

## `v`, `V` and CTRL-V after an operator force the motion's type

Nov 2026.

**Upstream behavior.** Between an operator and its motion, `v`, `V` and
CTRL-V say how the motion is to be taken: `dvj` is characterwise (from the
cursor to the same column of the next line), `dVw` linewise, `d<C-V>j`
blockwise, and `v` before an inclusive motion makes it exclusive (`dve`). vim's
`nv_visual()` notes the type and clears `finish_op` — "operator doesn't finish
now but later". libvim set `finish_op` again as soon as the command function
returned, so the operator ran at once with the `v` as its motion, and the real
motion was then a command by itself: `dvj` deleted one character and moved
down a line; `d<C-V>j` deleted nothing.

**This fork.** The operator goes on waiting, as in vim; `vimGetMode()` is still
`OP_PENDING` after the `v` and `vimGetPendingOperator()` still reports the
operator. Counts, `.` and `<Esc>` behave as they do for any pending operator.

**Tests.** `apitest/forced_motion.c`.

## On the command line, CTRL-V `<CR>` is a character, and CTRL-R does not run a line

Nov 2026.

**Upstream behavior.** After CTRL-V the next key is taken literally. The
command-line state reads it in a state of its own and takes the character up
when that returns — but it did so *after* testing the returning key for the
keys that end a command line. So CTRL-V `<CR>` ran the command line instead of
putting a `^M` on it (`:s/x/^M/` could not be typed).

That reached further than CTRL-V. vim puts a register on the command line by
typing it, with CTRL-V before any character that would end the line, and a
register holding whole lines ends each in a `<CR>`. So **CTRL-R of a yanked
line ran the command line**: `yy` on a line reading `2,3d`, then `:` CTRL-R
`0`, deleted two lines with no `<CR>` typed. An embedder that takes `<CR>`
itself, to check what is about to run, never saw that one — it came from
inside libvim.

**This fork.** The literal is taken up first, and goes straight onto the
command line, as in vim's `getcmdline()`. CTRL-V `<CR>`, CTRL-V `<Esc>` and
CTRL-V CTRL-C insert the character. CTRL-R of a line puts the text and a `^M`
on the command line and leaves it there: `vimCommandLineGetText()` ends in
`"\r"`.

**If you relied on the old behavior.** A register that is meant to run as a
command is run with `:@a`, not by putting it on the command line.

**Tests.** `apitest/cmdline.c` (`test_insert_literal_keys_that_end_the_line`,
`test_insert_literal_cr_in_a_substitute`) and `apitest/ctrl_r.c`
(`test_cmdline_register_of_a_line_is_not_run`).

## `g0`, `g^`, `gm` and `g$` ask the embedder: `vimSetCursorMoveScreenColumnCallback()`

Nov 2026.

**Upstream behavior.** `gj`, `gk`, `H`, `M` and `L` ask the embedder where
its screen lines are (`vimSetCursorMoveScreenPositionCallback()`,
`vimSetCursorMoveScreenLineCallback()`), because libvim's window matches
nothing the embedder draws. The motions to a place *in* a screen line did
not: `g0` (`g<Home>`), `g^`, `gm` and `g$` (`g<End>`) were worked out in
`nv_g_cmd()` from libvim's own window -- 80 columns unless
`vimWindowSetWidth()` is called, and `'nowrap'`, which is libvim's default
and not vim's. On a line of 200 columns `150|g0` went to column 109 and
`150|g$` to 188, the edges of a horizontally scrolled view nobody was shown;
on a line shorter than the window they were `0`, `^` and `$`. With `'wrap'`
set the line was wrapped at that width at any character, which is not where
an embedder that wraps at word boundaries has its screen lines.

**This fork.** A third callback, for an embedder that wants it:

```c
typedef enum
{
  MOTION_SCREEN_START,  /* g0, g^: its first character */
  MOTION_SCREEN_MIDDLE, /* gm: the one half the width of the window along it, or its last */
  MOTION_SCREEN_END,    /* g$: its last character */
} screenColumnMotion_T;
typedef void (*CursorMoveScreenColumnCallback)(
    screenColumnMotion_T motion, linenr_T lnum, colnr_T col, colnr_T *destCol);

void vimSetCursorMoveScreenColumnCallback(CursorMoveScreenColumnCallback f);
```

It is given a position -- a line and a byte offset in it -- and answers with
the byte offset, in the same line, of a character of the screen line holding
that position. `*destCol` starts as `col`, so a callback that does nothing
leaves the cursor where it is.

- `g0` goes to the start; `g^` goes there and on to the first non-blank, as
  in vim.
- `gm` is the embedder's to work out, not the middle of the answers to the
  other two: vim's `gm` is half the width of the window from the start of the
  screen line, and a screen line can be shorter than the window.
- `g$` goes to the end, and the column wanted afterwards is that character's,
  not "the end of the line": `j` keeps to it. With a count above 1 it is the
  end of a screen line further down, which is the *position* callback's, asked
  for `count - 1` lines down with `curswant` at `MAXCOL` -- `gj` after `$`.
  With no position callback that fails, as `gj` does.
- An answer outside the line is brought to its first or last character, and
  one inside a multi-byte character to that character's first byte.
- As motions they are what they are in vim: `g$` inclusive, the rest
  exclusive, so `dg$` deletes to the end of the screen line and takes its
  last character.

**If you relied on the old behavior.** Nothing changes unless the callback is
set: with none, the four are worked out from libvim's window as before.

**Tests.** `apitest/motion_screen.c`.

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

## An embedder can forget a buffer's undo history: `vimUndoClear()`

Nov 2026.

**Upstream behavior.** `vimBufferSetLines()` saves nothing for undo and
leaves what the buffer has saved as it was. Upstream's way to keep the two in
step is `vimUndoSaveRegion()` before the call, which makes the new lines a
change `u` undoes. An embedder that wants no such thing — it has loaded a
file's text into a buffer, or loaded it again because the file changed — had
nothing to call, and a history that described the old lines: `x` on line 1,
new text set over the whole buffer, then `u`, put the old line 1 back over the
new one and left the rest. With a line spliced in above an edit, `u` did not
even leave the number of lines as it was.

**This fork.** `vimUndoClear(buf)` (`libvim.h`) forgets the buffer's history,
as vim does when it reloads a file (`buf_reload()`): `u`, CTRL-R, `U`,
`:earlier` and `:undolist` find nothing, and changes made afterwards are
undone as usual, back to the text that was set and no further. The text, the
changed tick and the modified flag are not touched. Called during an insert,
what the rest of that insert types is not saved for undo.

`vimBufferSetLines()` is as it was, and so is `vimUndoSaveRegion()` before it.
The comment on `vimBufferSetLines()` in `libvim.h` now says that one of the
two is wanted.

**If you relied on the old behavior.** Nothing changes unless you call
`vimUndoClear()`.

**Tests.** `apitest/undo_redo.c`: `test_set_lines_alone_leaves_a_stale_history`
is the fault, the `test_clear_undo_*` tests and the two beside them what the
call does about it, and upstream's `test_undo_save` and `test_undo_sync` the
other way of using `vimBufferSetLines()`, unchanged.

## `vimBufferSetLines()` looks after the cursor and the marks

Nov 2026.

**Upstream behavior.** `vimBufferSetLines()` appended and deleted lines and
looked at nothing else the buffer or a mode holds. vim's own `setbufline()`,
`appendbufline()` and `deletebufline()` move the marks and the cursor.

- The cursor stayed where it was. Five lines replaced by one with the cursor
  on line 5: the next key gave `E315: ml_get: invalid lnum: 5`, and in Insert
  mode read past the buffer's memory (AddressSanitizer: a
  heap-buffer-overflow in `ins_char_bytes()`). On a line that was still
  there but shorter, the column was past its end.
- Marks stayed on their line numbers: `'a` gave `E19: Mark has invalid line
  number`, or went to whatever line had that number now.
- A negative `start` corrupted the buffer. A `start` past the end, or a
  buffer that had never been current, set nothing and still raised the
  changed tick and the modified flag.

**This fork.**

- **The cursor and the marks go with their lines.** The first lines of the
  range are replaced one for one, and a cursor or mark on one stays. Lines
  below the range move and take theirs along (`mark_adjust()`: named marks,
  `'[` `']` `'<` `'>` `'.` `'^`, the jumplist and the changelist). When
  the new lines are fewer, a mark on a line that went is not set any more
  (`E20`), and the cursor goes to the line that followed. The cursor is
  then brought inside the text. So a line spliced in above the cursor moves
  the cursor down with its line, where it used to stay on its number; set
  the cursor after the call if that is not wanted.
- **What a mode holds.** The other end of a Visual area, and where the
  insert started, are moved and brought inside the text as the cursor is. A
  Normal mode command that is half typed — a count, a register, an operator
  waiting for its motion — is forgotten, as `<Esc>` forgets it
  (`normal_cmd_forget_pending()`, `normal.c`): the operator holds the
  position it was typed at, and `d CTRL-V`, lines set, then a motion read
  past the end of a line. A blockwise insert or change in progress
  (`CTRL-V jj I`) is not copied to the other lines of its block when Insert
  mode ends (`set_lines_count`, `ops.c`): the block is lines and columns of
  the text there was, and copying gave `E315` for each of its lines that
  was gone.
- **Arguments.** `start` past the last line is the last line; `end` of -1,
  or past it, is the last line; `end` before `start` is `start`. A negative
  `start` or `count`, no lines to set and none to remove, or a buffer whose
  memline is not open (`vimBufferNew()` without `vimBufferSetCurrent()`;
  `setbufline()` refuses it too) do nothing and are not a change.
- In a buffer that is not the current one the marks and the cursor it will
  have again are adjusted the same way, and the context mark of the window
  (`''`), which is the current buffer's, is left alone.

Undo is as it was: see the section above. vimango calls `vimUndoClear()`
after every set.

**If you relied on the old behavior.** A line put in above the cursor now
moves the cursor down with its text (vimango's `TestCommandLineAPI` put a
line above an empty buffer's one line and expected to be on the new one).
A call with `start` past the end now appends.

**Tests.** `apitest/buffer_set_lines.c`, the twelve after
`test_modified_is_set`. `apitest/compare/setlines_stress.exe` gives libvim
keys and `vimBufferSetLines()` calls in a random order, each seed in a
process of its own, and fails on `E315`, a cursor outside the buffer, a
crash or a hang; it is what found the half-typed operator and the blockwise
insert. Under AddressSanitizer, 5,000 seeds of 400 steps pass.

## The column wanted after `$` is the one the cursor went to

Nov 2026.

**Upstream behavior.** `0$0j` put the cursor at the end of the next line. So
did `$^j` and `$ggj`, `l$hj` from column 1, and any motion along the line
that brought the cursor back to the column where a motion had last left it:
`j` and `k` went on wanting the end of the line, or whatever column the
cursor had been taken to in between. vim goes to the column the cursor is in.

It took a line of 80 characters or fewer, which is most lines but not a
paragraph kept on one line, and a cursor that an earlier motion had placed:
`$0j` as the first keys in a buffer was right.

The cause is `update_topline()` (`move.c`), which libvim made a function that
does nothing, since the embedder scrolls. In vim it begins with
`check_cursor_moved()`, and `curs_columns()` counts on that: it calls
`update_topline()` first and then marks `w_virtcol` valid, which means valid
for the cursor recorded in `w_valid_cursor`. With the check gone the record
was not brought up to date. After `$` the column of the line's end was valid
for a cursor still recorded where it had been before; a motion back to that
column looked like no motion at all, and `update_curswant()` took the stale
`w_virtcol` for the column wanted. On a longer line `$` scrolls libvim's own
window sideways (80 columns, `'nowrap'`), and the change of `w_leftcol`
invalidated the column by another route.

**This fork.** `update_topline()` calls `check_cursor_moved(curwin)` and does
nothing else.

**If you relied on the old behavior.** Nothing to do.

**Tests.** `apitest/normal_mode_curswant.c`: the five `test_curswant_reset_*`
tests fail without the change, and `test_curswant_end_again` is `$` still
wanting the end. `apitest/compare/cases/curswant.txt` is sequences to compare
with vim: of the 56 written for this, 21 differed, and none do.

## What a buffer just entered wants is settled before the first key

Nov 2026.

**Upstream behavior.** `$` as the first key in a buffer went to the end of
the line and wanted the end of that line only: `j` then went to that column
of the next line, and `gj` kept the column where it should have gone to the
end of each screen line. Any `$` after another command was right.

A buffer that has just been entered has a column wanted that is still to be
worked out from where the cursor is (`enter_buffer()` sets
`w_set_curswant`). vim works it out at the top of `main_loop()`, before the
next command. `vimInputCore()` did it only after the command it had run, so
the first command's own answer was replaced: `$` set the column wanted to
"the end", and the pending flag then made it the column the cursor was in.

**This fork.** `vimInputCore()` calls `update_curswant()` before the key as
well as after it. The call after stays: `vimCursorGetColumnWant()` is right
as soon as the key returns.

**If you relied on the old behavior.** Nothing to do.

**Tests.** `apitest/normal_mode_curswant.c`,
`test_curswant_dollar_first_in_buffer`, and four more sequences in
`apitest/compare/cases/curswant.txt`.

## Nothing a command or key does ends the process: `:cq` and `<PasteStart>`

Nov 2026.

**Upstream behavior.** `:cq` called `getout(1)`, which is `exit(1)`: the
process ended there, and the quit callback was never called, though `:q`,
`:qa` and `:qa!` all go to it. An embedder that refused `:cq` on its own
command line still met it through `:execute 'cq'` or a mapping.

`<PasteStart>`, the key a terminal sends before a bracketed paste, crashed in
Normal, Insert and Command-line mode, from a mapping, `:normal` or
`:execute` as well as from `vimKey()`: `bracketed_paste()` (`edit.c`) takes
the length of the paste's end code, `find_termcode("PE")`, which libvim does
not have, and would then have read the paste from a terminal it does not
have either. In Visual mode it deleted the selection and started Insert
mode.

**This fork.** `:cq` (`ex_cquit()`, `ex_docmd.c`) calls the quit callback as
`:qa!` does — no buffer, forced — and nothing else; while the command line is
locked it says so and does nothing. `vgetc()` (`getchar.c`) turns
`<PasteStart>` and `<PasteEnd>` into `K_IGNORE`, which every state already
skips, so the keys do nothing wherever they come from. `bracketed_paste()`
is no longer reached. An embedder pastes by sending the text, with `'paste'`
set if it wants it taken literally.

**If you relied on the old behavior.** If your quit callback ends the
process, `:cq` still does, through it; the exit code is the callback's to
choose. Nothing can have relied on `<PasteStart>`.

**Tests.** `apitest/quit.c`, `test_cquit` (`:cq`, `:execute 'cquit'`, and
typed); `apitest/paste_start.c`, the key in each mode, in a mapping and in
`:normal`. Before the change the first ended the test program and the second
crashed it.

## A running command can be interrupted: `vimInterrupt()`

Nov 2026.

**Upstream behavior.** Nothing could stop a command that does not end —
`:while 1 | endwhile`, a mapping that types itself again (`:nmap Q jkQ`), a
function that never returns. libvim reads no keys while it runs one, so the
user's CTRL-C could not reach it, and the call never returned. Setting vim's
`got_int` from outside did stop a `:while`, but nothing cleared it
afterwards — vim's `main_loop()` does, and libvim has none — so
`vgetorpeek()` answered every later request for a key with CTRL-C: the next
`vimInput()` never returned, and sent "Type :qa! and press <Enter> to abandon
all changes and exit Vim" to the message callback as fast as it could.

**This fork.** `vimInterrupt()` sets `got_int` and nothing else, so it may be
called from another thread or a signal handler while a call into libvim is
running. Whatever checks `got_int` in vim stops — `:while`, `:for`, a
function, mapping expansion, a search — and the call returns:

- `sm_run()` (`state_machine.c`), which hands typed keys to the states, takes
  one more key once interrupted — the `<Esc>` or CTRL-C `vgetorpeek()` gives
  for it — and stops. The state takes that key as a typed one: in Insert
  mode and on a command line it ends them, as in vim, and in Normal mode it
  cancels what was half typed. (Until the next section it was dropped in
  Normal mode, where a typed CTRL-C said how to quit vim.)
- `vimInputCore()` (`vimInput()`, `vimKey()`), `vimExecuteLines()`
  (`vimExecute()`), `vimInputTimeout()` and `vimEval()` clear `got_int` and
  flush the typeahead on the way in and on the way out (`interrupt_done()`,
  `libvim.c`), which is what `main_loop()` does in vim. On the way in, an
  interrupt that came between calls is forgotten; on the way out, nothing
  the embedder asks afterwards (search highlights, spelling) sees a stale
  one and stops short.

An interrupt is for the call it arrives during. A command that loops in C
without looking at `got_int` is not stopped; vim's own loops look.

**If you relied on the old behavior.** Nothing to do: until
`vimInterrupt()` is called, nothing sets `got_int` (`vimInit()` installs no
signal handlers), and the clearing does nothing.

**Tests.** `apitest/interrupt.c`: `:while` run, typed a key at a time and
typed in one call, a mapping that never ends in Normal and in Insert mode,
`:while` around `:normal`, a function that never returns, and an interrupt
between calls; none may say how to quit vim. A second thread interrupts
after 200 ms; before the change, with `vimInterrupt()` alone, the program
never finished. `apitest/compare` gives the same results for all
2,692 cases as before the change.

## CTRL-C in Normal mode says nothing

Nov 2026.

**Upstream behavior.** CTRL-C in Normal mode with nothing to cancel sent
vim's "Type  :qa  and press <Enter> to exit Vim" to the message callback —
"Type  :qa!  and press <Enter> to abandon all changes and exit Vim" once a
buffer had changed (`nv_esc()`, `normal.c`).

**This fork.** Nothing is said. libvim has no screen, and how an embedder
quits is its own: in vimango `:qa!` closes the note, and the message told a
user who pressed CTRL-C that their changes were about to be abandoned.
What CTRL-C does is unchanged — it cancels a count, an operator or a
register typed, ends Visual mode, and calls the unhandled-escape callback.

With nothing to avoid, `sm_run()` no longer drops the CTRL-C an interrupt
leaves in Normal mode (previous section): it goes to the state like any
other key.

**If you relied on the old behavior.** Show your own hint on CTRL-C if you
want one; the unhandled-escape callback is called for a CTRL-C with nothing
to cancel, as before.

**Tests.** `apitest/messages_reports.c`, `test_ctrl_c_says_nothing`;
`apitest/interrupt.c`, every case of which checks that nothing says
`:qa`. `apitest/compare`: the same results for all 2,692 cases.

## CTRL-K, CTRL-\ and the completion keys in Insert mode

Nov 2026.

**Upstream behavior.** Three kinds of Insert-mode key put a control
character in the text:

- **CTRL-K**, vim's digraph key (`CTRL-K a :` is `ä`): digraphs were "temporarily
  disabled" in `feature.h`, and with them gone CTRL-K was a character. On the
  command line too. vim's way of reading the two characters,
  `ins_digraph()` and `get_digraph()`, waits for keys.
- **CTRL-\ CTRL-N**, **CTRL-\ CTRL-G** and **CTRL-\ CTRL-O**, which leave Insert
  mode: the code that reads the second key was commented out ("TODO: Bring
  back this logic"), so both keys were inserted.
- **CTRL-N**, **CTRL-P** and **CTRL-X**: keyword completion and the CTRL-X
  commands are not compiled in (`FEAT_INS_EXPAND`), and vim without them
  inserts the key. In an editor of notes the reflex CTRL-N put an invisible
  `^N` in the text, and it was saved.

**This fork.**

| | |
|---|---|
| Insert: `CTRL-K {char1} {char2}` | the digraph, as in vim; `.` repeats it |
| Command line: `CTRL-K {char1} {char2}` | the same, on the line |
| `:digraphs`, `'digraph'` (`{char1} <BS> {char2}`), `digraph` in `has()` | as in vim |
| Insert: `CTRL-\ CTRL-N`, `CTRL-\ CTRL-G` | leave Insert mode, as `<Esc>` does |
| Insert: `CTRL-\ CTRL-O` | one Normal mode command, the cursor not moved back |
| Insert: `CTRL-\ {other}` | a `^\` in the text, then {other} as typed (vim's own rule) |
| Insert: `CTRL-N`, `CTRL-P`, `CTRL-X` | nothing is inserted, and the message callback gets "Completion is not available" |
| Insert: `CTRL-X {key}` | a key that is one of CTRL-X's commands in vim (CTRL-N, CTRL-L, CTRL-U, `s`, CTRL-E …) does nothing more; any other ends CTRL-X mode and does what it does, as in vim |

The keys after CTRL-K and CTRL-\ are read the way CTRL-R's register name is
(previous sections): the state notes that it is owed them and takes them on
its next calls, with `no_mapping` raised meanwhile, as vim raises it. As in
vim, `<Esc>` after CTRL-K or after its first character cancels the digraph
and stays in Insert mode (on the command line, stays on the line); a special
key in the first place is inserted as itself; two characters that are no
digraph give the second. If Insert mode is told to end (`:stopinsert`) while
one of these keys is awaited, it is not awaited any more.

What vim draws while it waits — a `?`, then the first character — is not
reported; an embedder that wants it can look at the last key it sent.

**Not done.** `r CTRL-K {char1} {char2}` and `f`/`t` with a digraph in
Normal mode; CTRL-\ on the command line (`CTRL-\ CTRL-N`, `CTRL-\ e`);
completion itself, which would need the embedder to draw its menu.

**If you relied on the old behavior.** A literal control character is
`CTRL-V` and the key, as in vim. A mapping on CTRL-N, CTRL-P or CTRL-X in
Insert mode still wins: the message is only for the unmapped key.

**Tests.** `apitest/digraph.c`, `apitest/insert_ctrl_bsl.c`. `apitest/compare`: 38 Insert-mode cases differ from vim (58 on 2026-11-12), and what is left of these keys among them is completion itself; Normal, Ex and the rest are as before, and both sweeps come back.

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
