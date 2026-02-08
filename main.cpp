#include <chrono>
#include <iostream>
#include <string>
#include <sstream>
#include "action_generator.h"
#include "board.h"
#include "action.h"
#include "action_list.h"
#include "init.h"
#include "bot.h"
#include "zobrist_hash.h"
#include "lichess_communicator.h"

using namespace std;

static uint64_t perft(const board& chess_board, int depth)
{
	if (depth == 0)
		return 1;

	action_list legal_actions = generate_legal_actions(chess_board);
	uint64_t nodes = 0;

	for (int i = 0; i < legal_actions.count; ++i)
	{
		uint16_t action = legal_actions.actions[i];

		board temp = chess_board;
		temp.make_action(action);

		nodes += perft(temp, depth - 1);
	}

	return nodes;
}

static uint64_t perft_divide(const board& chess_board, int depth)
{
	uint64_t total_nodes = 0;
	action_list legal_actions = generate_legal_actions(chess_board);

	for (int i = 0; i < legal_actions.count; ++i)
	{
		uint16_t action = legal_actions.actions[i];

		board temp = chess_board;
		temp.make_action(action);

		uint64_t nodes = perft(temp, depth - 1);
		cout << action_to_string(action, chess_board.side_to_move) << ", nodes: " << nodes << "\n";

		total_nodes += nodes;
	}

	return total_nodes;
}

static vector<string> split(const string& s)
{
	stringstream ss(s);
	vector<string> tokens;
	string tok;

	while (ss >> tok)
		tokens.push_back(tok);

	return tokens;
}

int main()
{
	init_zobrist_rng();
	init_action_generator();

	board chess_board = {};
	chess_board.load_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

	//chess_board.load_fen("5k1r/1p2n1p1/p1nB1pQ1/2N4p/2B5/1PP1P3/P2P1PPP/RN2K2R w KQ - 12 24");
	//chess_board.load_fen("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8");
	//chess_board.load_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -");
	//chess_board.load_fen("1k6/p4pp1/8/6n1/8/8/pK6/7q b - - 1 52");

	//auto start = chrono::steady_clock::now();

	//uint64_t n_moves = perft_divide(chess_board, 7);

	//auto end = chrono::steady_clock::now();
	//auto duration = chrono::duration_cast<chrono::nanoseconds>(end - start);
	//cout << "Time taken: " << (duration.count() / 1000000) << " milliseconds" << endl;
	//cout << "Total moves: " << n_moves << endl << endl;

	//cout << "Pawn time: " << (pawn_time / 1000000) << " ms" << endl;
	//cout << "Knight time: " << (knight_time / 1000000) << " ms" << endl;
	//cout << "Bishop time: " << (bishop_time / 1000000) << " ms" << endl;
	//cout << "Rook time: " << (rook_time / 1000000) << " ms" << endl;
	//cout << "Queen time: " << (queen_time / 1000000) << " ms" << endl;
	//cout << "King time: " << (king_time / 1000000) << " ms" << endl;
	//cout << "Legality check time: " << (legality_check_time / 1000000) << " ms" << endl;

	ios::sync_with_stdio(false);
	cin.tie(nullptr);

	string input;

	while (getline(cin, input))
	{
		if (input == "uci")
		{
			cout << "id name MyEngine\n";
			cout << "id author Akhil\n";
			cout << "uciok\n" << flush;
		}
		else if (input == "isready")
		{
			cout << "readyok\n" << flush;
		}
		else if (input.rfind("setoption", 0) == 0)
		{
			// ignore options for now
		}
		else if (input == "ucinewgame")
		{
			chess_board.load_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
		}
		else if (input.rfind("position", 0) == 0)
		{
			set_position(chess_board, input);
		}
		else if (input.rfind("go", 0) == 0)
		{
			action_list legal_actions = generate_legal_actions(chess_board);

			if (legal_actions.count == 0)
			{
				cout << "bestmove 0000\n" << flush;
				continue;
			}

			go_params params = parse_go_command(input);
			uint16_t best = bot_play(chess_board, legal_actions, params);
			string best_str = action_to_string(best, chess_board.side_to_move);

			cout << "bestmove " << best_str << "\n" << flush;
		}
		else if (input == "quit")
		{
			break;
		}
	}

	return 0;
}