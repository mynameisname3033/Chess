# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

This is a Visual Studio C++ project. Build using Visual Studio 2022 or MSBuild:

```
msbuild Chess.sln /p:Configuration=Release /p:Platform=x64
```

Or open `Chess.sln` in Visual Studio and build from there. The output executable goes to `Chess\x64\Release\` or `Chess\x64\Debug\`.

There are no automated tests — correctness is validated manually via perft (move generation) and UCI interaction.

## Running

The engine communicates via UCI protocol over stdin/stdout. On startup it loads an NNUE weights file hardcoded in `main.cpp`:

```cpp
init_parameters("C:/Users/akhil/c++/repos/Chess/nn_train/nnue_params8.bin");
```

Update this path if running on a different machine. To test perft or specific positions, uncomment the relevant lines in `main.cpp` before building.

## Architecture

This is a UCI chess engine with NNUE evaluation, written as a single-threaded C++ program.

### Core data structures
- **`board`** (`board.h`) — The central game state. Uses bitboards (`uint64_t pieces[COLOR][PIECE]`), an 8x8 `squares[]` mailbox, Zobrist hash, repetition stack, castling rights, and en passant square. `make_action()` and `make_null_action()` are inline and mutate the board in place (callers copy before calling if they need to undo).
- **`action`** (`action.h`) — Moves are packed into `uint16_t`: 6 bits from-square, 6 bits to-square, 4 bits flags (castling, en passant, promotions, double pawn push).
- **`NNUE`** (`nnue.h`) — Efficiently-updated neural network for position evaluation. Uses king-bucketed embeddings (22532 total, 384-dim), two hidden layers (32 units each), AVX2 SIMD for inference. The accumulator is updated incrementally as pieces move. Weights are loaded from a binary file via `init_parameters()`.

### Move generation
- **`action_generator.h`** / **`init.h`** / **`init.cpp`** — Magic bitboard move generation for sliders (bishops, rooks, queens). Attack tables for all piece types are precomputed at startup via `init_action_generator()`. `generate_legal_actions()` returns an `action_list` (fixed-size array + count).
- **`action_picker.h`** — Staged move ordering: TT move first, then captures ordered by SEE/MVV-LVA, then killers and counter-moves, then quiet moves ordered by history heuristics.

### Search
- **`search.cpp`** / **`search.h`** — Iterative deepening alpha-beta with:
  - Aspiration windows
  - Late move reductions (LMR) via precomputed `LAR_table`
  - Late move pruning (LMP) via `LAP` table
  - Null move pruning with verification
  - Reverse futility pruning (RFP) and futility pruning (FP)
  - Check extensions
  - Internal iterative deepening (IID)
  - Quiescence search with SEE pruning
  - Killer moves (3 slots per ply), history heuristic, counter-move heuristic, 2-ply continuation history
- **`transposition_table.h`** — Fixed-size TT (2^25 clusters of 4 entries). Uses Zobrist key for lookup; entries store best action, score, static eval, depth, and bound type (EXACT/LOWER/UPPER).

### UCI interface
- **`uci_communicator.h/.cpp`** — Parses `position` and `go` commands. `set_position()` applies moves to the board. `parse_go_command()` returns time controls as `go_params`.
- **`options.h/.cpp`** — All tunable search parameters (time management, pruning margins, reduction divisors) are exposed as UCI `option` entries and stored as globals accessed via `option_map`. Changing them at runtime calls `init_LAR_table()` to recompute reduction tables.
- **`main.cpp`** — UCI loop: handles `uci`, `isready`, `setoption`, `ucinewgame`, `position`, `go`, `stop`, `ponderhit`, `quit`. Also contains `perft`/`perft_divide` for move generation testing (commented out by default). The **search runs on a worker thread** while the main thread keeps reading stdin, so `stop`/`ponderhit`/`isready` work mid-search. Only one worker runs at a time; board/option-mutating commands (`position`, `ucinewgame`, `setoption`) call `join_search()` first to avoid racing the `board`. All engine→GUI output goes through `uci_send()` (mutex-guarded) so the worker's `info`/`bestmove` lines can't race or interleave with the main thread under `sync_with_stdio(false)`.

### Pondering
UCI ponder is supported (`go ponder` → `ponderhit`/`stop`); the engine emits `bestmove X ponder Y`. Cross-thread control is via two atomics in `search.cpp` (`g_stop`, `g_ponder`), set only by the main thread via `prepare_search()` (before the worker launches), `stop_search()`, and `ponderhit()`; the worker only reads them. While pondering the search ignores time limits; on `ponderhit` the clock restarts and the search converts to a normal timed search, keeping all work done. The transition is enforced in both the per-8191-node poll (`should_abort`) and the ID loop so a mid-iteration `ponderhit` can't overrun. The ponder move (`get_ponder_action`) is captured when `best_action` is committed for a completed depth — not read from `pv_table` at the end, which an aborted iteration can clobber. Note: `setoption` only parses integer values (guarded), since the `Ponder` check option carries `true`/`false`.

### Evaluation
- **`parameters.h/.cpp`** — Loads/frees NNUE weight arrays from a binary file. Network shape: embeddings (22532×384) → accumulator (768) → FC1 (768→32) → FC2 (32→32) → FC3 (32→1).
- **`zobrist_hash.h/.cpp`** — Zobrist keys for pieces, castling rights, en passant file, and side to move. `zobrist_hash()` computes a full hash; incremental updates are done inside `board::make_action()`.
- **`bits.h`** — Bit manipulation utilities used throughout move generation and search.

### lichess-bot integration
The `lichess-bot/` subdirectory is a separate Python project (its own git repo) that connects the engine to Lichess via their Bot API.
