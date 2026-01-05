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
gcc -c -I. -Iproto -DHAVE_CONFIG_H -O2 -Wno-unused-but-set-variable -o objects/<file>.o <file>.c
ar rv libvim.a objects/<file>.o
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
