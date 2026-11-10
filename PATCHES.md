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

**Known limits.** Single-key left-hand sides only. A partially matched
multi-key mapping (`inoremap jk <Esc>`) leaves its first key waiting in the
typeahead buffer, and `sm_execute()` inserts each later key at offset 0 —
*ahead* of it — so the pair never completes and the first key is lost.
Appending instead (offset `typebuf.tb_len`, as `feedkeys()` does) is the likely
fix, but it changes every input path and has not been tried. Separately,
`<C-r>` in an insert-mode right-hand side inserts a literal `^R`; that is
libvim's insert state, not mapping.

**Tests.** `apitest/mapping.c`: not expanded by default, normal and visual
mappings expanded when enabled (including an ex right-hand side), disabling
stops expansion, enable is idempotent, and counts and `0` are unaffected.

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
fell through to `msg_puts_attr_len()`, which ends in a `TODO` and drops it: "5
lines yanked", "4 fewer lines", "5 more lines", "3 lines >ed 1 time", "search
hit BOTTOM, continuing at TOP", what undo and redo did, the count from `:s`,
`ga`, `g CTRL-G`.

**This fork.** `msg_attr_keep()`, where `msg()`, `smsg()`, `msg_attr()` and
`give_warning()` all end up, sends the message to the callback: `MSG_INFO`, or
`MSG_WARNING` when it came through `give_warning()`. It is sent whole, before
the truncation vim does for a screen, and not at all under `:silent`. An error
is still sent once, as `MSG_ERROR`: `emsg_core()` displays the error through the
same path after sending it, and that second pass is skipped.

`check_visual_highlight()` no longer warns "terminal cannot highlight" the
first time Visual mode starts. There is no terminal.

**If you relied on the old behavior.** A callback that counted every message
now counts these too — a file load reports `"name" 3L, 115C`, for instance. The
callback gets the priority, so count `MSG_ERROR` if errors are what you mean.

**Known gap.** On the `<cr>` that completes a typed `/pattern`, the wrap
warning is unreliable: libvim searches again from where the incremental search
left the cursor, so it can report a wrap that did not happen and miss one that
did. `n`, `N`, `*` and `#` report wraps correctly.

Tests: `apitest/messages_reports.c`.

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
