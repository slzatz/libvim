# CLAUDE.md

This file provides guidance to Claude when working with code in this repository.

## Overview

libvim is a headless vim library extracted from vim source code, originally designed for Onivim. It provides vim editing functionality without any TUI elements, using a state machine architecture for non-blocking operation.

## Build Commands

libvim does not use ncurses or any terminal UI libraries. To build:

```bash
cd src
make
```

This produces `libvim.a` which can be linked into applications.

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

Applications linking to `libvim.a` (e.g., vimango) may cache the library. To ensure changes take effect:

```bash
# For Go applications using cgo
go build -a ./...  # -a forces rebuild of all packages

# Or clean the build cache
go clean -cache
go build ./...
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
