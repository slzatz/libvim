# CLAUDE.md

This file provides guidance to Claude when working with code in this repository.

## Overview

libvim is a headless vim library extracted from vim source code, originally designed for Onivim. It provides vim editing functionality without any TUI elements, using a state machine architecture for non-blocking operation.

**This repository is a permanent fork** of onivim/libvim, which is unmaintained; nothing goes back upstream, and changes are ordinary commits on `main`. Working rules:

- **[PATCHES.md](PATCHES.md) is the list of everything that diverges from upstream**, written for someone embedding the library. A change in behavior or API gets a section there (upstream behavior, this fork, what to do if you relied on the old behavior, tests), and a test in `src/apitest/`. The sections below are the notes for someone *changing* this tree.
- **The consumer is [vimango](https://github.com/slzatz/vimango)**, which may not be the only one: a change must leave an embedder that asks for nothing new unaffected. New behavior is opt-in (`vimSetMappingsEnabled`, `vimSetFileWritesEnabled`, an option value).
- vimango builds its `libvim.a` from this repository's *pushed* `main` (its `scripts/build-libvim.sh` pulls, and refuses a checkout with uncommitted changes). It does not compile against these headers: it declares the part of the API it uses in its own `vim/libvim_api.h`, and a test there (`TestHeaderMatchesLibvim`, run by that script after every build) compares those declarations, and the layout of `pos_T`, `spellError_T` and `searchHighlight_T`, with the headers here. So a change to a function vimango calls, or to one of those types, fails that test until vimango's copy is updated; `src/ex_cmds.h` is copied across by the script.
- **An object file needs its header dependencies listed** in the generated `### Dependencies:` section of `src/Makefile`. Without them a header change does not rebuild it, and the stale object links: `spellfile.o`, compiled against an old `structs.h`, crashed on `zg`. If a header change seems not to take effect, build with `--clean`.

## Build Commands

Use the build scripts. From the repository root:

```bash
./build-macos.sh      # macOS
./build-linux.sh      # Linux
```

No arguments, no flags. Each script verifies its dependencies, runs
`./configure` with the correct flags when needed, and builds `src/libvim.a`.
Re-running is cheap: `./configure` is skipped unless the checkout has never been
configured or the required flags changed, in which case the stale autoconf cache
is cleared automatically.

Two options are accepted: `--clean` to discard objects and rebuild from scratch,
and `--tests` to also build and run the API test suite.

The rest of this section documents what the scripts do, for when something goes
wrong or you need to build by hand.

### Building by hand

A fresh clone must be configured first. `src/auto/config.h` and
`src/auto/config.mk` are generated and are not in the repository, so `make`
alone fails with `no terminal library found` / `checking for tgetent()...
NOT FOUND!`. Configure once per checkout, then `make libvim.a`.

**macOS (Apple Silicon / Homebrew):**

```bash
brew install ncurses gettext
cd src
./configure --disable-selinux --with-tlib=ncurses \
  CFLAGS="-I/opt/homebrew/opt/ncurses/include -I/opt/homebrew/include \
    -Wno-error=implicit-function-declaration -Wno-error=implicit-int \
    -Wno-error=int-conversion -Wno-error=incompatible-function-pointer-types \
    -Wno-error=unused-but-set-variable -Wno-error=deprecated-non-prototype \
    -Wno-error=implicit-int-float-conversion" \
  LDFLAGS="-L/opt/homebrew/opt/ncurses/lib -L/opt/homebrew/lib"
make libvim.a
```

Every `-Wno-error` above is load-bearing on current clang, which rejects legacy
C that this codebase is full of. `-Wno-error=implicit-int-float-conversion` is
the least obvious: without it the build stops at `evalfunc.c:3751` on
`VARNUM_MAX`.

**Linux** (Debian/Ubuntu: `libtinfo-dev libacl1-dev`; Arch: `ncurses acl`):

```bash
cd src
./configure --disable-selinux \
  CFLAGS="-fPIC -Wno-error=implicit-int -Wno-error=implicit-function-declaration"
make libvim.a
```

`-fPIC` so the archive can be linked into a shared object or a Go binary. The
two `-Wno-error` flags are for `./configure`'s probe programs, which GCC 14+
rejects: without `implicit-int` every terminal-library probe fails and
configure stops at `checking for tgetent()... NOT FOUND!` even with ncurses
installed; without `implicit-function-declaration` the SVR4 pty probe fails
silently and `HAVE_SVR4_PTYS` is missing from `auto/config.h`. With both,
`config.h` matches a `-fpermissive` configure (verified on GCC 16.2). The
compile itself needs no extra flags — its GCC 14+ suppressions are in the
Makefile's `WARN_FLAGS`, and the build is warning-free under `-Werror` on
GCC 16.

Both produce `src/libvim.a`, which is what applications link against.

### Reconfiguring an existing checkout

Re-running `./configure` with different flags in a checkout that was already
configured fails before it does any work:

```
configure: error: `CFLAGS' has changed since the previous run:
configure: error: changes in the environment can compromise the build
configure: error: run `make distclean' and/or `rm auto/config.cache' and start over
```

Autoconf caches the previous flags in `src/auto/config.cache` and refuses to
proceed when they differ. Delete it and configure again:

```bash
rm -f src/auto/config.cache
```

This is not hypothetical for this repository: checkouts configured before
`-Wno-error=implicit-int-float-conversion` was added to the macOS recipe above
will hit it the first time anyone follows the current instructions. `make` never
reads `config.cache`, so nothing flags the stale flags until you reconfigure —
and on clang a full rebuild from such a checkout fails at `evalfunc.c:3751`
anyway, for the missing flag the reconfigure was meant to add.

`configure` also prints `test: 21.0.0: integer expression expected` twice on
clang 21. Its GCC-version probe cannot parse clang's version string, so it
answers "no" to the GCC-3-or-later and `-D_FORTIFY_SOURCE` checks. Cosmetic; it
does not affect the generated config.

### Running the API tests

`./build-macos.sh --tests` (or `./build-linux.sh --tests`) builds and runs all
60 of them and reports pass/fail per test. To run a single test by hand:

```bash
cd src
make apitest/visual_mode.test.exe
cd apitest && ./visual_mode.test.exe
```

Tests are compiled with ASan and UBSan. Two things the bare `make` above gets
wrong, both of which the scripts handle:

- Test executables link the library rather than just archiving it, so on macOS
  they also need gettext's lib directory, which the Makefile does not add:
  `EXTRA_LIBS="-L/opt/homebrew/opt/gettext/lib"`, otherwise
  `ld: library 'intl' not found`.
- The rule's last step copies the binary into `$(DEST_BIN)`, which defaults to
  `/bin` and is not writable, so `make` exits non-zero after a successful link.
  Override it with `DEST_BIN=<writable dir>`.

A test program ends with `MU_REPORT(); MU_RETURN();`. `return minunit_status;`
is the status of the *last* test only: ten programs ended that way, and the
scripts printed PASS for them whatever the tests before the last had done.

Keep the suite `-Werror`-clean on current toolchains. `apitest/fileformats.c`
used `sprintf()`, which the macOS SDK marks deprecated, and that alone broke the
full-suite build until it was changed to `snprintf()`; prefer the bounded form
in new tests rather than adding another `-Wno-error` flag.

### Building Individual Files

If you need to compile specific changed files without a full rebuild:

```bash
cd src

# Compile the changed file
gcc -c -I. -Iproto -DHAVE_CONFIG_H -O2 -Wno-unused-but-set-variable -o objects/<file>.o <file>.c

# Update the library archive
ar rv libvim.a objects/<file>.o
```

For example, to rebuild just `normal.c`:

```bash
cd src
gcc -c -I. -Iproto -DHAVE_CONFIG_H -O2 -Wno-unused-but-set-variable -o objects/normal.o normal.c
ar rv libvim.a objects/normal.o
```

### Rebuilding Applications

After rebuilding `libvim.a`, copy it where the consuming application expects it
and rebuild normally. For vimango that is the project root, next to `go.mod`:

```bash
cp src/libvim.a /path/to/vimango/
cd /path/to/vimango
CGO_ENABLED=1 go build --tags=fts5
```

**No `-a` and no `go clean -cache` is needed.** Go hashes the contents of
`libvim.a` into the link action ID, so replacing the archive invalidates the
cached link and the next ordinary `go build` picks it up. Verified on Go 1.26.2
(darwin/arm64) by swapping archives that differ only in whether they export
`sm_push_insert_operator`: building with each in turn produced binaries that
correctly gained and lost the symbol, while rebuilding twice against an
unchanged archive returned a byte-identical cached binary.

Avoid `-a` in particular. It does not just relink; it rebuilds the entire
standard library on every invocation, turning a fast relink into a multi-minute
one, and it fixes nothing here.

If you do suspect a stale binary, confirm it before reaching for a bigger
hammer — check whether the symbol you expect is actually linked in:

```bash
nm ./vimango | grep sm_push_insert_operator
```

## Architecture

### State Machine Pattern

libvim uses a non-blocking state machine architecture instead of vim's original blocking edit loops. Key components:

- **State Machine** (`state_machine.c`): Manages mode transitions via `sm_push_*` functions
- **Normal Mode** (`normal.c`): Contains `state_normal_cmd_execute` and `do_pending_operator`
- **Edit Mode** (`edit.c`): Contains `state_edit_*` functions for insert mode
- **Operations** (`ops.c`): Contains `state_change_*` and `state_insert_*` for visual block operations

### Key Functions

- `sm_push(mode, subMode, context, executeFn, pendingOperatorFn, cleanupFn)` - Push a new mode state
- `sm_push_change(oap)` - Push change operation state (for `c` command)
- `sm_push_insert_operator(oap, count1)` - Push insert operation state (for visual block `I`/`A`)
- `state_*_initialize` - Set up operation context
- `state_*_execute` - Handle keystrokes in that state
- `state_*_cleanup` - Finalize operation (e.g., replicate text in block mode)

### Visual Block Operations

Visual block `I` (insert) and `A` (append) use the state machine pattern:

1. `nv_edit` detects visual mode `I`/`A` and calls `v_visop`
2. `v_visop` translates command and calls `nv_operator` to set operator type
3. `do_pending_operator` handles OP_INSERT/OP_APPEND by calling `sm_push_insert_operator`
4. `state_insert_initialize` positions cursor at block start
5. `state_insert_cleanup` replicates inserted text to all block lines via `block_insert`

## Key Files

- `src/normal.c` - Normal mode command handling, `do_pending_operator`
- `src/ops.c` - Operation implementations including visual block insert/change states
- `src/state_machine.c` - State machine management
- `src/spell.c`, `src/spellfile.c` - Spell checking engine and `.spl`/`.add` file handling
- `src/edit.c` - Insert mode handling
- `src/proto/*.pro` - Function prototypes

## Historical Context

libvim was designed for Onivim which handles multi-cursor editing externally. The original visual block `I`/`A` was disabled (commit 04197cff7) because the blocking `op_insert` function caused hangs. Later, multi-cursor callback support was added (commit 0008a5213) which bypassed the standard vim operator mechanism.

For applications that need standard vim visual block behavior (text replicated on escape), the state machine approach (`sm_push_insert_operator`) provides non-blocking operation while maintaining vim semantics.

## Known gaps

None of these is started.

- **A typed `/pattern`: the wrap warning on the closing `<cr>` is unreliable.** libvim searches again from where the incremental search left the cursor, so it can report a wrap that did not happen and miss one that did. `n`, `N`, `*` and `#` are right. vimango drops the warning for that one key.
- **`` :runtime `cmd` `` segfaults** (outside restricted mode, where the backtick is refused first).
- **`:s` with the `c` flag** has no prompt to confirm with.
- **`<C-r>` in an insert-mode mapping right-hand side** inserts a literal `^R`.
- `vimSetCursorAddCallback()` and its global still exist and are wired up in `libvim.c`; nothing calls them for a blockwise `I`/`A` any more.

## What this fork changed

The whole account is in PATCHES.md. Here: what to know when working on each.

### Mappings (2026-11)

`vimInit` raises `no_mapping` and `no_zero_mapping` so that `:map` stores and never expands; `vimSetMappingsEnabled(TRUE)` lowers them. The counters are adjusted by a delta, never assigned, because vim raises and lowers them itself around single keys (CTRL-V, digraphs, counts) — except in `vimInit`, which assigns the baseline of 1 so that calling it again (test harnesses do) does not stack raises. 

A key that begins a longer mapping (`j` of `jk`) waits in the typeahead buffer. `sm_execute()` appends each new key after it (it used to insert at the front, which lost the first key), so **the typeahead buffer can be non-empty between calls** — `vimInputPending()` says so. vim would give up on the mapping after `'timeoutlen'`; libvim has no clock, so the embedder calls `vimInputTimeout()` when the time is up, and `vgetorpeek()` then runs with `timedout` set. Keys sent with mappings off never wait and take a waiting key with them. `sm_execute_normal()` (`:normal`) still inserts at the front, as vim's `exec_normal_cmd()` does. Tests: `apitest/mapping.c`.

### Search highlights (2026-11)

`vimSearchGetHighlights()` returns every match: the search accepts a match at its starting position on the first call and whenever it resumes at the end of a non-empty match, and moves on first only after an empty one (`^`), so it cannot loop. `vimSearchGetHighlightEnabled()` is vim's own `v:hlsearch`; an embedder asks it rather than tracking `/`, `n` and `:noh`. Tests: `apitest/search_highlights.c`, `search_large_file.c`.

### Messages (2026-11)

`msg_attr_keep()`, where `msg()`, `smsg()`, `msg_attr()` and `give_warning()` all end up, sends the message to the callback (`MSG_INFO`, or `MSG_WARNING` from `give_warning()`), whole, and not at all under `:silent`. An error is sent once, as `MSG_ERROR`, by `emsg_core()`; its second pass through the display path is skipped. `set_keep_msg()` keeps nothing: with no redraw a kept message stayed set, and `msgmore()` then held back the next "N more lines". Tests: `apitest/messages_reports.c`.

### Listings (2026-11)

What vim writes a piece at a time — `msg_puts()`, `msg_putchar()`, `msg_outtrans()`, `msg_advance()`: every listing — comes down to `msg_puts_attr_len()`. There `msg_listing_put()` keeps `msg_col` and `msg_didout` up to date (**nothing else does**; with `msg_col` standing still `msg_advance()` never returns, which was the bare `:let` hang) and, while `do_cmdline()` is running a command, collects the text. `msg_listing_end()` at the outermost `do_cmdline()` sends it as one `MSG_INFO` message; `msg2_send()` flushes first, to keep order. `do_cmdline()` has one exit after its recursion check, and the begin/end pair relies on that.

A new command that says nothing: check `msg_silent`, then whether it is one upstream gave to a callback (`:version`, `:messages` did). One that says something twice sends a message of its own *and* writes to the screen: wrap the screen part in `msg_listing_pause()` / `msg_listing_resume()`, as `ex_echo()` does. Tests: `apitest/listings.c`.

### Visual Block I/A Fix (2026-01)

Restored standard vim behavior for visual block `I` (insert) and `A` (append) commands:

- **Problem**: Visual block `I`/`A` was using a multi-cursor callback approach that bypassed vim's operator mechanism, so inserted text wasn't replicated to all selected lines on Escape.

- **Solution**: Re-enabled `v_visop()` in `nv_edit()` to route through the standard operator mechanism. Modified `do_pending_operator()` to use `sm_push_insert_operator()` instead of the blocking `op_insert()`. Added `state_insert_*` functions in `ops.c` to handle the state machine.

- **Cleanup**: Removed unused `restart_edit_save` variable and related dead code from `do_pending_operator()` in both OP_CHANGE and OP_INSERT/OP_APPEND cases.

- **Breaking change**: The `cursorAddCallback` is no longer called for visual block I/A operations. Applications relying on multi-cursor callbacks for visual block editing will need to adapt to standard vim behavior.

### Spell Checking Restored (2026-11)

Upstream libvim removed vim's spell checking in three commits (`60046fe02`,
`ee5010af6`, `fff5615cb`). It is back, so embedders no longer need an external
checker such as hunspell.

- **Engine**: `spell.c`, `spellfile.c` and `spell.h` are restored unchanged,
  except that the per-window `w_s` pointer was not brought back. libvim has
  no `:ownsyntax`, so `w_s` always equalled `&w_buffer->b_s`; the spell code
  uses `wp->w_buffer->b_s.` directly. Do not reintroduce `w_s`.
- **Not restored**: spell highlighting in `screen.c` (the embedder draws; see
  `vimSpellGetErrors`) and Insert-mode spell completion (`FEAT_INS_EXPAND` is
  off; the `ins_compl_*` calls in `spell.c` are stubbed by macros).
- **API** (`libvim.h`):
  - `vimSpellGetErrors(buf, start, end, &n, &errors)` returns
    `spellError_T { lnum, col, len, type }` for a line range. `col` and `len`
    are **bytes**. `type` is `SPELL_ERROR_BAD`, `_CAPS`, `_RARE` or `_LOCAL`.
    Like vim's display, it reports nothing unless the buffer is shown in a
    window with `'spell'` set and a dictionary loaded. The capital check after
    a sentence end crosses line breaks (`spell_line_needs_cap()`).
  - `vimSpellGetSuggestions(&badWord, &n, &suggestions)` returns what `z=`
    would offer, changing nothing.
  - `vimSetSpellSuggestCallback(cb)`: plain `z=` calls `cb` instead of vim's
    blocking numbered prompt. The embedder shows a menu and sends `{n}z=`,
    which applies suggestion n with undo, `.` and `:spellrepall` support.
    `spell_suggest()` must never be called with a count of 0.
  - The suggestion search is capped by `'spellsuggest'` `timeout:{msec}`
    (vim 9's item, which this 8.1-based code lacked), but as a **work budget,
    not a deadline**: `sps_timeout * SUG_WORK_PER_MSEC` units of 1000 trie-walk
    steps, counted where `suggest_trie_walk()` already checks for CTRL-C, set
    once per `spell_find_suggest()`. A deadline would make the list depend on
    machine load, and `{n}z=` repeats the search, so n could apply a different
    word than the embedder showed. `SUG_WORK_PER_MSEC` (6) was measured on an
    Apple M-series Mac; at `timeout:1000`, 19 common and long real
    misspellings kept their uncapped top three suggestions.
- **`'spelloptions'`** (buffer-local, fork-only values): `noallcaps` makes an
  unknown word with 2+ capitals and no lower-case letter OK; `nomixedcase`
  one with a capital after its first character and a lower-case letter
  (`iOS`, `NASA's`). Checked in `spell_check()` (`spell_caps_exempt()`), only
  for `SP_BAD` results, so
  `zW`-banned words still show and every caller (`vimSpellGetErrors`,
  `spell_move_to` for `]s`/`[s`/`z=`, `spellbadword()`) agrees. Any further
  "don't flag this kind of word" rule belongs there too, as another value.
- **Normal mode**: `]s`, `[s`, `]S`, `[S`, `zg`, `zw`, `zG`, `zW` and
  `zug`/`zuw`/`zuG`/`zuW` work. `zu` needs a third key, which vim read with
  a blocking `plain_vgetc()`; libvim routes it through the previously unused
  `NORMAL_THIRD_CHARACTER` state into `cmdarg_T.extra_char`. Any future
  three-key normal command can use the same path.
- **Embedder setup**: dictionaries are found in `spell/` on `'runtimepath'`;
  this repo ships `runtime/spell/en.utf-8.spl`. Other languages need a
  `.spl`, which `:mkspell` can build from hunspell `.aff`/`.dic` files (vim's
  automatic download relies on netrw, which libvim lacks). Set:
  - `'spelllang'` and `'spell'`, e.g. `set spelllang=en spell`.
  - `'spellfile'` explicitly before `zg`/`zw` are used. When it is empty vim
    writes to the first `spell/` directory on `'runtimepath'`, which may be
    inside the libvim checkout.
  - a `'spellsuggest'` limit such as `best,9`. The default is the window
    height minus 2 (38 in libvim's default 40-line window).
  - a `'spellsuggest'` `timeout:`, e.g. `best,9,timeout:1000`. The suggestion
    search is exponential in the word's length: uncapped, `llllllllllll`
    takes over two minutes and `antidisestablishmentarianizm` three, with the
    embedder frozen. The default is vim 9's 5000.
- **Tests**: `apitest/spell.c` covers the engine, all three API functions and
  the normal-mode commands. It sets `'spellfile'` to a `tempname()` so `zg`
  never writes into `runtime/spell/`.

### File writes can be turned off (2026-11)

`vimSetFileWritesEnabled(FALSE)` makes vim write no file by any route; see
PATCHES.md for the whole of it. What to know when changing this tree:

- **Open files with `mch_open()` / `mch_fopen()`, never bare `open()` /
  `fopen()`.** Those two macros are `libvim_open()` / `libvim_fopen()`
  (`vim.h`, defined in `libvim.c`), which is where the switch is enforced. A
  bare call writes regardless.
- A command whose purpose is to write a file should also call
  `check_file_writes()` first, so the user gets a reason rather than a generic
  "can't open file".
- A file vim keeps for itself and must write even then (so far only the
  `'spellfile'` word list) brackets the write with
  `libvim_own_file_write(TRUE)` / `(FALSE)`.
- `call_shell()` checks `restricted` itself. Do not add a second way to start
  a program that goes around it.
