#include <iostream>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string.h>
#include <sstream>
#include <tuple>
#include <atomic>
#include <thread>
#include <mutex>
#include "search.h"
#include "action_generator.h"
#include "transposition_table.h"
#include "nnue.h"
#include "options.h"
#include "action.h"
#include "board.h"
#include "piece.h"
#include "bits.h"
#include "init.h"
#include "uci_communicator.h"
#include "action_picker.h"

static uint16_t pv_table[MAX_DEPTH + 1][MAX_DEPTH + 1];
static int pv_length[MAX_DEPTH + 1];

static transposition_table tt;

static uint16_t killer_actions[MAX_DEPTH + 1][3];
static int history_heuristic[COLOR_NB][64][64];

static int LAR_table[218][MAX_DEPTH + 1];

static uint16_t counteraction_heuristic_1[COLOR_NB][PIECE_NB][64];
static uint16_t counteraction_heuristic_2[COLOR_NB][PIECE_NB][64];

static int continuation_history_1[COLOR_NB][PIECE_NB][64][PIECE_NB][64];
static int continuation_history_2[COLOR_NB][PIECE_NB][64][PIECE_NB][64];

static int capture_history[COLOR_NB][PIECE_NB][64][PIECE_NB];

static constexpr int CORR_SIZE = 16384;
static int correction_history[COLOR_NB][CORR_SIZE];

static int max_seldepth = 0;

static uint64_t nodes = 0;
static bool search_aborted;
static std::chrono::steady_clock::time_point search_start_time;
static int search_hard_limit_ms;

static std::atomic<bool> g_stop { false };
static std::atomic<bool> g_ponder { false };
static int real_hard_limit_ms;
static bool was_pondering;
static uint16_t ponder_action;

static std::mutex output_mutex;

void uci_send(const std::string& line)
{
	std::lock_guard<std::mutex> lock(output_mutex);
	std::cout << line << std::endl;
}

void prepare_search(bool ponder)
{
	g_stop.store(false);
	g_ponder.store(ponder);
}

void stop_search() { g_stop.store(true); }
void ponderhit() { g_ponder.store(false); }
uint16_t get_ponder_action() { return ponder_action; }

static __forceinline void check_ponder_transition()
{
	if (was_pondering && !g_ponder.load(std::memory_order_relaxed))
	{
		was_pondering = false;
		search_start_time = std::chrono::steady_clock::now();
		search_hard_limit_ms = real_hard_limit_ms;
	}
}

static __forceinline bool should_abort()
{
	if (g_stop.load(std::memory_order_relaxed))
		return true;

	if (g_ponder.load(std::memory_order_relaxed))
		return false;

	check_ponder_transition();

	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - search_start_time).count();
	return elapsed >= search_hard_limit_ms;
}

void reset_engine()
{
	tt.clear();

	memset(history_heuristic, 0, sizeof(history_heuristic));

	memset(counteraction_heuristic_1, 0, sizeof(counteraction_heuristic_1));
	memset(counteraction_heuristic_2, 0, sizeof(counteraction_heuristic_2));

	memset(continuation_history_1, 0, sizeof(continuation_history_1));
	memset(continuation_history_2, 0, sizeof(continuation_history_2));

	memset(capture_history, 0, sizeof(capture_history));

	memset(correction_history, 0, sizeof(correction_history));
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

static __forceinline int get_static_eval(const board& chess_board, const NNUE& net)
{
	int color = chess_board.side_to_move;
	int score = (int)net.forward(color);

	int idx = chess_board.pawn_king_hash & (CORR_SIZE - 1);
	score += correction_history[color][idx] / CORR_GRAIN;

	score = std::min(score, MATE_THRESHOLD);
	score = std::max(score, -MATE_THRESHOLD);

	return score;
}

static __forceinline int SEE(const board& chess_board, uint16_t action)
{
	int from = from_sq(action);
	int to = to_sq(action);
	int action_flags = flags(action);

	int victim = PAWN;
	if (action_flags != EN_PASSANT)
	{
		uint8_t sq_content = chess_board.squares[to];
		if (sq_content == 0xFF)
			victim = 0;
		else
			victim = full_piece_piece(sq_content);
	}

	int attacker = full_piece_piece(chess_board.squares[from]);

	uint64_t occupied = chess_board.occupied[WHITE] | chess_board.occupied[BLACK];

	int gain[32];
	int depth = 0;

	int initial_victim_value = (victim == 0) ? 0 : PIECE_VALUES[victim];

	if (is_promo(action_flags))
	{
		int promo = promo_piece(action_flags);
		initial_victim_value += PIECE_VALUES[promo] - PIECE_VALUES[PAWN];
		attacker = promo;
	}

	gain[depth++] = initial_victim_value;

	occupied &= ~(1ull << from);

	if (action_flags == EN_PASSANT)
	{
		int side = chess_board.side_to_move;
		int ep_captured_sq = (side == WHITE) ? (to - 8) : (to + 8);
		occupied &= ~(1ull << ep_captured_sq);
	}

	int color = chess_board.side_to_move ^ 1;

	while (depth <= 31)
	{
		int from_square = -1;
		int attacker_piece = -1;

		if (uint64_t p_attackers = pawn_attacks[!color][to] & chess_board.pieces[color][PAWN] & occupied)
		{
			from_square = lsb(p_attackers);
			attacker_piece = PAWN;
		}
		else if (uint64_t n_attackers = knight_attacks[to] & chess_board.pieces[color][KNIGHT] & occupied)
		{
			from_square = lsb(n_attackers);
			attacker_piece = KNIGHT;
		}
		else if (uint64_t b_attackers = bishop_attacks[to][((occupied & bishop_masks[to]) * bishop_magics[to]) >> bishop_shifts[to]] & chess_board.pieces[color][BISHOP] & occupied)
		{
			from_square = lsb(b_attackers);
			attacker_piece = BISHOP;
		}
		else if (uint64_t r_attackers = rook_attacks[to][((occupied & rook_masks[to]) * rook_magics[to]) >> rook_shifts[to]] & chess_board.pieces[color][ROOK] & occupied)
		{
			from_square = lsb(r_attackers);
			attacker_piece = ROOK;
		}
		else if (uint64_t q_attackers = (bishop_attacks[to][((occupied & bishop_masks[to]) * bishop_magics[to]) >> bishop_shifts[to]] | rook_attacks[to][((occupied & rook_masks[to]) * rook_magics[to]) >> rook_shifts[to]]) & chess_board.pieces[color][QUEEN] & occupied)
		{
			from_square = lsb(q_attackers);
			attacker_piece = QUEEN;
		}
		else if (uint64_t k_attackers = king_attacks[to] & chess_board.pieces[color][KING] & occupied)
		{
			from_square = lsb(k_attackers);
			attacker_piece = KING;
		}

		if (from_square == -1)
			break;

		gain[depth++] = PIECE_VALUES[attacker];

		attacker = attacker_piece;
		occupied &= ~(1ull << from_square);
		color ^= 1;
	}

	while (--depth > 0)
	{
		gain[depth - 1] = gain[depth - 1] - std::max(0, gain[depth]);
	}

	return gain[0];
}

static __forceinline int capture_history_score(const board& chess_board, int color, uint16_t action)
{
	int from = from_sq(action);
	int to = to_sq(action);
	int moving_piece = full_piece_piece(chess_board.squares[from]);
	int captured_piece = (flags(action) == EN_PASSANT) ? PAWN : full_piece_piece(chess_board.squares[to]);

	return capture_history[color][moving_piece][to][captured_piece] / CAPTURE_HISTORY_SEE_SCORE_DIVISOR;
}

static int quiescence(const board& chess_board, const NNUE& net, int alpha, int beta, int relative_depth, int absolute_depth, uint16_t prev_action_1, int prev_piece_1, uint16_t prev_action_2, int prev_piece_2)
{
	if (absolute_depth > max_seldepth)
	{
		max_seldepth = absolute_depth;
	}

	if (!(++nodes & 8191) && should_abort())
	{
		search_aborted = true;
		return 0;
	}

	if (chess_board.is_draw())
		return 0;

	const TTEntry* entry = tt.probe(chess_board.hash);

	if (absolute_depth >= MAX_QUIESCENCE_DEPTH)
	{
		return entry && entry->static_eval != -INF ? entry->static_eval : get_static_eval(chess_board, net);
	}

	bool can_search_checks = relative_depth <= MAX_QUIET_QUIESCENCE_CHECK_DEPTH;
	int can_search_checks_flag = can_search_checks ? 1 : 0;

	if (entry && (!entry->is_quiescence || entry->depth_remaining >= can_search_checks_flag))
	{
		int score = entry->score;

		if (score > MATE_THRESHOLD)
			score -= absolute_depth;
		else if (score < -MATE_THRESHOLD)
			score += absolute_depth;

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

	bool root_in_check = is_square_attacked(chess_board, chess_board.king_square[color], color ^ 1);
	int stand_pat = entry && entry->static_eval != -INF ? entry->static_eval : get_static_eval(chess_board, net);

	if (!root_in_check)
	{
		if (beta <= stand_pat)
		{
			tt.add(chess_board.hash, 0, stand_pat, stand_pat, can_search_checks_flag, LOWERBOUND, true);
			return stand_pat;
		}

		if (stand_pat > alpha)
			alpha = stand_pat;
	}

	action_list legal_actions;

	if (root_in_check)
	{
		legal_actions = generate_legal_actions(chess_board);

		if (legal_actions.count == 0)
		{
			return -INF + absolute_depth;
		}
	}
	else
	{
		legal_actions = generate_only_tactical_legal_actions(chess_board, can_search_checks);
	}

	uint16_t counteraction_1 = 0;
	uint16_t counteraction_2 = 0;
	int (*continuation_values_1)[PIECE_NB][64] = nullptr;
	int (*continuation_values_2)[PIECE_NB][64] = nullptr;

	if (prev_action_1 != 0)
	{
		int prev_to_1 = to_sq(prev_action_1);
		counteraction_1 = counteraction_heuristic_1[color][prev_piece_1][prev_to_1];
		continuation_values_1 = &continuation_history_1[color][prev_piece_1][prev_to_1];
	}

	if (prev_action_2 != 0)
	{
		int prev_to_2 = to_sq(prev_action_2);
		counteraction_2 = counteraction_heuristic_2[color][prev_piece_2][prev_to_2];
		continuation_values_2 = &continuation_history_2[color][prev_piece_2][prev_to_2];
	}

	uint16_t best_action = 0;
	uint16_t tt_action = entry ? entry->best_action : 0;
	action_picker picker(legal_actions, tt_action);

	uint16_t action;

	while ((action = picker.next([&](int* scores, int start, int end)
		{
			for (int i = start; i < end; ++i)
			{
				uint16_t current_action = legal_actions.actions[i];
				int from = from_sq(current_action);
				int to = to_sq(current_action);
				int action_flags = flags(current_action);

				if (is_promo(action_flags))
				{
					scores[i] = 1000000 + PIECE_VALUES[promo_piece(action_flags)];
				}
				else if (chess_board.squares[to] != 0xFF || action_flags == EN_PASSANT)
				{
					int score = SEE(chess_board, current_action);
					if (score < 0)
						scores[i] = -BIG_INF;
					else
						scores[i] = 800000 + score + capture_history_score(chess_board, color, current_action);
				}
				else if (current_action == counteraction_1)
				{
					scores[i] = 600000;
				}
				else if (current_action == counteraction_2)
				{
					scores[i] = 500000;
				}
				else
				{
					scores[i] = history_heuristic[color][from][to];

					int moving_piece = full_piece_piece(chess_board.squares[from]);

					if (continuation_values_1)
						scores[i] += (*continuation_values_1)[moving_piece][to];
					if (continuation_values_2)
						scores[i] += (*continuation_values_2)[moving_piece][to];
				}
			}
		})) != 0)

	{
		if (picker.scored && picker.scores[picker.current_index - 1] == -BIG_INF)
			break;

		int from = from_sq(action);
		board temp_board = chess_board;
		temp_board.make_action(action);

		int moving_piece = full_piece_piece(chess_board.squares[from]);

		tt.prefetch(temp_board.hash);

		NNUE temp_net = net;
		temp_net.update(temp_board, chess_board, action);

		int score = -quiescence(temp_board, temp_net, -beta, -alpha, relative_depth + 1, absolute_depth + 1, action, moving_piece, prev_action_1, prev_piece_1);

		if (score > alpha)
		{
			alpha = score;
			best_action = action;
		}

		if (beta <= alpha)
		{
			break;
		}
	}

	uint8_t flag;
	if (alpha <= alpha_orig)
		flag = UPPERBOUND;
	else if (alpha >= beta)
		flag = LOWERBOUND;
	else
		flag = EXACT;

	int16_t tt_score = alpha;
	if (tt_score > MATE_THRESHOLD)
		tt_score += absolute_depth;
	else if (tt_score < -MATE_THRESHOLD)
		tt_score -= absolute_depth;

	tt.add(chess_board.hash, best_action, tt_score, stand_pat, can_search_checks_flag, flag, true);

	return alpha;
}

static int negamax(const board& chess_board, const NNUE& net, int depth_remaining, int alpha, int beta, int depth, uint16_t prev_action_1, int prev_piece_1, uint16_t prev_action_2, int prev_piece_2, int check_extensions = 0, uint16_t excluded_action = 0)
{
	if (depth > max_seldepth)
	{
		max_seldepth = depth;
	}

	if (!(++nodes & 8191) && should_abort())
	{
		search_aborted = true;
		return 0;
	}

	if (chess_board.is_draw())
		return 0;

	pv_length[depth] = depth;

	if (depth >= MAX_DEPTH)
		return quiescence(chess_board, net, alpha, beta, 1, depth, prev_action_1, prev_piece_1, prev_action_2, prev_piece_2);

	int mate_value = INF - depth;
	if (mate_value <= alpha)
		return alpha;
	if (-mate_value >= beta)
		return beta;

	bool root_is_pv = beta - alpha > 1;

	uint64_t key = chess_board.hash;
	const TTEntry* entry = tt.probe(key);

	bool tt_hit = entry && !entry->is_quiescence;
	int tt_score = tt_hit ? entry->score : -INF;
	uint16_t tt_action = tt_hit ? entry->best_action : 0;
	int tt_depth = tt_hit ? entry->depth_remaining : 0;
	int tt_bound = tt_hit ? entry->flag : EXACT;

	if (tt_hit)
	{
		if (tt_score > MATE_THRESHOLD)
			tt_score -= depth;
		else if (tt_score < -MATE_THRESHOLD)
			tt_score += depth;
	}

	if (root_is_pv && (!entry || entry->is_quiescence) && depth_remaining >= MIN_IID_DEPTH_REMAINING)
	{
		negamax(chess_board, net, depth_remaining - IID_DEPTH_REDUCTION, alpha, beta, depth, prev_action_1, prev_piece_1, prev_action_2, prev_piece_2, check_extensions);
		entry = tt.probe(key);
		tt_hit = entry && !entry->is_quiescence;
		if (tt_hit)
		{
			tt_score = entry->score;
			tt_action = entry->best_action;
			tt_depth = entry->depth_remaining;
			tt_bound = entry->flag;
			if (tt_score > MATE_THRESHOLD) tt_score -= depth;
			else if (tt_score < -MATE_THRESHOLD) tt_score += depth;
		}
	}

	int color = chess_board.side_to_move;
	bool root_in_check = is_square_attacked(chess_board, chess_board.king_square[color], color ^ 1);

	if (root_in_check && check_extensions < MAX_CHECK_EXTENSIONS && depth < MAX_DEPTH && depth_remaining >= MIN_CHECK_EXTENSION_DEPTH_REMAINING)
	{
		++depth_remaining;
		++check_extensions;
	}

	if (tt_hit && tt_depth >= depth_remaining && excluded_action == 0 && !root_is_pv)
	{
		if (tt_bound == EXACT)
			return tt_score;
		if (tt_bound == LOWERBOUND)
			alpha = std::max(alpha, tt_score);
		else if (tt_bound == UPPERBOUND)
			beta = std::min(beta, tt_score);

		if (alpha >= beta)
			return tt_score;
	}

	int static_eval = entry && entry->static_eval != -INF ? entry->static_eval : -INF;
	bool eval_is_computed = (static_eval != -INF);

	if (!root_is_pv && !root_in_check && excluded_action == 0 && depth_remaining >= MIN_NULL_PRUNING_DEPTH_REMAINING && (chess_board.pieces[color][KNIGHT] || chess_board.pieces[color][BISHOP] || chess_board.pieces[color][ROOK] || chess_board.pieces[color][QUEEN]))
	{
		if (!eval_is_computed)
		{
			static_eval = get_static_eval(chess_board, net);
			eval_is_computed = true;
		}

		if (static_eval >= beta)
		{
			board temp = chess_board;
			temp.make_null_action();

			int reduction = BASE_NULL_PRUNING_VERIFICATION_REDUCTION + depth_remaining / NULL_PRUNING_VERIFICATION_REDUCTION_DIVISOR;
			int score = -negamax(temp, net, std::max(depth_remaining - 1 - reduction, 0), -beta, -beta + 1, depth + 1, 0, 0, 0, 0, check_extensions);

			if (score >= beta)
			{
				int verify = negamax(chess_board, net, depth_remaining - reduction, beta - 1, beta, depth, prev_action_1, prev_piece_1, prev_action_2, prev_piece_2, check_extensions);
				if (verify >= beta)
				{
					tt.add(key, 0, beta, static_eval, depth_remaining, LOWERBOUND, false);
					return beta;
				}
			}
		}
	}


	if (depth_remaining == 0)
		return quiescence(chess_board, net, alpha, beta, 1, depth, prev_action_1, prev_piece_1, prev_action_2, prev_piece_2);

	action_list legal_actions = generate_legal_actions(chess_board);

	if (legal_actions.count == 0)
	{
		if (root_in_check)
			return -INF + depth;
		else
			return 0;
	}

	if (!root_is_pv && !root_in_check && excluded_action == 0 && depth_remaining <= MAX_RAZOR_DEPTH_REMAINING)
	{
		if (!eval_is_computed)
		{
			static_eval = get_static_eval(chess_board, net);
			eval_is_computed = true;
		}

		if (static_eval + RAZOR_MARGIN * depth_remaining <= alpha)
		{
			int razor_score = quiescence(chess_board, net, alpha, beta, 1, depth, prev_action_1, prev_piece_1, prev_action_2, prev_piece_2);
			if (razor_score <= alpha)
				return razor_score;
		}
	}

	if (!root_is_pv && !root_in_check && excluded_action == 0 && depth_remaining <= MAX_RFP_DEPTH_REMAINING)
	{
		if (!eval_is_computed)
		{
			static_eval = get_static_eval(chess_board, net);
			eval_is_computed = true;
		}

		if (static_eval - RFP_MARGIN_MULTIPLIER * depth_remaining >= beta)
		{
			tt.add(key, 0, static_eval, static_eval, depth_remaining, LOWERBOUND, false);
			return static_eval;
		}
	}

	int futility_pruning_threshold = -INF;

	if (!root_is_pv && !root_in_check && excluded_action == 0 && depth_remaining <= MAX_FP_DEPTH_REMAINING)
	{
		if (!eval_is_computed)
		{
			static_eval = get_static_eval(chess_board, net);
			eval_is_computed = true;
		}

		futility_pruning_threshold = static_eval + FP_MARGIN_MULTIPLIER * depth_remaining;
	}

	int extension = 0;
	if (excluded_action == 0 && depth_remaining >= MIN_SE_DEPTH_REMAINING && tt_action != 0 && tt_depth >= depth_remaining - SE_DEPTH_REMAINING_MIN_REDUCTION && tt_bound != UPPERBOUND && abs(tt_score) < MATE_THRESHOLD)
	{
		int singular_margin = depth_remaining * SE_MARGIN_MULTIPLIER;
		int singular_beta = tt_score - singular_margin;
		singular_beta = std::max(singular_beta, -INF + 1);

		int singular_depth = (depth_remaining - SE_REDUCTION_BASE) / SE_REUCTION_DIVISOR;
		int singular_score = negamax(chess_board, net, singular_depth, singular_beta - 1, singular_beta, depth, prev_action_1, prev_piece_1, prev_action_2, prev_piece_2, check_extensions, tt_action);
		if (singular_score < singular_beta - SE_DOUBLE_EXTENSION_MARGIN)
		{
			extension = 2;
		}
		else if (singular_score < singular_beta)
		{
			extension = 1;
		}
	}

	int alpha_orig = alpha;
	int best_score = -INF;
	uint16_t best_action = 0;

	uint16_t (&killers)[3] = killer_actions[depth];

	uint16_t* counteraction_1 = nullptr;
	uint16_t* counteraction_2 = nullptr;

	int (*continuation_values_1)[PIECE_NB][64] = nullptr;
	int (*continuation_values_2)[PIECE_NB][64] = nullptr;

	if (prev_action_1 != 0)
	{
		int prev_to_1 = to_sq(prev_action_1);
		counteraction_1 = &counteraction_heuristic_1[color][prev_piece_1][prev_to_1];
		continuation_values_1 = &continuation_history_1[color][prev_piece_1][prev_to_1];
	}

	if (prev_action_2 != 0)
	{
		int prev_to_2 = to_sq(prev_action_2);
		counteraction_2 = &counteraction_heuristic_2[color][prev_piece_2][prev_to_2];
		continuation_values_2 = &continuation_history_2[color][prev_piece_2][prev_to_2];
	}

	action_picker picker(legal_actions, tt_action);

	int action_count = 0;
	uint16_t action;

	while ((action = picker.next([&](int* scores, int start, int end)
		{
			for (int i = start; i < end; ++i)
			{
				uint16_t current_action = legal_actions.actions[i];
				int from = from_sq(current_action);
				int to = to_sq(current_action);
				int action_flags = flags(current_action);

				if (is_promo(action_flags))
				{
					scores[i] = 1000000 + PIECE_VALUES[promo_piece(action_flags)];
				}
				else if (chess_board.squares[to] != 0xFF || action_flags == EN_PASSANT)
				{
					int score = SEE(chess_board, current_action);
					if (score < 0)
						scores[i] = -10000 + score + capture_history_score(chess_board, color, current_action);
					else
						scores[i] = 800000 + score + capture_history_score(chess_board, color, current_action);
				}
				else if (current_action == killers[0]) { scores[i] = 700000; }
				else if (current_action == killers[1]) { scores[i] = 700000 - 1; }
				else if (current_action == killers[2]) { scores[i] = 700000 - 2; }
				else if (counteraction_1 && current_action == *counteraction_1) { scores[i] = 600000; }
				else if (counteraction_2 && current_action == *counteraction_2) { scores[i] = 600000 - 1; }
				else
				{
					scores[i] = history_heuristic[color][from][to];

					int moving_piece = full_piece_piece(chess_board.squares[from]);

					if (continuation_values_1)
						scores[i] += (*continuation_values_1)[moving_piece][to];
					if (continuation_values_2)
						scores[i] += (*continuation_values_2)[moving_piece][to];
				}
			}
		})) != 0)

	{
		if (action == excluded_action)
			continue;

		int i = action_count++;

		int to = to_sq(action);
		int from = from_sq(action);
		int action_flags = flags(action);

		board temp_board = chess_board;
		temp_board.make_action(action);

		uint8_t captured = chess_board.squares[to];
		bool is_capture = captured != 0xFF || action_flags == EN_PASSANT;

		bool is_killer = action == killers[0] || action == killers[1] || action == killers[2];
		bool gives_check = is_square_attacked(temp_board, temp_board.king_square[color ^ 1], color);
		bool quiet = !is_capture && !is_promo(action_flags) && !gives_check;

		if (quiet && !is_killer && i > 0 && !root_is_pv)
		{
			if ((depth_remaining <= MAX_LAP_DEPTH_REMAINING && i >= BASE_LAP_INDEX + depth_remaining * depth_remaining) ||
				(futility_pruning_threshold != -INF && futility_pruning_threshold < alpha))
				continue;
		}

		int moving_piece = full_piece_piece(chess_board.squares[from]);
		bool is_pv = beta - alpha > 1;

		tt.prefetch(temp_board.hash);

		NNUE temp_net = net;
		temp_net.update(temp_board, chess_board, action);

		int score;

		int local_extension = (action == tt_action) ? extension : 0;
		int new_depth = depth_remaining - 1 + local_extension;

		if (i == 0)
		{
			score = -negamax(temp_board, temp_net, new_depth, -beta, -alpha, depth + 1, action, moving_piece, prev_action_1, prev_piece_1, check_extensions);

			if (search_aborted)
				return 0;
		}
		else
		{
			int reduction = 0;

			if (i >= MIN_LAR_INDEX && depth_remaining >= MIN_LAR_DEPTH_REMAINING && quiet && !is_killer)
				reduction = LAR_table[i][depth_remaining];

			int history = history_heuristic[color][from][to];
			reduction -= history / HISTORY_REDUCTION_DIVISOR;
			if (!is_killer && history < -HEURISTIC_REDUCTION_THRESHOLD)
				++reduction;
			else if (history > HEURISTIC_REDUCTION_THRESHOLD)
				--reduction;

			int continuation = 0;
			if (continuation_values_1)
				continuation += (*continuation_values_1)[moving_piece][to];
			if (continuation_values_2)
				continuation += (*continuation_values_2)[moving_piece][to];
			reduction -= continuation / CONTINUATION_REDUCTION_DIVISOR;
			if (!is_killer && continuation < -HEURISTIC_REDUCTION_THRESHOLD)
				++reduction;
			else if (continuation > HEURISTIC_REDUCTION_THRESHOLD)
				--reduction;

			if (is_capture && !is_promo(action_flags))
			{
				int captured_piece = (action_flags == EN_PASSANT) ? PAWN : full_piece_piece(captured);
				int capture = capture_history[color][moving_piece][to][captured_piece];
				reduction -= capture / CAPTURE_REDUCTION_DIVISOR;
				if (capture < -HEURISTIC_REDUCTION_THRESHOLD)
					++reduction;
				else if (capture > HEURISTIC_REDUCTION_THRESHOLD)
					--reduction;
			}

			if (is_pv)
				--reduction;

			if (gives_check)
				--reduction;

			int search_depth = new_depth - std::max(0, std::min(reduction, new_depth));

			score = -negamax(temp_board, temp_net, search_depth, -alpha - 1, -alpha, depth + 1, action, moving_piece, prev_action_1, prev_piece_1, check_extensions);

			if (search_aborted)
				return 0;

			if (score > alpha)
			{
				score = -negamax(temp_board, temp_net, new_depth, -beta, -alpha, depth + 1, action, moving_piece, prev_action_1, prev_piece_1, check_extensions);

				if (search_aborted)
					return 0;
			}
		}

		if (score > best_score)
		{
			best_score = score;
			best_action = action;
		}

		bool raised_alpha = false;

		if (score > alpha)
		{
			alpha = score;
			raised_alpha = true;

			pv_table[depth][depth] = action;
			for (int next_ply = depth + 1; next_ply < pv_length[depth + 1]; ++next_ply)
			{
				pv_table[depth][next_ply] = pv_table[depth + 1][next_ply];
			}
			pv_length[depth] = pv_length[depth + 1];
		}

		if (alpha >= beta)
		{
			if (quiet)
			{
				if (killers[0] != action)
				{
					killers[2] = killers[1];
					killers[1] = killers[0];
					killers[0] = action;
				}

				int bonus = depth_remaining * depth_remaining;

				int& history_value = history_heuristic[color][from][to];
				history_value += bonus - (history_value * abs(bonus)) / MAX_HEURISTIC_VALUE;

				if (continuation_values_1)
				{
					int& continuation_value = (*continuation_values_1)[moving_piece][to];
					continuation_value += bonus - (continuation_value * abs(bonus)) / MAX_HEURISTIC_VALUE;
				}

				if (continuation_values_2)
				{
					int& continuation_value = (*continuation_values_2)[moving_piece][to];
					continuation_value += bonus - (continuation_value * abs(bonus)) / MAX_HEURISTIC_VALUE;
				}

				if (counteraction_1)
					*counteraction_1 = action;

				if (counteraction_2)
					*counteraction_2 = action;
			}
			else if (is_capture && !is_promo(action_flags))
			{
				int bonus = depth_remaining * depth_remaining;
				int captured_piece = (action_flags == EN_PASSANT) ? PAWN : full_piece_piece(captured);
				int& capture_value = capture_history[color][moving_piece][to][captured_piece];
				capture_value += bonus - (capture_value * abs(bonus)) / MAX_HEURISTIC_VALUE;
			}

			break;
		}
		else if (quiet && !raised_alpha)
		{
			int penalty = depth_remaining;

			int& history_value = history_heuristic[color][from][to];
			history_value += -penalty - (history_value * abs(penalty)) / MAX_HEURISTIC_VALUE;

			if (continuation_values_1)
			{
				int& continuation_value = (*continuation_values_1)[moving_piece][to];
				continuation_value += -penalty - (continuation_value * abs(penalty)) / MAX_HEURISTIC_VALUE;
			}

			if (continuation_values_2)
			{
				int& continuation_value = (*continuation_values_2)[moving_piece][to];
				continuation_value += -penalty - (continuation_value * abs(penalty)) / MAX_HEURISTIC_VALUE;
			}
		}
		else if (is_capture && !is_promo(action_flags) && !raised_alpha)
		{
			int penalty = depth_remaining;
			int captured_piece = (action_flags == EN_PASSANT) ? PAWN : full_piece_piece(captured);
			int& capture_value = capture_history[color][moving_piece][to][captured_piece];
			capture_value += -penalty - (capture_value * abs(penalty)) / MAX_HEURISTIC_VALUE;
		}
	}

	uint8_t flag;
	if (best_score <= alpha_orig)
		flag = UPPERBOUND;
	else if (best_score >= beta)
		flag = LOWERBOUND;
	else
		flag = EXACT;

	if (excluded_action == 0 && !root_in_check && eval_is_computed && abs(best_score) < MATE_THRESHOLD)
	{
		bool best_is_tactical = false;
		if (best_action != 0)
		{
			int b_to = to_sq(best_action);
			int b_flags = flags(best_action);
			best_is_tactical = chess_board.squares[b_to] != 0xFF || b_flags == EN_PASSANT || is_promo(b_flags);
		}

		if (!best_is_tactical && (flag == EXACT || (flag == LOWERBOUND && best_score > static_eval) || (flag == UPPERBOUND && best_score < static_eval)))
		{
			int idx = chess_board.pawn_king_hash & (CORR_SIZE - 1);
			int diff = best_score - static_eval;
			int bonus = std::clamp(diff * depth_remaining / CORR_WEIGHT, -CORR_MAX / 4, CORR_MAX / 4);
			int& correction = correction_history[color][idx];
			correction += bonus - correction * abs(bonus) / CORR_MAX;
		}
	}

	int16_t tt_score_store = best_score;
	if (tt_score_store > MATE_THRESHOLD)
		tt_score_store += depth;
	else if (tt_score_store < -MATE_THRESHOLD)
		tt_score_store -= depth;

	if (excluded_action == 0)
	{
		tt.add(key, best_action, tt_score_store, static_eval, depth_remaining, flag, false);
	}

	return best_score;
}

static __forceinline void age_heuristics()
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
						continuation_history_1[color][piece1][square1][piece2][square2] -= continuation_history_1[color][piece1][square1][piece2][square2] >> 1;
						continuation_history_2[color][piece1][square1][piece2][square2] -= continuation_history_2[color][piece1][square1][piece2][square2] >> 1;
					}
				}
			}
		}
	}

	for (int color = 0; color < COLOR_NB; ++color)
		for (int piece = 0; piece < PIECE_NB; ++piece)
			for (int to = 0; to < 64; ++to)
				for (int captured = 0; captured < PIECE_NB; ++captured)
					capture_history[color][piece][to][captured] -= capture_history[color][piece][to][captured] >> 1;

	memset(killer_actions, 0, sizeof(killer_actions));
}

static __forceinline std::tuple<int, int, bool> choose_search_times(const go_params& params, int color)
{
	if (params.depth != -1 || params.nodes != -1)
		return { 1000000000, 1000000000, true };

	if (params.infinite)
		return { MAX_THINKING_TIME_MS, MAX_THINKING_TIME_MS, true };

	if (params.movetime != -1)
		return { params.movetime, params.movetime, true };

	int time_left_ms = (color == WHITE) ? params.wtime : params.btime;
	int inc_ms = (color == WHITE) ? params.winc : params.binc;

	if (time_left_ms <= 0)
		return { MIN_THINKING_TIME_MS, MIN_THINKING_TIME_MS, true };

	time_left_ms -= MIN_THINKING_TIME_MS;
	int thinking_time_ms = time_left_ms / TIME_DIVISOR + inc_ms * (INC_FRACTION / 100.0f);

	thinking_time_ms = std::min(thinking_time_ms, MAX_THINKING_TIME_MS);
	thinking_time_ms = std::max(thinking_time_ms, MIN_THINKING_TIME_MS);

	return { (int)(thinking_time_ms * (HARD_LIMIT_MULTIPLIER / 100.0f)), thinking_time_ms, false };
}

uint16_t get_best_action(const board& chess_board, action_list& legal_actions, const go_params& params)
{
	age_heuristics();
	tt.new_generation();

	was_pondering = params.ponder;
	pv_length[0] = 0;
	ponder_action = 0;

	if (legal_actions.count == 1)
	{
		while (g_ponder.load() && !g_stop.load())
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		return legal_actions.actions[0];
	}

	NNUE net;
	net.build_accumulator(chess_board);

	nodes = 0;

	search_aborted = false;
	search_start_time = std::chrono::steady_clock::now();
	std::tuple<int, int, bool> search_times = choose_search_times(params, chess_board.side_to_move);

	int extreme_search_hard_limit_ms = std::get<0>(search_times);
	search_hard_limit_ms = extreme_search_hard_limit_ms;
	real_hard_limit_ms = extreme_search_hard_limit_ms;
	int search_soft_limit_ms = std::get<1>(search_times);
	bool only_use_hard_limit = std::get<2>(search_times);

	float time_factor = 1.0f;
	int best_action_stability = 0;
	int reset_instability = 0;

	int best_score = -INF;
	uint16_t best_action = 0;
	int depth;

	int color = chess_board.side_to_move;

	for (depth = 1; depth <= MAX_DEPTH; ++depth)
	{
		uint64_t key = chess_board.hash;
		const TTEntry* entry = tt.probe(key);

		max_seldepth = 0;

		int current_best_score = -INF;
		uint16_t current_best_action = 0;

		int alpha = (best_score == -INF) ? -INF : best_score - ASPIRATION_WINDOW;
		int beta = (best_score == -INF) ? INF : best_score + ASPIRATION_WINDOW;

		int window = ASPIRATION_WINDOW;

		uint64_t best_action_nodes = 0;
		uint64_t pass_start_nodes = nodes;

		while (true)
		{
			pass_start_nodes = nodes;
			best_action_nodes = 0;

			uint16_t priority_action = current_best_action != 0 ? current_best_action : (best_action != 0 ? best_action : (entry ? entry->best_action : 0));
			action_picker picker(legal_actions, priority_action);

			int alpha_orig = alpha;
			int beta_orig = beta;

			uint16_t prev_current_best_action = current_best_action;
			current_best_score = -INF;
			current_best_action = 0;

			int search_alpha = alpha;
			int search_beta = beta;

			int action_count = 0;
			uint16_t action;

			while ((action = picker.next([&](int* scores, int start, int end)
				{
					for (int i = start; i < end; ++i)
					{
						uint16_t current_action = legal_actions.actions[i];
						int from = from_sq(current_action);
						int to = to_sq(current_action);
						int action_flags = flags(current_action);

						if (current_action == prev_current_best_action)
						{
							scores[i] = 4000000;
						}
						else if (current_action == best_action)
						{
							scores[i] = 3000000;
						}
						else if (entry && current_action == entry->best_action)
						{
							scores[i] = 2000000;
						}
						else if (is_promo(action_flags))
						{
							scores[i] = 1000000 + PIECE_VALUES[promo_piece(action_flags)];
						}
						else if (chess_board.squares[to] != 0xFF || action_flags == EN_PASSANT)
						{
							int score = SEE(chess_board, current_action);
							if (score < 0)
								scores[i] = -10000 + score + capture_history_score(chess_board, color, current_action);
							else
								scores[i] = 800000 + score + capture_history_score(chess_board, color, current_action);
						}
						else
						{
							scores[i] = history_heuristic[color][from][to];
						}
					}
				})) != 0)

			{
				int i = action_count++;

				board temp_board = chess_board;
				temp_board.make_action(action);

				int moving_piece = full_piece_piece(chess_board.squares[from_sq(action)]);

				tt.prefetch(temp_board.hash);

				NNUE temp_net = net;
				temp_net.update(temp_board, chess_board, action);

				int score;

				uint64_t action_start_nodes = nodes;

				if (i == 0)
				{
					score = -negamax(temp_board, temp_net, depth - 1, -search_beta, -search_alpha, 1, action, moving_piece, 0, 0);
					if (score <= alpha)
					{
						current_best_score = score;
						break;
					}
				}
				else
				{
					score = -negamax(temp_board, temp_net, depth - 1, -search_alpha - 1, -search_alpha, 1, action, moving_piece, 0, 0);

					if (score > search_alpha)
						score = -negamax(temp_board, temp_net, depth - 1, -search_beta, -search_alpha, 1, action, moving_piece, 0, 0);
				}

				if (score > current_best_score)
				{
					current_best_score = score;
					current_best_action = action;
					best_action_nodes = nodes - action_start_nodes;

					pv_table[0][0] = action;
					for (int pv_i = 1; pv_i < pv_length[1]; ++pv_i)
					{
						pv_table[0][pv_i] = pv_table[1][pv_i];
					}
					pv_length[0] = pv_length[1];
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

		auto now = std::chrono::steady_clock::now();
		auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - search_start_time);
		long total_elapsed_ms = total_elapsed.count();

		if (best_action == current_best_action)
		{
			++best_action_stability;
			reset_instability = std::max(reset_instability - 1, 0);
		}
		else
		{
			best_action_stability = 0;
			++reset_instability;

			float search_completed_factor = (float)total_elapsed_ms / search_soft_limit_ms;
			time_factor = std::min(1.0f + (search_completed_factor / (float)INSTABILITY_DIVISOR) * reset_instability, MAX_INSTABILITY_FACTOR / 100.0f);
		}

		best_score = current_best_score;
		best_action = current_best_action;

		ponder_action = pv_length[0] >= 2 ? pv_table[0][1] : 0;

		uint64_t nps = 0;
		if (total_elapsed_ms > 0)
			nps = nodes * 1000 / total_elapsed_ms;

		std::string pv_string = "";
		board temp_pv_board = chess_board;

		for (int i = 0; i < pv_length[0]; ++i)
		{
			pv_string += action_to_string(pv_table[0][i], temp_pv_board.side_to_move) + " ";
			temp_pv_board.make_action(pv_table[0][i]);
		}

		std::ostringstream info;
		info << "info depth " << depth
			<< " seldepth " << max_seldepth;

		if (abs(best_score) > MATE_THRESHOLD)
		{
			int mate_in = (INF - abs(best_score) + 1) / 2;
			if (best_score < 0) mate_in = -mate_in;
			info << " score mate " << mate_in;
		}
		else
		{
			info << " score cp " << best_score;
		}

		info << " nodes " << nodes
			<< " nps " << nps
			<< " time " << total_elapsed_ms
			<< " hashfull " << tt.hashfull()
			<< " pv " << pv_string;

		uci_send(info.str());

		time_factor *= 1.0f - (STABILITY_DECREMENT / 100.0f) * std::min(best_action_stability, MAX_STABILITY_STEPS);
		time_factor = std::max(time_factor, MIN_TIME_FACTOR / 100.0f);
		int adjusted_soft_limit = (int)(search_soft_limit_ms * time_factor);

		if (!only_use_hard_limit)
		{
			uint64_t total_pass_nodes = nodes - pass_start_nodes;
			if (total_pass_nodes > 0 && best_action_nodes > 0)
			{
				double effort = (double)best_action_nodes / (double)total_pass_nodes;
				double node_factor = (NODE_TM_BASE - NODE_TM_SCALE * effort) / 100.0;
				node_factor = std::clamp(node_factor, NODE_TM_MIN / 100.0, NODE_TM_MAX / 100.0);
				adjusted_soft_limit = (int)(adjusted_soft_limit * node_factor);
			}
		}

		check_ponder_transition();

		if (g_ponder.load())
			continue;

		if (params.depth != -1 && depth >= params.depth || params.nodes != -1 && nodes >= params.nodes)
			break;

		auto limit_now = std::chrono::steady_clock::now();
		long elapsed_for_limit = std::chrono::duration_cast<std::chrono::milliseconds>(limit_now - search_start_time).count();

		search_hard_limit_ms = only_use_hard_limit ? search_hard_limit_ms : std::min(extreme_search_hard_limit_ms, adjusted_soft_limit * SOFT_TO_HARD_MULTIPLIER);
		int effective_soft_limit = only_use_hard_limit ? search_hard_limit_ms : adjusted_soft_limit;
		if (elapsed_for_limit >= effective_soft_limit)
			break;
	}

	while (g_ponder.load() && !g_stop.load())
		std::this_thread::sleep_for(std::chrono::milliseconds(1));

	if (best_action == 0)
		return legal_actions.actions[0];

	return best_action;
}