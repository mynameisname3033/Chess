#include <iostream>
#include <limits>
#include <chrono>
#include "bot.h"
#include "action_generator.h"
#include "transposition_table.h"

using namespace std;

static constexpr int ASPIRATION_WINDOW = 50;
static constexpr int MAX_DEPTH = 10;
static constexpr float MAX_TIME = .5;
static constexpr int INF = 1000000000;

static transposition_table tt(1 << 24);

static inline int heuristic(const board& chess_board)
{
	static constexpr int PIECE_VALUES[5] = {100, 300, 320, 500, 900};

	int value = 0;

	for (int piece = 0; piece < PIECE_NB - 1; ++piece)
	{
		uint64_t bb = chess_board.pieces[WHITE][piece];
		value += __popcnt64(bb) * PIECE_VALUES[piece];
	}

	for (int piece = 0; piece < PIECE_NB - 1; ++piece)
	{
		uint64_t bb = chess_board.pieces[BLACK][piece];
		value -= __popcnt64(bb) * PIECE_VALUES[piece];
	}

	return value;
}

static int minimax(const board& chess_board, int depth, int alpha, int beta)
{
	uint64_t key = chess_board.hash;
	const TTEntry* entry = tt.probe(key);
	if (entry && entry->depth_remaining >= depth)
	{
		if (entry->flag == EXACT)
			return entry->score;
		if (entry->flag == LOWERBOUND && entry->score >= beta)
			return entry->score;
		if (entry->flag == UPPERBOUND && entry->score <= alpha)
			return entry->score;
	}

	int alpha_orig = alpha;
	int beta_orig = beta;

	if (depth == 0)
		return heuristic(chess_board);

	int color = chess_board.side_to_move;

	action_list legal_actions = generate_legal_actions(chess_board);
	if (legal_actions.count == 0)
	{
		bool in_check = is_square_attacked(chess_board, chess_board.king_square[color], color ^ 1);
		return in_check ? color == WHITE ? -INF : INF : 0;
	}

	if (entry)
	{
		for (int i = 0; i < legal_actions.count; ++i)
		{
			if (legal_actions.actions[i] == entry->best_action)
			{
				legal_actions.actions[i] = legal_actions.actions[0];
				legal_actions.actions[0] = entry->best_action;
				break;
			}
		}
	}

	int best_score = color == WHITE ? -INF : INF;
	uint16_t best_action = 0;

	for (int i = 0; i < legal_actions.count; ++i)
	{
		uint16_t action = legal_actions.actions[i];

		board temp = chess_board;
		temp.make_action(action);
		int score = minimax(temp, depth - 1, alpha, beta);

		if (color == WHITE)
		{
			if (score > best_score)
			{
				best_score = score;
				best_action = action;
			}

			if (best_score > alpha)
				alpha = best_score;

			if (alpha >= beta)
				break;
		}
		else
		{
			if (score < best_score)
			{
				best_score = score;
				best_action = action;
			}

			if (best_score < beta)
				beta = best_score;

			if (alpha >= beta)
				break;
		}
	}

	uint8_t flag = EXACT;

	if (best_score <= alpha_orig)
		flag = UPPERBOUND;
	else if (best_score >= beta_orig)
		flag = LOWERBOUND;

	tt.add(key, best_action, best_score, depth, flag);

	return best_score;
}

uint16_t bot_play(const board& chess_board, action_list& legal_actions)
{
	auto start = chrono::steady_clock::now();

	int color = chess_board.side_to_move;

	int best_score = color == WHITE ? -INF : INF;
	uint16_t best_action = 0;
	static board temp;

	for (int depth = 1; depth <= MAX_DEPTH; ++depth)
	{
		int current_best_score = color == WHITE ? -INF : INF;
		uint16_t current_best_action = 0;

		if (best_action != 0)
		{
			for (int i = 0; i < legal_actions.count; ++i)
			{
				if (legal_actions.actions[i] == best_action)
				{
					legal_actions.actions[i] = legal_actions.actions[0];
					legal_actions.actions[0] = best_action;
					break;
				}
			}
		}

		for (int i = 0; i < legal_actions.count; ++i)
		{
			uint16_t action = legal_actions.actions[i];

			temp = chess_board;
			temp.make_action(action);
			int score = minimax(temp, depth - 1, -INF, INF);

			if (color == WHITE)
			{
				if (score > current_best_score)
				{
					current_best_score = score;
					current_best_action = action;
				}
			}
			else
			{
				if (score < current_best_score)
				{
					current_best_score = score;
					current_best_action = action;
				}
			}
		}

		best_score = current_best_score;
		best_action = current_best_action;

		auto now = chrono::steady_clock::now();
		auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - start);
		if (elapsed.count() >= MAX_TIME * 1000)
			break;
	}

	if (best_action == 0)
		cerr << "bug";

	auto now = chrono::steady_clock::now();
	auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - start);
	cout << "Time: " << elapsed.count() << " ms" << endl;
	cout << "Transposition table size: " << tt.tt.size();

	return best_action;
}