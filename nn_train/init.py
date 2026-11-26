import chess
import chess.pgn
import chess.engine
import random
import multiprocessing as mp
import time

PGN_PATH = "dataset.pgn"
OUT_PATH = "labels.txt"
CHECKPOINT_PATH = "pgn_checkpoint.txt"
STOCKFISH_PATH = r"C:/Users/akhil/Downloads/stockfish-windows-x86-64-avx2/stockfish/stockfish-windows-x86-64-avx2.exe"

MAX_GAMES = 100_000_000
MOVE_SAMPLE_PROB = 0.12

EVAL_NODES_SHALLOW = 1_500
EVAL_NODES_DEEP = 15_000

BATCH_SIZE = 4096
NUM_WORKERS = 14
STOCKFISH_THREADS_PER_WORKER = 1

def eval_loop(input_queue, output_queue):
    engine = chess.engine.SimpleEngine.popen_uci(STOCKFISH_PATH)
    engine.configure({"Threads": STOCKFISH_THREADS_PER_WORKER, "Hash": 128})
    nonvolatile_positions_evaluated = 0

    try:
        while True:
            batch = input_queue.get()
            if batch is None:
                break

            for fen in batch:
                board = chess.Board(fen)

                try:
                    shallow_res = engine.analyse(board, chess.engine.Limit(nodes=EVAL_NODES_SHALLOW))
                    final_res = engine.analyse(board, chess.engine.Limit(nodes=EVAL_NODES_DEEP))
                except (chess.engine.EngineError, chess.engine.EngineTerminatedError):
                    continue

                shallow_score_obj = shallow_res.get("score")
                final_score_obj = final_res.get("score")

                if shallow_score_obj is None or final_score_obj is None:
                    continue

                # Quiet == the deep search's best move is not a capture. An empty PV
                # means no legal move (stalemate), which is not a position we want to
                # train on either -- previously those slipped through, since a stalemate
                # has no legal captures.
                final_pv = final_res.get("pv")
                if not final_pv:
                    continue

                if board.is_capture(final_pv[0]) or final_pv[0].promotion:
                    continue

                shallow_cp = shallow_score_obj.relative.score(mate_score=100_000)
                final_cp = final_score_obj.relative.score(mate_score=100_000)

                if abs(final_cp - shallow_cp) > 100:
                    continue

                cp_eval = final_score_obj.white().score(mate_score=100_000)

                if abs(cp_eval) > 2000:
                    continue

                output_queue.put((fen, cp_eval))
                nonvolatile_positions_evaluated += 1

                if nonvolatile_positions_evaluated % 2000 == 0:
                    print(f"[Worker-{mp.current_process().pid}] Nonvolatile positions evaluated: {nonvolatile_positions_evaluated}")

    finally:
        engine.quit()
        output_queue.put(None)

def writer_process(output_queue):
    seen = set()

    try:
        with open(OUT_PATH, "r", encoding="utf-8") as labels:
            for line in labels:
                parts = line.split("\t")
                if parts:
                    fen = parts[0]
                    dedup_key = " ".join(fen.split(" ")[:4])
                    seen.add(dedup_key)
    except FileNotFoundError:
        pass

    finished_workers = 0
    positions_written = 0

    with open(OUT_PATH, "a", encoding="utf-8") as out:
        while True:
            item = output_queue.get()
            if item is None:
                finished_workers += 1
                if finished_workers == NUM_WORKERS:
                    break
                continue

            fen, cp_eval = item
            dedup_key = " ".join(fen.split(" ")[:4])
            if dedup_key in seen:
                continue
            seen.add(dedup_key)

            out.write(f"{fen}\t{cp_eval}\n")
            positions_written += 1
            if positions_written % 5000 == 0:
                print(f"[Writer] Positions written: {positions_written}")

def main():
    mp.set_start_method("spawn", force=True)
    input_queue = mp.Queue(maxsize=100_000)
    output_queue = mp.Queue(maxsize=50_000)

    workers = []
    for _ in range(NUM_WORKERS):
        p = mp.Process(target=eval_loop, args=(input_queue, output_queue))
        p.start()
        workers.append(p)

    writer = mp.Process(target=writer_process, args=(output_queue,))
    writer.start()

    games_done = 0
    fens_sent = 0
    start = time.time()
    batch = []
    start_offset = 0

    try:
        with open(CHECKPOINT_PATH, "r") as f:
            start_offset = int(f.readline().strip())
            games_done = int(f.readline().strip())
        print(f"[Main] Resuming from byte {start_offset}, game {games_done}")
    except:
        print("[Main] Starting from beginning.")

    with open(PGN_PATH, "r", encoding="utf-8") as pgn:
        pgn.seek(start_offset)
        while games_done < MAX_GAMES:
            game = chess.pgn.read_game(pgn)
            if game is None:
                print("Finished PGN file.")
                break

            games_done += 1
            board = game.board()

            for move in game.mainline_moves():
                board.push(move)

                if random.random() > MOVE_SAMPLE_PROB:
                    continue

                if board.is_checkmate() or board.is_check():
                    continue

                # Quietness is decided in the worker from the deep search's best move,
                # not here. Rejecting every position that merely *has* a legal capture
                # discards ~70% of plies and halves middlegame representation while
                # tripling endgames; "best move is not a capture" keeps the phase
                # distribution within a few percent of the true one.
                fen = board.fen()
                batch.append(fen)

                if len(batch) >= BATCH_SIZE:
                    while input_queue.qsize() > 20_000 / BATCH_SIZE:
                        time.sleep(0.05)

                    input_queue.put(batch)
                    batch = []

                fens_sent += 1

            if games_done % 5000 == 0:
                pos = pgn.tell()
                with open(CHECKPOINT_PATH, "w") as f:
                    f.write(f"{pos}\n{games_done}\n")
                end = time.time()
                print(f"[Main] Games processed: {games_done}, FENs queued: {fens_sent}, Input queue size: {input_queue.qsize()}, Time: {end - start:.2f}s")

        if batch:
            input_queue.put(batch)
            fens_sent += len(batch)

    print("[Main] Finished parsing PGN. Sending shutdown signals...")
    for _ in range(NUM_WORKERS):
        input_queue.put(None)

    for p in workers:
        p.join()
    writer.join()
    print(f"Done! All jobs complete. Games processed: {games_done}")

if __name__ == "__main__":
    main()