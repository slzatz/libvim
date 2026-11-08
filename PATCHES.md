# How this fork differs from onivim/libvim

This repository is a permanent fork of
[onivim/libvim](https://github.com/onivim/libvim). Upstream is unmaintained and
nothing here is intended to go back to it, so changes land as ordinary commits
on `master` rather than as a patch queue.

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

## Downstream

Built for [vimango](https://github.com/slzatz/vimango), which links `libvim.a`
statically and wants stock vim keybindings rather than a multi-cursor editor.
Nothing here is vimango-specific — the result is simply closer to vim than
upstream libvim is.
