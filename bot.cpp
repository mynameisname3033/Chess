#include <iostream>
#include <limits>
#include <chrono>
#include <algorithm>
#include "bot.h"
#include "action_generator.h"
#include "transposition_table.h"
#include "lichess_communicator.h"
#include "action.h"

using namespace std;

static constexpr int MAX_THINKING_TIME_MS = 1500;
static constexpr int MIN_THINKING_TIME_MS = 20;
static constexpr int TIME_DIVISOR = 30;
static constexpr int MAX_DEPTH = 50;

static constexpr int ORDERED_ACTIONS = 8;

static constexpr int INF = 1000000000;
static constexpr int PIECE_VALUES[6] = { 100, 300, 320, 500, 900, 20000 };

static transposition_table tt(1 << 24);
static uint16_t killer_actions[MAX_DEPTH + 1][2];

static inline int evaluate_action(const board& chess_board, uint16_t action)
{
	int from = from_sq(action);
	int to = to_sq(action);

	int action_flags = flags(action);
	int attacker_piece = full_piece_piece(chess_board.squares[from]);

	if (is_promo(action_flags))
	{
		int promo = promo_piece(action_flags);
		return 100000 + PIECE_VALUES[promo];
	}

	if (action_flags == EN_PASSANT)
	{
		return 50000;
	}

	uint8_t captured = chess_board.squares[to];

	if (captured != 0xFF)
	{
		int victim_piece = full_piece_piece(captured);
		return 10000 + (PIECE_VALUES[victim_piece] * 10) - PIECE_VALUES[attacker_piece];
	}

	if (action_flags == CASTLE_WHITE_KINGSIDE || action_flags == CASTLE_WHITE_QUEENSIDE ||
		action_flags == CASTLE_BLACK_KINGSIDE || action_flags == CASTLE_BLACK_QUEENSIDE)
	{
		return 2000;
	}

	return 0;
}

static inline int heuristic(const board& chess_board)
{
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

static int minimax(const board& chess_board, int remaining_depth, int alpha, int beta, int max_depth)
{
	uint64_t key = chess_board.hash;

	const TTEntry* entry = tt.probe(key);
	if (entry && entry->depth_remaining >= remaining_depth)
	{
		if (entry->flag == EXACT)
			return entry->score;
		if (entry->flag == LOWERBOUND && entry->score >= beta)
			return entry->score;
		if (entry->flag == UPPERBOUND && entry->score <= alpha)
			return entry->score;
	}

	if (remaining_depth == 0)
		return heuristic(chess_board);

	int alpha_orig = alpha;
	int beta_orig = beta;

	int color = chess_board.side_to_move;
	int depth = max_depth - remaining_depth;

	action_list legal_actions = generate_legal_actions(chess_board);

	if (legal_actions.count == 0)
	{
		bool in_check = is_square_attacked(chess_board, chess_board.king_square[color], color ^ 1);
		return in_check ? (color == WHITE ? -INF + depth : INF - depth) : 0;
	}

	int scores[218];

	uint16_t killer_action1 = killer_actions[depth][0];
	uint16_t killer_action2 = killer_actions[depth][1];

	for (int i = 0; i < legal_actions.count; ++i)
	{
		uint16_t action = legal_actions.actions[i];

		if (entry && action == entry->best_action)
			scores[i] = INF;
		else if (action == killer_action1 || action == killer_action2)
			scores[i] = INF - 1;
		else
			scores[i] = evaluate_action(chess_board, action);
	}

	int limit = min(legal_actions.count, ORDERED_ACTIONS);

	for (int i = 0; i < limit; ++i)
	{
		int best_index = i;

		for (int j = i + 1; j < legal_actions.count; ++j)
		{
			if (scores[j] > scores[best_index])
				best_index = j;
		}

		if (best_index != i)
		{
			swap(scores[i], scores[best_index]);
			swap(legal_actions.actions[i], legal_actions.actions[best_index]);
		}
	}

	int best_score = (color == WHITE) ? -INF : INF;
	uint16_t best_action = 0;

	for (int i = 0; i < legal_actions.count; ++i)
	{
		uint16_t action = legal_actions.actions[i];

		board temp = chess_board;
		temp.make_action(action);

		int score = minimax(temp, remaining_depth - 1, alpha, beta, max_depth);

		if (color == WHITE)
		{
			if (score > best_score)
			{
				best_score = score;
				best_action = action;
			}

			if (best_score > alpha)
				alpha = best_score;
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
		}

		if (alpha >= beta)
		{
			int to = to_sq(action);
			int f = flags(action);
			uint8_t captured = chess_board.squares[to];

			if (captured == 0xFF && !is_promo(f) && f != EN_PASSANT)
			{
				if (killer_actions[depth][0] != action)
				{
					killer_actions[depth][1] = killer_actions[depth][0];
					killer_actions[depth][0] = action;
				}
			}

			break;
		}
	}

	uint8_t flag = EXACT;

	if (best_score <= alpha_orig)
		flag = UPPERBOUND;
	else if (best_score >= beta_orig)
		flag = LOWERBOUND;

	tt.add(key, best_action, best_score, remaining_depth, flag);

	return best_score;
}

static int choose_think_time_ms(int color, const go_params& params)
{
	if (params.movetime != -1)
		return params.movetime;

	int time_left_ms = (color == WHITE) ? params.wtime : params.btime;
	int inc_ms = (color == WHITE) ? params.winc : params.binc;

	if (time_left_ms <= 0)
		return MIN_THINKING_TIME_MS;

	int thinking_time_ms = time_left_ms / TIME_DIVISOR + inc_ms;

	thinking_time_ms = min(thinking_time_ms, MAX_THINKING_TIME_MS);
	thinking_time_ms = max(thinking_time_ms, MIN_THINKING_TIME_MS);

	return thinking_time_ms;
}

uint16_t bot_play(const board& chess_board, action_list& legal_actions, const go_params& params)
{
	int thinking_time = choose_think_time_ms(chess_board.side_to_move, params);
	int color = chess_board.side_to_move;
	memset(killer_actions, 0, sizeof(killer_actions));

	auto start = chrono::steady_clock::now();

	int best_score = color == WHITE ? -INF : INF;
	uint16_t best_action = 0;
	static board temp;
	int depth;

	for (depth = 1; depth <= MAX_DEPTH; ++depth)
	{
		int current_best_score = color == WHITE ? -INF : INF;
		uint16_t current_best_action = 0;

		int scores[218];

		uint16_t killer_action1 = killer_actions[depth][0];
		uint16_t killer_action2 = killer_actions[depth][1];

		for (int i = 0; i < legal_actions.count; ++i)
		{
			uint16_t action = legal_actions.actions[i];

			if (action == best_action)
				scores[i] = INF;
			else if (action == killer_action1 || action == killer_action2)
				scores[i] = INF - 1;
			else
				scores[i] = evaluate_action(chess_board, action);
		}

		int limit = min(legal_actions.count, ORDERED_ACTIONS);

		for (int i = 0; i < limit; ++i)
		{
			int best_index = i;

			for (int j = i + 1; j < legal_actions.count; ++j)
			{
				if (scores[j] > scores[best_index])
					best_index = j;
			}

			if (best_index != i)
			{
				swap(scores[i], scores[best_index]);
				swap(legal_actions.actions[i], legal_actions.actions[best_index]);
			}
		}

		for (int i = 0; i < legal_actions.count; ++i)
		{
			uint16_t action = legal_actions.actions[i];

			temp = chess_board;
			temp.make_action(action);

			int score = minimax(temp, depth - 1, -INF, INF, depth);

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

		if (abs(best_score) > INF - 1000)
		{
			int mate_in = (INF - abs(best_score) + 1) / 2 - 1;
			if (best_score < 0) mate_in = -mate_in;
			cout << "info depth " << depth << " score mate " << mate_in << " time " << elapsed.count() << "\n";
		}
		else
		{
			cout << "info depth " << depth << " score cp " << best_score << " time " << elapsed.count() << "\n";
		}

		if (elapsed.count() >= thinking_time)
			break;
	}

	if (best_action == 0)
		cerr << "bug";

	return best_action;
}