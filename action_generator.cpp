#include <intrin.h>
#include <chrono>
#include <iostream>
#include "action_generator.h"
#include "board.h"
#include "action.h"
#include "init.h"

using namespace std;

long long pawn_time = 0;
long long knight_time = 0;
long long bishop_time = 0;
long long rook_time = 0;
long long queen_time = 0;
long long king_time = 0;
long long legality_check_time = 0;

inline bool is_square_attacked(const board& chess_board, int square, int by_color)
{
	uint64_t occupied = chess_board.occupied[WHITE] | chess_board.occupied[BLACK];

	if (pawn_attacks[!by_color][square] & chess_board.pieces[by_color][PAWN])
		return true;

	if (knight_attacks[square] & chess_board.pieces[by_color][KNIGHT])
		return true;

	if (king_attacks[square] & chess_board.pieces[by_color][KING])
		return true;

	{
		uint64_t blockers = occupied & bishop_masks[square];
		uint64_t index = (blockers * bishop_magics[square]) >> bishop_shifts[square];
		if (bishop_attacks[square][index] & (chess_board.pieces[by_color][BISHOP] | chess_board.pieces[by_color][QUEEN]))
			return true;
	}

	{
		uint64_t blockers = occupied & rook_masks[square];
		uint64_t index = (blockers * rook_magics[square]) >> rook_shifts[square];
		if (rook_attacks[square][index] & (chess_board.pieces[by_color][ROOK] | chess_board.pieces[by_color][QUEEN]))
			return true;
	}

	return false;
}

static inline void add_promo_pieces(int from, int to, action_list& pseudo_legal_actions)
{
	for (int promo_piece = PROMO_KNIGHT; promo_piece <= PROMO_QUEEN; ++promo_piece)
	{
		pseudo_legal_actions.actions[pseudo_legal_actions.count++] = create_action(from, to, promo_piece);
	}
}

static inline void generate_pseudo_legal_pawn_actions(const board& chess_board, action_list& pseudo_legal_actions)
{
	bool color = chess_board.side_to_move;
	uint64_t pawns = chess_board.pieces[color][PAWN];
	uint64_t empty = ~(chess_board.occupied[WHITE] | chess_board.occupied[BLACK]);
	uint64_t enemy = chess_board.occupied[color ^ 1];

	uint64_t single_push = color == WHITE ? (pawns << 8) & empty : (pawns >> 8) & empty;

	while (single_push)
	{
		int to = pop_lsb(single_push);
		int from = color == WHITE ? to - 8 : to + 8;

		if ((to >= 56 && color == WHITE) || (to <= 7 && color == BLACK))
			add_promo_pieces(from, to, pseudo_legal_actions);
		else
			pseudo_legal_actions.actions[pseudo_legal_actions.count++] = create_action(from, to, QUIET);
	}

	uint64_t start_rank = color == WHITE ? 0x000000000000FF00ull : 0x00FF000000000000ull;
	uint64_t double_push = color == WHITE ? ((pawns & start_rank) << 16) & empty & (empty << 8) : ((pawns & start_rank) >> 16) & empty & (empty >> 8);

	while (double_push)
	{
		int to = pop_lsb(double_push);
		int from = color == WHITE ? to - 16 : to + 16;
		pseudo_legal_actions.actions[pseudo_legal_actions.count++] = create_action(from, to, DOUBLE_PAWN);
	}

	uint64_t left_capture = color == WHITE ? ((pawns & ~0x0101010101010101ull) << 7) & enemy : ((pawns & ~0x8080808080808080ull) >> 7) & enemy;
	uint64_t right_capture = color == WHITE ? ((pawns & ~0x8080808080808080ull) << 9) & enemy : ((pawns & ~0x0101010101010101ull) >> 9) & enemy;

	while (left_capture)
	{
		int to = pop_lsb(left_capture);
		int from = color == WHITE ? to - 7 : to + 7;

		if ((to >= 56 && color == WHITE) || (to <= 7 && color == BLACK))
			add_promo_pieces(from, to, pseudo_legal_actions);
		else
			pseudo_legal_actions.actions[pseudo_legal_actions.count++] = create_action(from, to, QUIET);
	}

	while (right_capture)
	{
		int to = pop_lsb(right_capture);
		int from = color == WHITE ? to - 9 : to + 9;

		if ((to >= 56 && color == WHITE) || (to <= 7 && color == BLACK))
			add_promo_pieces(from, to, pseudo_legal_actions);
		else
			pseudo_legal_actions.actions[pseudo_legal_actions.count++] = create_action(from, to, QUIET);
	}

	int8_t ep_square = chess_board.en_passant_square;
	if (ep_square == -1)
		return;

	uint64_t ep_mask = 1ull << ep_square;
	uint64_t left_ep = color == WHITE ? ((pawns & ~0x0101010101010101ull) << 7) & ep_mask : ((pawns & ~0x8080808080808080ull) >> 7) & ep_mask;
	uint64_t right_ep = color == WHITE ? ((pawns & ~0x8080808080808080ull) << 9) & ep_mask : ((pawns & ~0x0101010101010101ull) >> 9) & ep_mask;

	if (left_ep)
	{
		int from = color == WHITE ? ep_square - 7 : ep_square + 7;
		pseudo_legal_actions.actions[pseudo_legal_actions.count++] = create_action(from, ep_square, EN_PASSANT);
	}

	if (right_ep)
	{
		int from = color == WHITE ? ep_square - 9 : ep_square + 9;
		pseudo_legal_actions.actions[pseudo_legal_actions.count++] = create_action(from, ep_square, EN_PASSANT);
	}
}

static inline void generate_pseudo_legal_knight_actions(const board& chess_board, action_list& pseudo_legal_actions)
{
	int color = chess_board.side_to_move;
	uint64_t own = chess_board.occupied[color];

	uint64_t knights = chess_board.pieces[color][KNIGHT];
	while (knights)
	{
		int from = pop_lsb(knights);
		uint64_t legal_knight_actions = knight_attacks[from] & ~own;

		while (legal_knight_actions)
		{
			int to = pop_lsb(legal_knight_actions);

			uint16_t action = create_action(from, to, QUIET);
			pseudo_legal_actions.actions[pseudo_legal_actions.count++] = action;
		}
	}
}

static inline void generate_pseudo_legal_bishop_actions(const board& chess_board, action_list& pseudo_legal_actions)
{
	int color = chess_board.side_to_move;
	uint64_t own = chess_board.occupied[color];
	uint64_t enemy = chess_board.occupied[color ^ 1];
	uint64_t occupied = own | enemy;

	uint64_t bishops = chess_board.pieces[color][BISHOP];

	while (bishops)
	{
		int from = pop_lsb(bishops);
		uint64_t blockers = occupied & bishop_masks[from];
		int index = (blockers * bishop_magics[from]) >> bishop_shifts[from];

		uint64_t legal_bishop_actions = bishop_attacks[from][index] & ~own;
		while (legal_bishop_actions)
		{
			int to = pop_lsb(legal_bishop_actions);

			uint16_t action = create_action(from, to, QUIET);
			pseudo_legal_actions.actions[pseudo_legal_actions.count++] = action;
		}
	}
}

static inline void generate_pseudo_legal_rook_actions(const board& chess_board, action_list& pseudo_legal_actions)
{
	int color = chess_board.side_to_move;
	uint64_t own = chess_board.occupied[color];
	uint64_t enemy = chess_board.occupied[color ^ 1];
	uint64_t occupied = own | enemy;

	uint64_t rooks = chess_board.pieces[color][ROOK];

	while (rooks)
	{
		int from = pop_lsb(rooks);
		uint64_t blockers = occupied & rook_masks[from];
		int index = (blockers * rook_magics[from]) >> rook_shifts[from];

		uint64_t legal_rook_actions = rook_attacks[from][index] & ~own;
		while (legal_rook_actions)
		{
			int to = pop_lsb(legal_rook_actions);

			uint16_t action = create_action(from, to, QUIET);
			pseudo_legal_actions.actions[pseudo_legal_actions.count++] = action;
		}
	}
}

static inline void generate_pseudo_legal_queen_actions(const board& chess_board, action_list& pseudo_legal_actions)
{
	int color = chess_board.side_to_move;
	uint64_t own = chess_board.occupied[color];
	uint64_t enemy = chess_board.occupied[color ^ 1];
	uint64_t occupied = own | enemy;

	uint64_t queens = chess_board.pieces[color][QUEEN];

	while (queens)
	{
		int from = pop_lsb(queens);

		uint64_t rook_blockers = occupied & rook_masks[from];
		uint64_t bishop_blockers = occupied & bishop_masks[from];

		int rook_index = (rook_blockers * rook_magics[from]) >> rook_shifts[from];
		int bishop_index = (bishop_blockers * bishop_magics[from]) >> bishop_shifts[from];

		uint64_t legal_queen_actions = (rook_attacks[from][rook_index] | bishop_attacks[from][bishop_index]) & ~own;
		while (legal_queen_actions)
		{
			int to = pop_lsb(legal_queen_actions);

			uint16_t action = create_action(from, to, QUIET);
			pseudo_legal_actions.actions[pseudo_legal_actions.count++] = action;
		}
	}
}

static inline void generate_pseudo_legal_king_actions(const board& chess_board, action_list& pseudo_legal_actions)
{
	int color = chess_board.side_to_move;
	uint64_t own = chess_board.occupied[color];
	uint64_t enemy = chess_board.occupied[color ^ 1];
	uint64_t occupied = own | enemy;

	int from = chess_board.king_square[color];
	uint64_t legal_king_actions = king_attacks[from] & ~own;

	while (legal_king_actions)
	{
		int to = pop_lsb(legal_king_actions);

		uint16_t action = create_action(from, to, QUIET);
		pseudo_legal_actions.actions[pseudo_legal_actions.count++] = action;
	}

	if (color == WHITE && (chess_board.castling_rights & CASTLE_WHITE_KINGSIDE_RIGHT))
	{
		if (!(occupied & ((1ull << 5) | (1ull << 6))))
		{
			if (!is_square_attacked(chess_board, 4, BLACK) &&
				!is_square_attacked(chess_board, 5, BLACK) &&
				!is_square_attacked(chess_board, 6, BLACK))
			{
				pseudo_legal_actions.actions[pseudo_legal_actions.count++] = create_action(4, 6, CASTLE_WHITE_KINGSIDE);
			}
		}
	}

	if (color == WHITE && (chess_board.castling_rights & CASTLE_WHITE_QUEENSIDE_RIGHT))
	{
		if (!(occupied & ((1ull << 1) | (1ull << 2) | (1ull << 3))))
		{
			if (!is_square_attacked(chess_board, 2, BLACK) &&
				!is_square_attacked(chess_board, 3, BLACK) &&
				!is_square_attacked(chess_board, 4, BLACK))
			{
				pseudo_legal_actions.actions[pseudo_legal_actions.count++] = create_action(4, 2, CASTLE_WHITE_QUEENSIDE);
			}
		}
	}

	if (color == BLACK && (chess_board.castling_rights & CASTLE_BLACK_KINGSIDE_RIGHT))
	{
		if (!(occupied & ((1ull << 61) | (1ull << 62))))
		{
			if (!is_square_attacked(chess_board, 60, WHITE) &&
				!is_square_attacked(chess_board, 61, WHITE) &&
				!is_square_attacked(chess_board, 62, WHITE))
			{
				pseudo_legal_actions.actions[pseudo_legal_actions.count++] = create_action(60, 62, CASTLE_BLACK_KINGSIDE);
			}
		}
	}

	if (color == BLACK && (chess_board.castling_rights & CASTLE_BLACK_QUEENSIDE_RIGHT))
	{
		if (!(occupied & ((1ull << 57) | (1ull << 58) | (1ull << 59))))
		{
			if (!is_square_attacked(chess_board, 58, WHITE) &&
				!is_square_attacked(chess_board, 59, WHITE) &&
				!is_square_attacked(chess_board, 60, WHITE))
			{
				pseudo_legal_actions.actions[pseudo_legal_actions.count++] = create_action(60, 58, CASTLE_BLACK_QUEENSIDE);
			}
		}
	}
}

static inline uint64_t compute_pins(const board& chess_board)
{
	int color = chess_board.side_to_move;
	int enemy = color ^ 1;
	uint64_t occupied = chess_board.occupied[WHITE] | chess_board.occupied[BLACK];
	uint64_t own = chess_board.occupied[color];

	int king_square = chess_board.king_square[color];
	uint64_t pinned = 0;

	{
		uint64_t blockers = occupied & rook_masks[king_square];
		uint64_t index = (blockers * rook_magics[king_square]) >> rook_shifts[king_square];
		uint64_t ray = rook_attacks[king_square][index];

		uint64_t candidates = ray & own;

		while (candidates)
		{
			int pinned_square = pop_lsb(candidates);

			uint64_t occ2 = occupied ^ (1ull << pinned_square);

			uint64_t blockers2 = occ2 & rook_masks[king_square];
			uint64_t index2 = (blockers2 * rook_magics[king_square]) >> rook_shifts[king_square];
			uint64_t ray2 = rook_attacks[king_square][index2];

			if (ray2 & (chess_board.pieces[enemy][ROOK] | chess_board.pieces[enemy][QUEEN]))
				pinned |= (1ull << pinned_square);
		}
	}

	{
		uint64_t blockers = occupied & bishop_masks[king_square];
		uint64_t index = (blockers * bishop_magics[king_square]) >> bishop_shifts[king_square];
		uint64_t ray = bishop_attacks[king_square][index];

		uint64_t candidates = ray & own;

		while (candidates)
		{
			int pinned_square = pop_lsb(candidates);

			uint64_t occ2 = occupied ^ (1ull << pinned_square);

			uint64_t blockers2 = occ2 & bishop_masks[king_square];
			uint64_t index2 = (blockers2 * bishop_magics[king_square]) >> bishop_shifts[king_square];
			uint64_t ray2 = bishop_attacks[king_square][index2];

			if (ray2 & (chess_board.pieces[enemy][BISHOP] | chess_board.pieces[enemy][QUEEN]))
				pinned |= (1ull << pinned_square);
		}
	}

	return pinned;
}

action_list generate_legal_actions(const board& chess_board)
{
	action_list legal_actions;

	generate_pseudo_legal_pawn_actions(chess_board, legal_actions);
	generate_pseudo_legal_knight_actions(chess_board, legal_actions);
	generate_pseudo_legal_rook_actions(chess_board, legal_actions);
	generate_pseudo_legal_bishop_actions(chess_board, legal_actions);
	generate_pseudo_legal_queen_actions(chess_board, legal_actions);
	generate_pseudo_legal_king_actions(chess_board, legal_actions);

	int color = chess_board.side_to_move;
	int king_square = chess_board.king_square[color];

	uint64_t pins = compute_pins(chess_board);
	bool in_check = is_square_attacked(chess_board, king_square, color ^ 1);
	static board temp;

	for (int i = 0; i < legal_actions.count;)
	{
		uint16_t action = legal_actions.actions[i];
		int from = from_sq(action);
		int to = to_sq(action);

		temp = chess_board;
		temp.make_action(action);

		if (from == king_square)
		{
			if (is_square_attacked(temp, to, color ^ 1))
				legal_actions.actions[i] = legal_actions.actions[--legal_actions.count];
			else
				++i;
		}
		else if (in_check || flags(action) == EN_PASSANT)
		{
			int king_square_temp = temp.king_square[color];
			if (is_square_attacked(temp, king_square_temp, color ^ 1))
				legal_actions.actions[i] = legal_actions.actions[--legal_actions.count];
			else
				++i;
		}
		else if (pins & (1ull << from))
		{
			if (!(aligned_mask[king_square][from] & (1ull << to)))
				legal_actions.actions[i] = legal_actions.actions[--legal_actions.count];
			else
				++i;
		}
		else
		{
			++i;
		}
	}

	return legal_actions;
}