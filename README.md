# Chess

I’m Akhil, and I started building Ply14 at 14 because I wanted to understand how a chess engine chooses a move. I wrote up the decisions I made along the way, from fitting a move into 16 bits to teaching the engine to evaluate a position. It searches ahead with alpha-beta pruning and evaluates positions with an efficiently updatable neural network (NNUE). You can play against it or use it to analyze a position in a chess GUI.

[Download for Windows x64 (AVX2)](https://github.com/mynameisname3033/Chess/releases/latest/download/Chess-windows-x64-avx2.zip)

Read [Chess Engine Summary](ENGINE.md) for a walkthrough from packing a move into 16 bits to searching candidate moves and evaluating positions with a quantized network.

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

## Build from source

Open `Chess.sln` in Visual Studio with the Desktop development with C++ workload, the MSVC v143 toolset, and the Windows SDK installed. Select **Release** and **x64**, then build the solution. The build copies the network weights from `nn_train` next to `build\x64\Release\Chess.exe`.
