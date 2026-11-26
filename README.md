# Chess

I’m Akhil, and I started building Ply14 at 14 because I wanted to understand how a chess engine chooses a move. I wrote up the decisions I made along the way, from fitting a move into 16 bits to teaching the engine to evaluate a position. It searches ahead with alpha-beta pruning and evaluates positions with an efficiently updatable neural network (NNUE). You can play against it or use it to analyze a position in a chess GUI.

[Download for Windows x64 (AVX2)](https://github.com/mynameisname3033/Chess/releases/latest/download/Chess-windows-x64-avx2.zip)

Read [Ply14 Details](Ply14%20Details.md) for a walkthrough from packing a move into 16 bits to searching candidate moves and evaluating positions with a quantized network.

## Play against it

1. Download and extract the ZIP on a 64-bit Windows PC with an AVX2-capable CPU. Keep `Chess.exe` and `nnue_params8_q.bin` together.
2. In a UCI-compatible chess GUI such as Cute Chess, open the engine settings, add a new engine, choose UCI, and select the extracted `Chess.exe`.
3. Start a game and select the engine as your opponent. It identifies itself as `MyEngine`.

## Playing strength

Ply14 played 1,328 local games against six engines at 10 seconds per side plus 0.1 seconds per move. Each opening was played twice, with colors reversed. The CCRL 40/15 column gives each opponent's reference rating. All match results are from Ply14's perspective, with draws worth half a point.

| Opponent | CCRL 40/15 | Games | Wins–draws–losses | Score | Elo difference (approx. 95% interval) |
|---|---:|---:|---:|---:|---:|
| Lozza 11.0 | 3250 | 272 | 51–129–92 | 42.5% | −53 ± 30 |
| KnightX 5.0 | 3206 | 64 | 46–13–5 | 82.0% | +264 ± 111 |
| Pingu 5.0.0 | 3187 | 256 | 89–119–48 | 58.0% | +56 ± 29 |
| Strelka 5.5 | 3140 | 608 | 248–164–196 | 54.3% | +30 ± 23 |
| Admete 1.9.0 | 3103 | 64 | 38–19–7 | 74.2% | +184 ± 74 |
| Amoeba 3.4 | 3042 | 64 | 43–16–5 | 79.7% | +237 ± 78 |

Match lengths varied because testing stopped early when the evidence favored either engine strongly enough. Overall, the results place the engine at 3170–3245 realistically. Note that these are estimates run from my own computer.

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
