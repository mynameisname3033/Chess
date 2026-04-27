#include <iostream>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include "bot.h"
#include "action_generator.h"
#include "transposition_table.h"
#include "nnue.h"
#include "options.h"
#include "action.h"
#include "board.h"
#include "piece.h"
#include "uci_communicator.h"

static constexpr int INF = 33000;
static constexpr int MATE_THRESHOLD = 32000;

static transposition_table tt(TT_SIZE);

static uint16_t killer_actions[MAX_DEPTH + 1][2];
static int history_heuristic[COLOR_NB][64][64];
static uint16_t counteraction_heuristic[COLOR_NB][PIECE_NB][64];
int continuation_history[2][PIECE_NB][64][PIECE_NB][64];

static int LAR_table[218][MAX_DEPTH + 1];

static uint64_t nodes = 0;

static bool search_aborted;
static std::chrono::steady_clock::time_point search_start_time;
static int search_limit_ms;

void bot_reset()
{
	tt.clear();
	memset(history_heuristic, 0, sizeof(history_heuristic));
	memset(counteraction_heuristic, 0, sizeof(counteraction_heuristic));
	memset(continuation_history, 0, sizeof(continuation_history));
}

void init_LAR_table()
{
	for (int i = MIN_LAR_INDEX; i < 218; ++i)
	{
		for (int depth_remaining = MIN_LAR_DEPTH_REMAINING; depth_remaining <= MAX_DEPTH; ++depth_remaining)
		{
			int reduction = 1 + log(depth_remaining) * log(i) / LAR_REDUCTION_DIVISOR;
			reduction = std::min(reduction, depth_remaining);
			LAR_table[i][depth_remaining] = reduction;
		}
	}
}

static inline int heuristic(const NNUE& net, int color)
{
	int score = (int)net.forward(color);

	score = std::min(score, MATE_THRESHOLD);
	score = std::max(score, -MATE_THRESHOLD);

	return score;
}

static int quiescence(const board& chess_board, const NNUE& net, int alpha, int beta, uint16_t prev_action)
{
	++nodes;

	const TTEntry* entry = tt.probe(chess_board.hash);

	if (entry)
	{
		int score = entry->score;

		if (entry->flag == EXACT)
			return score;

		if (entry->flag == LOWERBOUND)
			alpha = std::max(alpha, score);
		else if (entry->flag == UPPERBOUND)
			beta = std::min(beta, score);

		if (alpha >= beta)
			return score;
	}

	int color = chess_board.side_to_move;

	int alpha_orig = alpha;
	int stand_pat = entry && entry->static_eval != -INF ? entry->static_eval : heuristic(net, color);

	if (beta <= stand_pat)
	{
		tt.add(chess_board.hash, 0, beta, stand_pat, 0, LOWERBOUND);
		return beta;
	}

	if (stand_pat > alpha)
		alpha = stand_pat;

	bool root_in_check = is_square_attacked(chess_board, chess_board.king_square[color], color ^ 1);

	action_list legal_actions;
	int scores[218];

	if (root_in_check)
	{
		legal_actions = generate_legal_actions(chess_board);

		if (legal_actions.count == 0)
		{
			return -INF;
		}
	}
	else
	{
		legal_actions = generate_only_tactical_legal_actions(chess_board);
	}

	uint16_t counteraction = 0;
	int (*continuation_values)[PIECE_NB][64] = nullptr;

	if (prev_action != 0)
	{
		int prev_to = to_sq(prev_action);
		int prev_piece = full_piece_piece(chess_board.squares[prev_to]);

		counteraction = counteraction_heuristic[color][prev_piece][prev_to];
		continuation_values = &continuation_history[color][prev_piece][prev_to];
	}

	for (int i = 0; i < legal_actions.count; ++i)
	{
		uint16_t action = legal_actions.actions[i];
		int from = from_sq(action);
		int to = to_sq(action);
		int action_flags = flags(action);

		if (entry && action == entry->best_action)
		{
			scores[i] = 2000000;
		}
		else if (is_promo(action_flags))
		{
			scores[i] = 1000000 + PIECE_VALUES[promo_piece(action_flags)];
		}
		else if (chess_board.squares[to] != 0xFF || action_flags == EN_PASSANT)
		{
			int victim = (action_flags == EN_PASSANT) ? PAWN : full_piece_piece(chess_board.squares[to]);
			int attacker = full_piece_piece(chess_board.squares[from]);
			scores[i] = 800000 + (10 * PIECE_VALUES[victim]) - PIECE_VALUES[attacker];
		}
		else if (action == counteraction)
		{
			scores[i] = 600000;
		}
		else
		{
			scores[i] = history_heuristic[color][from][to];
			if (continuation_values)
				scores[i] += (*continuation_values)[full_piece_piece(chess_board.squares[from])][to];
		}
	}

	for (int i = 0; i < legal_actions.count; ++i)
	{
		int best_index = i;
		int best_score = scores[i];

		for (int j = i + 1; j < legal_actions.count; ++j)
		{
			int score = scores[j];

			if (score > best_score)
			{
				best_index = j;
				best_score = score;
			}
		}

		if (best_score == -INF)
			break;

		std::swap(legal_actions.actions[i], legal_actions.actions[best_index]);
		std::swap(scores[i], scores[best_index]);

		uint16_t action = legal_actions.actions[i];

		board temp_board = chess_board;
		temp_board.make_action(action);

		tt.prefetch(temp_board.hash);

		NNUE temp_net = net;
		temp_net.update(temp_board, chess_board, action);

		int score = -quiescence(temp_board, temp_net, -beta, -alpha, action);

		if (score > alpha)
			alpha = score;

		if (beta <= alpha)
		{
			tt.add(chess_board.hash, 0, beta, stand_pat, 0, LOWERBOUND);
			return beta;
		}
	}

	uint8_t flag = EXACT;
	if (alpha <= alpha_orig)
		flag = UPPERBOUND;
	else if (alpha >= beta)
		flag = LOWERBOUND;

	tt.add(chess_board.hash, 0, alpha, stand_pat, 0, flag);

	return alpha;
}

static int negamax(const board& chess_board, const NNUE& net, int depth_remaining, int alpha, int beta, int depth, uint16_t prev_action, int check_extensions = 0)
{
	if (!(++nodes & 8191))
	{
		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - search_start_time).count();
		if (elapsed >= search_limit_ms)
		{
			search_aborted = true;
			return 0;
		}
	}

	int mate_value = INF - depth;
	if (mate_value <= alpha)
		return alpha;
	if (-mate_value >= beta)
		return beta;

	bool root_is_pv = beta - alpha > 1;

	uint64_t key = chess_board.hash;
	const TTEntry* entry = tt.probe(key);

	if (root_is_pv && !entry && depth_remaining >= MIN_IID_DEPTH_REMAINING)
	{
		negamax(chess_board, net, depth_remaining - IID_DEPTH_REDUCTION, alpha, alpha + 1, depth, prev_action, check_extensions);
		entry = tt.probe(key);
	}

	int color = chess_board.side_to_move;
	bool root_in_check = is_square_attacked(chess_board, chess_board.king_square[color], color ^ 1);

	if (root_in_check && check_extensions < MAX_CHECK_EXTENSIONS && depth < MAX_DEPTH && depth_remaining >= MIN_CHECK_EXTENSION_DEPTH_REMAINING)
	{
		++depth_remaining;
		++check_extensions;
	}

	if (entry && entry->depth_remaining >= depth_remaining)
	{
		int score = entry->score;

		if (score > MATE_THRESHOLD)
			score -= depth;
		else if (score < -MATE_THRESHOLD)
			score += depth;

		if (entry->flag == EXACT)
			return score;
		if (entry->flag == LOWERBOUND)
			alpha = std::max(alpha, score);
		else if (entry->flag == UPPERBOUND)
			beta = std::min(beta, score);

		if (alpha >= beta)
			return score;
	}

	int static_eval = entry && entry->static_eval != -INF ? entry->static_eval : -INF;

	if (!root_is_pv && !root_in_check && depth_remaining <= MAX_RFP_DEPTH_REMAINING)
	{
		if (static_eval == -INF)
			static_eval = heuristic(net, color);
		if (static_eval - RFP_MARGIN_MULTIPLIER * depth_remaining >= beta)
			return static_eval;
	}

	if (!root_is_pv && !root_in_check && depth_remaining >= MIN_NULL_PRUNING_DEPTH_REMAINING)
	{
		bool has_non_pawn_material =
			chess_board.pieces[color][KNIGHT] ||
			chess_board.pieces[color][BISHOP] ||
			chess_board.pieces[color][ROOK] ||
			chess_board.pieces[color][QUEEN];

		if (has_non_pawn_material)
		{
			board temp = chess_board;
			temp.make_null_action();

			int reduction = BASE_NULL_PRUNING_VERIFICATION_REDUCTION + depth_remaining / NULL_PRUNING_VERIFICATION_REDUCTION_DIVISOR;
			int score = -negamax(temp, net, std::max(depth_remaining - 1 - reduction, 0), -beta, -beta + 1, depth + 1, 0);

			if (score >= beta)
			{
				int verify = negamax(chess_board, net, depth_remaining - reduction, beta - 1, beta, depth + 1, 0);
				if (verify >= beta)
					return beta;
			}
		}
	}

	action_list legal_actions = generate_legal_actions(chess_board);

	if (legal_actions.count == 0)
	{
		if (root_in_check)
			return -INF + depth;
		else
			return 0;
	}

	if (depth_remaining == 0)
		return quiescence(chess_board, net, alpha, beta, prev_action);

	int futility_pruning_threshold = -INF;

	if (!root_is_pv && !root_in_check && depth_remaining <= MAX_FP_DEPTH_REMAINING)
	{
		if (static_eval == -INF)
			static_eval = heuristic(net, color);
		futility_pruning_threshold = static_eval + FP_MARGIN_MULTIPLIER * depth_remaining;
	}

	int alpha_orig = alpha;
	int best_score = -INF;
	uint16_t best_action = 0;

	int scores[218];

	uint16_t (&killers)[2] = killer_actions[depth];
	uint16_t* counteraction = nullptr;
	int (*continuation_values)[PIECE_NB][64] = nullptr;

	if (prev_action != 0)
	{
		int prev_to = to_sq(prev_action);
		int prev_piece = full_piece_piece(chess_board.squares[prev_to]);

		counteraction = &counteraction_heuristic[color][prev_piece][prev_to];
		continuation_values = &continuation_history[color][prev_piece][prev_to];
	}

	for (int i = 0; i < legal_actions.count; ++i)
	{
		uint16_t action = legal_actions.actions[i];
		int from = from_sq(action);
		int to = to_sq(action);
		int action_flags = flags(action);

		if (entry && action == entry->best_action)
		{
			scores[i] = 2000000;
		}
		else if (is_promo(action_flags))
		{
			scores[i] = 1000000 + PIECE_VALUES[promo_piece(action_flags)];
		}
		else if (chess_board.squares[to] != 0xFF || action_flags == EN_PASSANT)
		{
			int victim = (action_flags == EN_PASSANT) ? PAWN : full_piece_piece(chess_board.squares[to]);
			int attacker = full_piece_piece(chess_board.squares[from]);
			scores[i] = 800000 + (10 * PIECE_VALUES[victim]) - PIECE_VALUES[attacker];
		}
		else if (action == killers[0])
		{
			scores[i] = 700000;
		}
		else if (action == killers[1])
		{
			scores[i] = 700000 - 1;
		}
		else if (counteraction && action == *counteraction)
		{
			scores[i] = 600000;
		}
		else
		{
			scores[i] = history_heuristic[color][from][to];
			if (continuation_values)
				scores[i] += (*continuation_values)[full_piece_piece(chess_board.squares[from])][to];
		}
	}

	for (int i = 0; i < legal_actions.count; ++i)
	{
		int best_index = i;

		for (int j = i + 1; j < legal_actions.count; ++j)
		{
			if (scores[j] > scores[best_index])
			{
				best_index = j;
			}
		}

		std::swap(legal_actions.actions[i], legal_actions.actions[best_index]);
		std::swap(scores[i], scores[best_index]);

		uint16_t action = legal_actions.actions[i];

		board temp_board = chess_board;
		temp_board.make_action(action);

		tt.prefetch(temp_board.hash);

		NNUE temp_net = net;
		temp_net.update(temp_board, chess_board, action);

		int to = to_sq(action);
		int from = from_sq(action);
		int action_flags = flags(action);

		uint8_t captured = chess_board.squares[to];
		bool is_capture = captured != 0xFF || action_flags == EN_PASSANT;

		bool is_killer = action == killers[0] || action == killers[1];
		bool gives_check = is_square_attacked(temp_board, temp_board.king_square[color ^ 1], color);
		bool quiet = !is_capture && !is_promo(action_flags) && !gives_check;

		if (quiet && !is_killer)
		{
			if ((depth_remaining <= MAX_LAP_DEPTH_REMAINING && i >= BASE_LAP_INDEX + depth_remaining * depth_remaining) ||
				(futility_pruning_threshold != -INF && futility_pruning_threshold < alpha))
				continue;
		}

		int score;

		int new_depth = depth_remaining - 1;

		if (i == 0)
		{
			score = -negamax(temp_board, temp_net, new_depth, -beta, -alpha, depth + 1, action, check_extensions);

			if (search_aborted)
				return 0;
		}
		else
		{
			int reduction = 0;

			if (i >= MIN_LAR_INDEX && depth_remaining >= MIN_LAR_DEPTH_REMAINING && quiet && !is_killer)
				reduction = LAR_table[i][depth_remaining];

			int history = history_heuristic[color][from][to];
			if (history > 0)
				reduction -= 1;
			else if (history < 0)
				reduction += 1;

			if (beta - alpha > 1)
				reduction -= 1;

			int search_depth = new_depth - std::max(0, std::min(reduction, depth_remaining - 1));

			score = -negamax(temp_board, temp_net, search_depth, -alpha - 1, -alpha, depth + 1, action, check_extensions);

			if (search_aborted)
				return 0;

			if (score > alpha)
			{
				score = -negamax(temp_board, temp_net, new_depth, -beta, -alpha, depth + 1, action, check_extensions);

				if (search_aborted)
					return 0;
			}
		}

		if (score > best_score)
		{
			best_score = score;
			best_action = action;
		}

		if (score > alpha)
			alpha = score;

		if (alpha >= beta)
		{
			if (quiet)
			{
				if (killers[0] != action)
				{
					killers[1] = killers[0];
					killers[0] = action;
				}

				int bonus = depth_remaining * depth_remaining;

				int& history_value = history_heuristic[color][from][to];
				history_value += bonus - (history_value * abs(bonus)) / MAX_HEURISTIC_VALUE;

				if (continuation_values)
				{
					int piece = full_piece_piece(chess_board.squares[from]);
					int& continuation_value = (*continuation_values)[piece][to];
					continuation_value += bonus - (continuation_value * abs(bonus)) / MAX_HEURISTIC_VALUE;
				}

				if (counteraction)
					*counteraction = action;
			}

			break;
		}
		else if (quiet)
		{
			int penalty = depth_remaining;

			int& history_value = history_heuristic[color][from][to];
			history_value += -penalty - (history_value * abs(penalty)) / MAX_HEURISTIC_VALUE;

			if (continuation_values)
			{
				int piece = full_piece_piece(chess_board.squares[from]);
				int& continuation_value = (*continuation_values)[piece][to];
				continuation_value += -penalty - (continuation_value * abs(penalty)) / MAX_HEURISTIC_VALUE;
			}
		}
	}

	uint8_t flag = EXACT;
	if (best_score <= alpha_orig)
		flag = UPPERBOUND;
	else if (best_score >= beta)
		flag = LOWERBOUND;

	int tt_score = best_score;
	if (tt_score > MATE_THRESHOLD)
		tt_score += depth;
	else if (tt_score < -MATE_THRESHOLD)
		tt_score -= depth;

	tt.add(key, best_action, tt_score, static_eval, depth_remaining, flag);

	return best_score;
}

static std::pair<int, int> choose_search_limit_ms_and_depth_time_cutoff_percent(const go_params& params, int color)
{
	if (params.movetime != -1)
		return { params.movetime, 100 };

	int time_left_ms = (color == WHITE) ? params.wtime : params.btime;
	int inc_ms = (color == WHITE) ? params.winc : params.binc;

	if (time_left_ms <= 0)
		return { MIN_THINKING_TIME_MS, DEPTH_TIME_CUTOFF_PERCENT };

	float t = (float)time_left_ms;
	float scale = std::pow(t / 60000.0f, 0.5f);
	scale = std::min(1.0f, scale);

	int thinking_time_ms = time_left_ms / TIME_DIVISOR * scale + inc_ms * 0.8f;

	thinking_time_ms = std::min(thinking_time_ms, MAX_THINKING_TIME_MS);
	thinking_time_ms = std::max(thinking_time_ms, MIN_THINKING_TIME_MS);

	return { thinking_time_ms, DEPTH_TIME_CUTOFF_PERCENT };
}

static inline std::string get_pv(board chess_board)
{
	std::string pv;

	for (int i = 0; i < std::min(10, MAX_DEPTH); ++i)
	{
		const TTEntry* entry = tt.probe(chess_board.hash);

		if (!entry || entry->best_action == 0 || entry->flag != EXACT)
			break;

		uint16_t best_move = entry->best_action;
		pv += action_to_string(best_move, chess_board.side_to_move) + " ";

		chess_board.make_action(best_move);
	}

	pv.pop_back();
	return pv;
}

uint16_t get_best_action(const board& chess_board, action_list& legal_actions, const go_params& params)
{
	for (int color = 0; color < COLOR_NB; ++color)
	{
		for (int square1 = 0; square1 < 64; ++square1)
		{
			for (int square2 = 0; square2 < 64; ++square2)
			{
				history_heuristic[color][square1][square2] -= history_heuristic[color][square1][square2] >> 1;

				for (int piece1 = 0; piece1 < PIECE_NB; ++piece1)
				{
					for (int piece2 = 0; piece2 < PIECE_NB; ++piece2)
					{
						continuation_history[color][piece1][square1][piece2][square2] -= continuation_history[color][piece1][square1][piece2][square2] >> 1;
					}
				}
			}
		}
	}

	NNUE net;
	net.build_accumulator(chess_board);
	memset(killer_actions, 0, sizeof(killer_actions));
	nodes = 0;

	search_aborted = false;
	search_start_time = std::chrono::steady_clock::now();
	std::pair<int, int> search_limit_ms_and_depth_time_cutoff_percent = choose_search_limit_ms_and_depth_time_cutoff_percent(params, chess_board.side_to_move);
	search_limit_ms = search_limit_ms_and_depth_time_cutoff_percent.first;
	int depth_time_cutoff_percent = search_limit_ms_and_depth_time_cutoff_percent.second;

	int best_score = -INF;
	uint16_t best_action = 0;
	int depth;

	int color = chess_board.side_to_move;

	for (depth = 1; depth <= MAX_DEPTH; ++depth)
	{
		uint64_t key = chess_board.hash;
		const TTEntry* entry = tt.probe(key);;

		int scores[218];

		for (int i = 0; i < legal_actions.count; ++i)
		{
			uint16_t action = legal_actions.actions[i];
			int from = from_sq(action);
			int to = to_sq(action);
			int action_flags = flags(action);

			if (action == best_action)
			{
				scores[i] = 3000000;
			}
			else if (entry && action == entry->best_action)
			{
				scores[i] = 2000000;
			}
			else if (is_promo(action_flags))
			{
				scores[i] = 1000000 + PIECE_VALUES[promo_piece(action_flags)];
			}
			else if (chess_board.squares[to] != 0xFF || action_flags == EN_PASSANT)
			{
				int victim = (action_flags == EN_PASSANT) ? PAWN : full_piece_piece(chess_board.squares[to]);
				int attacker = full_piece_piece(chess_board.squares[from]);
				scores[i] = 800000 + (10 * PIECE_VALUES[victim]) - PIECE_VALUES[attacker];
			}
			else
			{
				scores[i] = history_heuristic[color][from][to];
			}
		}

		int current_best_score = -INF;
		uint16_t current_best_action = legal_actions.count > 0 ? legal_actions.actions[0] : 0;

		int alpha = best_score - ASPIRATION_WINDOW;
		int beta = best_score + ASPIRATION_WINDOW;

		while (true)
		{
			int alpha_orig = alpha;
			int beta_orig = beta;

			current_best_score = -INF;
			current_best_action = legal_actions.count > 0 ? legal_actions.actions[0] : 0;

			int search_alpha = alpha;
			int search_beta = beta;

			int window = ASPIRATION_WINDOW;

			for (int i = 0; i < legal_actions.count; ++i)
			{
				int best_index = i;

				for (int j = i + 1; j < legal_actions.count; ++j)
				{
					if (scores[j] > scores[best_index])
					{
						best_index = j;
					}
				}

				std::swap(legal_actions.actions[i], legal_actions.actions[best_index]);
				std::swap(scores[i], scores[best_index]);

				uint16_t action = legal_actions.actions[i];

				board temp_board = chess_board;
				temp_board.make_action(action);

				tt.prefetch(temp_board.hash);

				NNUE temp_net = net;
				temp_net.update(temp_board, chess_board, action);

				int score;

				if (i == 0)
				{
					score = -negamax(temp_board, temp_net, depth - 1, -search_beta, -search_alpha, 1, action);
				}
				else
				{
					score = -negamax(temp_board, temp_net, depth - 1, -search_alpha - 1, -search_alpha, 1, action);

					if (score > search_alpha)
						score = -negamax(temp_board, temp_net, depth - 1, -search_beta, -search_alpha, 1, action);
				}

				if (score > current_best_score)
				{
					current_best_score = score;
					current_best_action = action;
				}

				search_alpha = std::max(search_alpha, score);
			}

			if (current_best_score <= alpha_orig)
			{
				alpha -= window;
				window *= 2;
			}
			else if (current_best_score >= beta_orig)
			{
				beta += window;
				window *= 2;
			}
			else
			{
				break;
			}
		}

		if (search_aborted)
			break;

		best_score = current_best_score;
		best_action = current_best_action;

		auto now = std::chrono::steady_clock::now();
		auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - search_start_time);

		uint64_t nps = 0;
		if (total_elapsed.count() > 0)
			nps = nodes * 1000 / total_elapsed.count();

		if (abs(best_score) > MATE_THRESHOLD)
		{
			int mate_in = (INF - abs(best_score) + 1) / 2;
			if (best_score < 0)
				mate_in = -mate_in;

			std::cout << "info depth " << depth
				<< " score mate " << mate_in
				<< " nodes " << nodes
				<< " nps " << nps
				<< " time " << total_elapsed.count()
				<< " hashfull " << (1000 * tt.used) / TT_SIZE
				<< "\n";
		}
		else
		{
			std::cout << "info depth " << depth
				<< " score cp " << best_score
				<< " nodes " << nodes
				<< " nps " << nps
				<< " time " << total_elapsed.count()
				<< " hashfull " << (1000 * tt.used) / TT_SIZE
				<< "\n";
		}

		if (total_elapsed.count() >= search_limit_ms * depth_time_cutoff_percent / 100)
			break;
	}

	if (best_action == 0)
		return legal_actions.actions[0];

	return best_action;
}