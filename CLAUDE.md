# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

This is a Visual Studio 2022 (MSVC v143) C++ project targeting Windows x64.

- **Build via Visual Studio**: Open `Chess.sln` and build with Ctrl+Shift+B
- **Build via MSBuild**: `msbuild Chess.sln /p:Configuration=Release /p:Platform=x64`
- **Run**: The output binary is a UCI chess engine; pipe UCI commands to it or connect it to a GUI (e.g., Arena, Cute Chess) or the `lichess-bot` Python wrapper in the `lichess-bot/` subdirectory

There are no automated tests — correctness is verified via `perft` (move count validation). To run perft, uncomment the relevant lines in `main.cpp` around `perft_divide(chess_board, N)`.

## Architecture

This is a UCI-compatible chess engine with NNUE evaluation. The main loop in `main.cpp` reads UCI commands from stdin and dispatches to the appropriate subsystems.

**Board representation** (`board.h`): Bitboard-based. `board` struct holds per-color, per-piece `uint64_t` bitboards, an `occupied` bitboard, a flat `squares[64]` array for O(1) piece lookup, Zobrist hash, repetition stack, and castling/en-passant state. `make_action()` is the core mutating function; there is no unmake — callers copy the board before making a move.

**Move generation** (`action_generator.h`, `action.h`): Actions are packed into `uint16_t` with from/to squares and flags. `generate_legal_actions()` returns an `action_list`. Move flags distinguish quiet, captures, promotions, castling, double pawn push, and en passant.

**Search** (`search.cpp`, `search.h`): Iterative deepening alpha-beta with:
- Transposition table (`transposition_table.h`)
- Null move pruning, reverse futility pruning (RFP), futility pruning (FP)
- Late action reduction (LAR) with a precomputed `LAR_table[218][MAX_DEPTH+1]`
- Late action pruning (LAP)
- Aspiration windows
- Internal iterative deepening (IID)
- Check extensions
- Move ordering via killer moves, history heuristic, counteraction heuristic, continuation history, and SEE

**Evaluation** (`nnue.h`, `parameters.h`, `init.cpp`): NNUE (Efficiently Updatable Neural Network). The network weights are loaded at startup from a binary file — the path is hardcoded in `main.cpp`: `C:/Users/akhil/c++/repos/Chess/nn_train/nnue_params8.bin`. The accumulator is maintained incrementally per perspective.

**UCI interface** (`uci_communicator.h/.cpp`): Parses `position`, `go`, and `setoption` commands. All tunable search parameters are exposed as UCI options (see `options.h`, `options.cpp`) and stored in `option_map`.

**Initialization** (`init.h`, `init.cpp`, `zobrist_hash.h/.cpp`): Must call `init_parameters()`, `init_zobrist_rng()`, `init_action_generator()`, `init_LAR_table()`, and `reset_engine()` before using the engine.

## Key constraints

- The NNUE weights path in `main.cpp:71` is an absolute local path — update it when running on a different machine.
- AVX2 intrinsics are used in `nnue.h`; the target machine must support AVX2.
- `__forceinline` and `__popcnt64`/`_tzcnt_u64` are MSVC/Windows intrinsics — the code is not portable to GCC/Clang without changes.
