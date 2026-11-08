# CLAUDE.md

This file provides guidance to Claude when working with code in this repository.

## Overview

libvim is a headless vim library extracted from vim source code, originally designed for Onivim. It provides vim editing functionality without any TUI elements, using a state machine architecture for non-blocking operation.

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
58 of them and reports pass/fail per test. To run a single test by hand:

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

## Recent Changes

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
