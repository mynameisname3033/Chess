#include <chrono>
#include <iostream>
#include <string>
#include "action_generator.h"
#include "board.h"
#include "action.h"
#include "action_list.h"
#include "init.h"
#include "bot.h"
#include "zobrist_hash.h"

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

static bool set_legal_action(const board& chess_board, uint16_t& action)
{
	int from = from_sq(action);
	int to = to_sq(action);

	action_list legal_actions = generate_legal_actions(chess_board);
	for (int i = 0; i < legal_actions.count; ++i)
	{
		uint16_t legal_action = legal_actions.actions[i];
		if (from_sq(legal_action) == from && to_sq(legal_action) == to)
		{
			action = legal_action;
			return true;
		}
	}
	return false;
}

static uint16_t user_play(const board& chess_board, const action_list& legal_actions)
{
	while (true)
	{
		cout << "Enter fromto: ";

		char from_file, to_file;
		int from_rank, to_rank;
		cin >> from_file >> from_rank >> to_file >> to_rank;

		int from = (from_rank - 1) * 8 + (from_file - 'a');
		int to = (to_rank - 1) * 8 + (to_file - 'a');

		if (chess_board.side_to_move == BLACK)
		{
			from = 63 - from;
			to = 63 - to;
		}

		cout << "From: " << from << " To: " << to << endl;

		uint16_t action = create_action(from, to, QUIET);
		bool valid = set_legal_action(chess_board, action);

		if (from < 0 || from >= 64 || to < 0 || to >= 64 || !valid)
		{
			cout << "Invalid input. Try again." << endl;
			continue;
		}

		if (is_promo(flags(action)))
		{
			cout << "Enter promo piece (N, B, R, Q): ";
			char promo_char;
			cin >> promo_char;

			int promo_type = char_to_piece(promo_char);
			int promo_flag = promo_piece(promo_type);
			set_action_flags(action, promo_flag);
		}

		return action;
	}
}

int main()
{
	init_zobrist_rng();
	init_action_generator();

	board chess_board = {};
	chess_board.load_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
	//chess_board.load_fen("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8");
	//chess_board.load_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -");

	/*auto start = chrono::steady_clock::now();

	uint64_t n_moves = perft(chess_board, 5);

	auto end = chrono::steady_clock::now();
	auto duration = chrono::duration_cast<chrono::nanoseconds>(end - start);
	cout << "Time taken: " << (duration.count() / 1000000) << " milliseconds" << endl;
	cout << "Total moves: " << n_moves << endl << endl;

	cout << "Pawn time: " << (pawn_time / 1000000) << " ms" << endl;
	cout << "Knight time: " << (knight_time / 1000000) << " ms" << endl;
	cout << "Bishop time: " << (bishop_time / 1000000) << " ms" << endl;
	cout << "Rook time: " << (rook_time / 1000000) << " ms" << endl;
	cout << "Queen time: " << (queen_time / 1000000) << " ms" << endl;
	cout << "King time: " << (king_time / 1000000) << " ms" << endl;
	cout << "Legality check time: " << (legality_check_time / 1000000) << " ms" << endl;*/

	int last_move_to = -1;

	while (true)
	{
		chess_board.print(last_move_to);
		cout << endl;

		if (chess_board.side_to_move == WHITE)
		{
			action_list legal_actions = generate_legal_actions(chess_board);
			if (legal_actions.count == 0)
				return 0;

			uint16_t action = user_play(chess_board, legal_actions);
			chess_board.make_action(action);
			last_move_to = to_sq(action);

			cout << endl;
		}
		else
		{
			action_list legal_actions = generate_legal_actions(chess_board);
			if (legal_actions.count == 0)
				return 0;

			uint16_t action = bot_play(chess_board, legal_actions);
			chess_board.make_action(action);
			last_move_to = to_sq(action);

			cout << endl;
		}
	}

	return 0;
}