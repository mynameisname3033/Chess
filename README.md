# Chess

I started this C++ chess engine at 14 to explore how a program chooses a move. It searches ahead with alpha-beta pruning and evaluates positions with an efficiently updatable neural network (NNUE). You can play against it or use it to analyze a position in a chess GUI.

[Download for Windows x64 (AVX2)](https://github.com/mynameisname3033/Chess/releases/latest/download/Chess-windows-x64-avx2.zip)

[Read Chess Engine Summary](ENGINE.md) for a walkthrough from packing a move into 16 bits to searching candidate moves and evaluating positions with a quantized network.

## Play against it

1. Download and extract the ZIP on a 64-bit Windows PC with an AVX2-capable CPU. Keep `Chess.exe` and `nnue_params8_q.bin` together.
2. In a UCI-compatible chess GUI such as Cute Chess, open the engine settings, add a new engine, choose UCI, and select the extracted `Chess.exe`.
3. Start a game and select the engine as your opponent. It identifies itself as `MyEngine`.

## Try a search from the terminal

Open PowerShell in the extracted folder and run `.\Chess.exe`. Enter these commands one line at a time:

```text
uci
isready
position startpos moves e2e4
go depth 8
```

Look for `uciok`, then `readyok`. After the search, the line beginning with `bestmove` gives the engine's reply as Black. Enter `quit` to exit.

## Inside the engine

- **Move generation.** The engine represents piece sets as 64-bit bitboards and uses precomputed attack tables for sliding pieces. To test the generator, enter `position startpos` and then `go perft 4`. The expected count is 197,281 legal move sequences. [Move generator](action_generator.h)
- **Search.** Iterative deepening searches the same position at increasing depths. A transposition table lets the engine reuse earlier search results when different move sequences lead to the same position. [Search implementation](search.cpp)
- **Evaluation.** After a move such as e2–e4, the evaluator updates the pawn's contribution to its neural network. A king move changes the reference square used to encode pieces, so the engine rebuilds the accumulator. [Neural evaluator](nnue.h)

## Build from source

Open `Chess.sln` in Visual Studio with the Desktop development with C++ workload, the MSVC v143 toolset, and the Windows SDK installed. Select **Release** and **x64**, then build the solution. The build copies the network weights from `nn_train` next to `build\x64\Release\Chess.exe`.
