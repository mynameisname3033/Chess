#include <chrono>
#include <iostream>
#include <string>
#include "action_generator.h"
#include "board.h"
#include "action.h"
#include "action_list.h"

using namespace std;

static uint64_t perft(const board& chess_board, int depth, int color)
{
	if (depth == 0)
		return 1;

	action_list actions = generate_legal_actions(chess_board, color);
	board temp;
	uint64_t nodes = 0;

	for (int i = 0; i < actions.count; ++i)
	{
		uint16_t action = actions.moves[i];
		temp = chess_board;
		temp.make_action(action);
		nodes += perft(temp, depth - 1, !color);
	}

	return nodes;
}

static string action_to_string(uint16_t action, int color)
{
	int from = from_sq(action);
	int to = to_sq(action);

	char file_from = 'a' + (from % 8);
	char rank_from = '1' + (from / 8);
	char file_to = 'a' + (to % 8);
	char rank_to = '1' + (to / 8);

	return string() + file_from + rank_from + file_to + rank_to + ", flags: " + to_string(flags(action));
}

static uint64_t perft_divide(const board& chess_board, int depth, int color)
{
	uint64_t total_nodes = 0;
	action_list actions = generate_legal_actions(chess_board, color);

	for (int i = 0; i < actions.count; ++i)
	{
		uint16_t action = actions.moves[i];

		board temp = chess_board;
		temp.make_action(action);

		uint64_t nodes = perft(temp, depth - 1, !color);
		cout << action_to_string(action, color) << ", nodes: " << nodes << "\n";

		total_nodes += nodes;
	}

	return total_nodes;
}

static bool set_legal_action(const board& chess_board, uint16_t& action, int color)
{
	int from = from_sq(action);
	int to = to_sq(action);

	action_list legal_actions = generate_legal_actions(chess_board, color);
	for (int i = 0; i < legal_actions.count; ++i)
	{
		uint16_t legal_action = legal_actions.moves[i];
		if (from_sq(legal_action) == from && to_sq(legal_action) == to)
		{
			action = legal_action;
			return true;
		}
	}
	return false;
}

int main()
{
	init_action_generator();
	board chess_board = {};
	chess_board.load_fen("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8");
	chess_board.print(true);

	auto start = chrono::steady_clock::now();

	uint64_t n_moves = perft_divide(chess_board, 4, WHITE);

	auto end = chrono::steady_clock::now();
	auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);
	cout << "Time taken: " << duration.count() << " milliseconds" << endl;
	cout << "Total moves: " << n_moves << endl;

	//bool black_to_action = true;

	//while (true)
	//{
	//	chess_board.print(!black_to_action);

	//	cout << "Enter fromto: ";
	//	
	//	char from_file, to_file;
	//	int from_rank, to_rank;
	//	cin >> from_file >> from_rank >> to_file >> to_rank;

	//	int from = (from_rank - 1) * 8 + (from_file - 'a');
	//	int to = (to_rank - 1) * 8 + (to_file - 'a');

	//	if (black_to_action)
	//	{
	//		from = 63 - from;
	//		to = 63 - to;
	//	}

	//	if (from == to)
	//	{
	//		black_to_action = !black_to_action;
	//		continue;
	//	}

	//	cout << "From: " << from << " To: " << to << endl;

	//	uint16_t action = create_action(from, to, QUIET);
	//	int side_to_move = black_to_action ? BLACK : WHITE;
	//	bool valid = set_legal_action(chess_board, action, side_to_move);

	//	if (from < 0 || from >= 64 || to < 0 || to >= 64 || !valid)
	//	{
	//		cout << "Invalid input. Try again." << endl;
	//		continue;
	//	}

	//	if (is_promo(flags(action)))
	//	{
	//		cout << "Enter promo piece (N, B, R, Q): ";
	//		char promo_char;
	//		cin >> promo_char;

	//		int promo_type = piece_char_to_type(promo_char);
	//		int promo_flag = type_promo_piece(promo_type);
	//		set_action_flags(action, promo_flag);
	//	}

	//	chess_board.make_action(action);
	//	black_to_action = !black_to_action;
	//}

	return 0;
}