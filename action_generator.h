#pragma once

#include <cstdint>
#include "board.h"
#include "init.h"
#include "piece.h"
#include "action.h"
#include "bits.h"

inline bool is_square_attacked(const board& chess_board, int square, int by_color, int king_from = -1)
{
	uint64_t occupied = (chess_board.occupied[WHITE] | chess_board.occupied[BLACK]);
	if (king_from != -1)
		occupied &= ~(1ull << king_from);

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

inline uint64_t attackers_to(const uint64_t (&pieces)[COLOR_NB][PIECE_NB], int square, uint64_t occupied)
{
	uint64_t bishops = bishop_attacks[square][((occupied & bishop_masks[square]) * bishop_magics[square]) >> bishop_shifts[square]];
	uint64_t rooks = rook_attacks[square][((occupied & rook_masks[square]) * rook_magics[square]) >> rook_shifts[square]];

	uint64_t attackers =
		(pawn_attacks[BLACK][square] & pieces[WHITE][PAWN]) |
		(pawn_attacks[WHITE][square] & pieces[BLACK][PAWN]) |
		(knight_attacks[square] & (pieces[WHITE][KNIGHT] | pieces[BLACK][KNIGHT])) |
		(king_attacks[square] & (pieces[WHITE][KING] | pieces[BLACK][KING]));

	attackers |= bishops & (
		pieces[WHITE][BISHOP] |
		pieces[BLACK][BISHOP] |
		pieces[WHITE][QUEEN] |
		pieces[BLACK][QUEEN]);

	attackers |= rooks & (
		pieces[WHITE][ROOK] |
		pieces[BLACK][ROOK] |
		pieces[WHITE][QUEEN] |
		pieces[BLACK][QUEEN]);

	return attackers;
}

static inline void add_promo_pieces(int from, int to, action_list& actions)
{
	for (int promo_piece = PROMO_KNIGHT; promo_piece <= PROMO_QUEEN; ++promo_piece)
	{
		actions.actions[actions.count++] = create_action(from, to, promo_piece);
	}
}

static inline void generate_pawn_actions(const board& chess_board, action_list& actions, uint64_t pinned, uint64_t check_mask)
{
	bool color = chess_board.side_to_move;
	uint64_t pawns = chess_board.pieces[color][PAWN];
	uint64_t empty = ~(chess_board.occupied[WHITE] | chess_board.occupied[BLACK]);
	uint64_t enemy = chess_board.occupied[color ^ 1];

	uint64_t single_push = color == WHITE ? (pawns << 8) & empty & check_mask : (pawns >> 8) & empty & check_mask;

	while (single_push)
	{
		int to = pop_lsb(single_push);
		int from = color == WHITE ? to - 8 : to + 8;

		if (((1ull << from) & pinned) && !(aligned_mask[from][chess_board.king_square[color]] & (1ull << to)))
			continue;

		if ((to >= 56 && color == WHITE) || (to <= 7 && color == BLACK))
			add_promo_pieces(from, to, actions);
		else
			actions.actions[actions.count++] = create_action(from, to, QUIET);
	}

	uint64_t start_rank = color == WHITE ? 0x000000000000FF00ull : 0x00FF000000000000ull;
	uint64_t double_push = color == WHITE ? ((pawns & start_rank) << 16) & empty & (empty << 8) & check_mask : ((pawns & start_rank) >> 16) & empty & (empty >> 8) & check_mask;

	while (double_push)
	{
		int to = pop_lsb(double_push);
		int from = color == WHITE ? to - 16 : to + 16;

		if (((1ull << from) & pinned) && !(aligned_mask[from][chess_board.king_square[color]] & (1ull << to)))
			continue;

		actions.actions[actions.count++] = create_action(from, to, DOUBLE_PAWN);
	}

	uint64_t left_capture = color == WHITE ? ((pawns & ~0x0101010101010101ull) << 7) & enemy & check_mask : ((pawns & ~0x8080808080808080ull) >> 7) & enemy & check_mask;
	uint64_t right_capture = color == WHITE ? ((pawns & ~0x8080808080808080ull) << 9) & enemy & check_mask : ((pawns & ~0x0101010101010101ull) >> 9) & enemy & check_mask;

	while (left_capture)
	{
		int to = pop_lsb(left_capture);
		int from = color == WHITE ? to - 7 : to + 7;

		if (((1ull << from) & pinned) && !(aligned_mask[from][chess_board.king_square[color]] & (1ull << to)))
			continue;

		if ((to >= 56 && color == WHITE) || (to <= 7 && color == BLACK))
			add_promo_pieces(from, to, actions);
		else
			actions.actions[actions.count++] = create_action(from, to, QUIET);
	}

	while (right_capture)
	{
		int to = pop_lsb(right_capture);
		int from = color == WHITE ? to - 9 : to + 9;

		if (((1ull << from) & pinned) && !(aligned_mask[from][chess_board.king_square[color]] & (1ull << to)))
			continue;

		if ((to >= 56 && color == WHITE) || (to <= 7 && color == BLACK))
			add_promo_pieces(from, to, actions);
		else
			actions.actions[actions.count++] = create_action(from, to, QUIET);
	}

	int8_t en_passant_square = chess_board.en_passant_square;
	if (en_passant_square == -1)
		return;

	uint64_t ep_mask = 1ull << en_passant_square;
	uint64_t left_ep = color == WHITE ? ((pawns & ~0x0101010101010101ull) << 7) & ep_mask : ((pawns & ~0x8080808080808080ull) >> 7) & ep_mask;
	uint64_t right_ep = color == WHITE ? ((pawns & ~0x8080808080808080ull) << 9) & ep_mask : ((pawns & ~0x0101010101010101ull) >> 9) & ep_mask;
	uint64_t captured_pawn_sq = (color == WHITE) ? (ep_mask >> 8) : (ep_mask << 8);

	board temp;

	if (left_ep && ((1ull << en_passant_square) & check_mask || captured_pawn_sq & check_mask))
	{
		int from = color == WHITE ? en_passant_square - 7 : en_passant_square + 7;

		temp = chess_board;
		temp.make_action(create_action(from, en_passant_square, EN_PASSANT));

		if (!is_square_attacked(temp, chess_board.king_square[color], color ^ 1))
			actions.actions[actions.count++] = create_action(from, en_passant_square, EN_PASSANT);
	}

	if (right_ep && ((1ull << en_passant_square) & check_mask || captured_pawn_sq & check_mask))
	{
		int from = color == WHITE ? en_passant_square - 9 : en_passant_square + 9;

		temp = chess_board;
		temp.make_action(create_action(from, en_passant_square, EN_PASSANT));

		if (!is_square_attacked(temp, chess_board.king_square[color], color ^ 1))
			actions.actions[actions.count++] = create_action(from, en_passant_square, EN_PASSANT);
	}
}

static inline void generate_knight_actions(const board& chess_board, action_list& actions, uint64_t pinned, uint64_t check_mask)
{
	int color = chess_board.side_to_move;
	uint64_t own = chess_board.occupied[color];

	uint64_t knights = chess_board.pieces[color][KNIGHT] & ~pinned;

	while (knights)
	{
		int from = pop_lsb(knights);

		uint64_t legal_knight_actions = knight_attacks[from] & ~own & check_mask;

		while (legal_knight_actions)
		{
			int to = pop_lsb(legal_knight_actions);

			uint16_t action = create_action(from, to, QUIET);
			actions.actions[actions.count++] = action;
		}
	}
}

static inline void generate_bishop_actions(const board& chess_board, action_list& actions, uint64_t pinned, uint64_t check_mask)
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

		uint64_t legal_bishop_actions = bishop_attacks[from][index] & ~own & check_mask;
		if ((1ull << from) & pinned)
			legal_bishop_actions &= aligned_mask[from][chess_board.king_square[color]];

		while (legal_bishop_actions)
		{
			int to = pop_lsb(legal_bishop_actions);

			uint16_t action = create_action(from, to, QUIET);
			actions.actions[actions.count++] = action;
		}
	}
}

static inline void generate_rook_actions(const board& chess_board, action_list& actions, uint64_t pinned, uint64_t check_mask)
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

		uint64_t legal_rook_actions = rook_attacks[from][index] & ~own & check_mask;
		if ((1ull << from) & pinned)
			legal_rook_actions &= aligned_mask[from][chess_board.king_square[color]];

		while (legal_rook_actions)
		{
			int to = pop_lsb(legal_rook_actions);

			uint16_t action = create_action(from, to, QUIET);
			actions.actions[actions.count++] = action;
		}
	}
}

static inline void generate_queen_actions(const board& chess_board, action_list& actions, uint64_t pinned, uint64_t check_mask)
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

		uint64_t legal_queen_actions = (rook_attacks[from][rook_index] | bishop_attacks[from][bishop_index]) & ~own & check_mask;
		if ((1ull << from) & pinned)
			legal_queen_actions &= aligned_mask[from][chess_board.king_square[color]];

		while (legal_queen_actions)
		{
			int to = pop_lsb(legal_queen_actions);

			uint16_t action = create_action(from, to, QUIET);
			actions.actions[actions.count++] = action;
		}
	}
}

static inline void generate_king_actions(const board& chess_board, action_list& actions)
{
	int color = chess_board.side_to_move;
	uint64_t own = chess_board.occupied[color];
	uint64_t occupied = chess_board.occupied[WHITE] | chess_board.occupied[BLACK];

	int from = chess_board.king_square[color];
	uint64_t legal_king_actions = king_attacks[from] & ~own;

	while (legal_king_actions)
	{
		int to = pop_lsb(legal_king_actions);

		if (is_square_attacked(chess_board, to, color ^ 1, from))
			continue;

		uint16_t action = create_action(from, to, QUIET);
		actions.actions[actions.count++] = action;
	}

	if (color == WHITE && (chess_board.castling_rights & CASTLE_WHITE_KINGSIDE_RIGHT))
	{
		if (!(occupied & ((1ull << 5) | (1ull << 6))))
		{
			if (!is_square_attacked(chess_board, 4, BLACK) &&
				!is_square_attacked(chess_board, 5, BLACK) &&
				!is_square_attacked(chess_board, 6, BLACK))
			{
				actions.actions[actions.count++] = create_action(4, 6, CASTLE_WHITE_KINGSIDE);
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
				actions.actions[actions.count++] = create_action(4, 2, CASTLE_WHITE_QUEENSIDE);
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
				actions.actions[actions.count++] = create_action(60, 62, CASTLE_BLACK_KINGSIDE);
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
				actions.actions[actions.count++] = create_action(60, 58, CASTLE_BLACK_QUEENSIDE);
			}
		}
	}
}

static inline uint64_t compute_pins(const board& chess_board)
{
	int color = chess_board.side_to_move;
	int enemy = color ^ 1;

	uint64_t own = chess_board.occupied[color];
	uint64_t occupied = chess_board.occupied[WHITE] | chess_board.occupied[BLACK];
	uint64_t enemy_rooks = chess_board.pieces[enemy][ROOK] | chess_board.pieces[enemy][QUEEN];
	uint64_t enemy_bishops = chess_board.pieces[enemy][BISHOP] | chess_board.pieces[enemy][QUEEN];

	uint8_t king_square = chess_board.king_square[color];
	uint64_t pinned = 0;

	{
		uint64_t blockers = occupied & rook_masks[king_square];
		uint64_t index = (blockers * rook_magics[king_square]) >> rook_shifts[king_square];
		uint64_t ray = rook_attacks[king_square][index];
		uint64_t candidates = ray & own;

		while (candidates)
		{
			int pinned_square = pop_lsb(candidates);
			uint64_t occ2 = occupied & ~(1ull << pinned_square);
			uint64_t index2 = ((occ2 & rook_masks[king_square]) * rook_magics[king_square]) >> rook_shifts[king_square];
			uint64_t ray2 = rook_attacks[king_square][index2];

			if (ray2 & enemy_rooks & aligned_mask[king_square][pinned_square])
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
			uint64_t occ2 = occupied & ~(1ull << pinned_square);
			uint64_t index2 = ((occ2 & bishop_masks[king_square]) * bishop_magics[king_square]) >> bishop_shifts[king_square];
			uint64_t ray2 = bishop_attacks[king_square][index2];

			if (ray2 & enemy_bishops & aligned_mask[king_square][pinned_square])
				pinned |= (1ull << pinned_square);
		}
	}

	return pinned;
}

inline action_list generate_legal_actions(const board& chess_board)
{
	action_list actions;

	int color = chess_board.side_to_move;
	int enemy = color ^ 1;
	uint64_t occupied = chess_board.occupied[WHITE] | chess_board.occupied[BLACK];

	uint8_t king_sq = chess_board.king_square[color];

	uint64_t checkers = attackers_to(chess_board.pieces, king_sq, occupied) & chess_board.occupied[enemy];
	uint64_t pinned = compute_pins(chess_board);
	uint64_t check_mask = ~0;

	if (checkers)
	{
		if (checkers & (checkers - 1))
		{
			check_mask = 0;
		}
		else
		{
			int square = lsb(checkers);
			check_mask = squares_between[king_sq][square] | (1ull << square);
		}
	}

	generate_king_actions(chess_board, actions);

	if (checkers && (checkers & (checkers - 1)))
		return actions;

	generate_pawn_actions(chess_board, actions, pinned, check_mask);
	generate_knight_actions(chess_board, actions, pinned, check_mask);
	generate_bishop_actions(chess_board, actions, pinned, check_mask);
	generate_rook_actions(chess_board, actions, pinned, check_mask);
	generate_queen_actions(chess_board, actions, pinned, check_mask);

	return actions;
}

static inline void generate_only_tactical_pawn_actions(const board& chess_board, action_list& actions, uint64_t pinned, uint64_t check_mask)
{
	bool color = chess_board.side_to_move;
	uint64_t pawns = chess_board.pieces[color][PAWN];
	uint64_t empty = ~(chess_board.occupied[WHITE] | chess_board.occupied[BLACK]);
	uint64_t enemy = chess_board.occupied[color ^ 1];

	uint64_t single_push = color == WHITE ? (pawns << 8) & empty & check_mask : (pawns >> 8) & empty & check_mask;

	while (single_push)
	{
		int to = pop_lsb(single_push);
		int from = color == WHITE ? to - 8 : to + 8;

		if (((1ull << from) & pinned) && !(aligned_mask[from][chess_board.king_square[color]] & (1ull << to)))
			continue;

		if ((to >= 56 && color == WHITE) || (to <= 7 && color == BLACK))
			add_promo_pieces(from, to, actions);
	}

	uint64_t left_capture = color == WHITE ? ((pawns & ~0x0101010101010101ull) << 7) & enemy & check_mask : ((pawns & ~0x8080808080808080ull) >> 7) & enemy & check_mask;
	uint64_t right_capture = color == WHITE ? ((pawns & ~0x8080808080808080ull) << 9) & enemy & check_mask : ((pawns & ~0x0101010101010101ull) >> 9) & enemy & check_mask;

	while (left_capture)
	{
		int to = pop_lsb(left_capture);
		int from = color == WHITE ? to - 7 : to + 7;

		if (((1ull << from) & pinned) && !(aligned_mask[from][chess_board.king_square[color]] & (1ull << to)))
			continue;

		if ((to >= 56 && color == WHITE) || (to <= 7 && color == BLACK))
			add_promo_pieces(from, to, actions);
		else
			actions.actions[actions.count++] = create_action(from, to, QUIET);
	}

	while (right_capture)
	{
		int to = pop_lsb(right_capture);
		int from = color == WHITE ? to - 9 : to + 9;

		if (((1ull << from) & pinned) && !(aligned_mask[from][chess_board.king_square[color]] & (1ull << to)))
			continue;

		if ((to >= 56 && color == WHITE) || (to <= 7 && color == BLACK))
			add_promo_pieces(from, to, actions);
		else
			actions.actions[actions.count++] = create_action(from, to, QUIET);
	}

	int8_t en_passant_square = chess_board.en_passant_square;
	if (en_passant_square == -1)
		return;

	uint64_t ep_mask = 1ull << en_passant_square;
	uint64_t left_ep = color == WHITE ? ((pawns & ~0x0101010101010101ull) << 7) & ep_mask : ((pawns & ~0x8080808080808080ull) >> 7) & ep_mask;
	uint64_t right_ep = color == WHITE ? ((pawns & ~0x8080808080808080ull) << 9) & ep_mask : ((pawns & ~0x0101010101010101ull) >> 9) & ep_mask;
	uint64_t captured_pawn_sq = (color == WHITE) ? (ep_mask >> 8) : (ep_mask << 8);

	board temp;

	if (left_ep && ((1ull << en_passant_square) & check_mask || captured_pawn_sq & check_mask))
	{
		int from = color == WHITE ? en_passant_square - 7 : en_passant_square + 7;

		temp = chess_board;
		temp.make_action(create_action(from, en_passant_square, EN_PASSANT));

		if (!is_square_attacked(temp, chess_board.king_square[color], color ^ 1))
			actions.actions[actions.count++] = create_action(from, en_passant_square, EN_PASSANT);
	}

	if (right_ep && ((1ull << en_passant_square) & check_mask || captured_pawn_sq & check_mask))
	{
		int from = color == WHITE ? en_passant_square - 9 : en_passant_square + 9;

		temp = chess_board;
		temp.make_action(create_action(from, en_passant_square, EN_PASSANT));

		if (!is_square_attacked(temp, chess_board.king_square[color], color ^ 1))
			actions.actions[actions.count++] = create_action(from, en_passant_square, EN_PASSANT);
	}
}

static inline void generate_only_tactical_knight_actions(const board& chess_board, action_list& actions, uint64_t pinned, uint64_t check_mask)
{
	int color = chess_board.side_to_move;
	uint64_t enemy = chess_board.occupied[color ^ 1];

	uint64_t knights = chess_board.pieces[color][KNIGHT] & ~pinned;

	while (knights)
	{
		int from = pop_lsb(knights);

		uint64_t legal_knight_actions = knight_attacks[from] & enemy & check_mask;

		while (legal_knight_actions)
		{
			int to = pop_lsb(legal_knight_actions);

			uint16_t action = create_action(from, to, QUIET);
			actions.actions[actions.count++] = action;
		}
	}
}

static inline void generate_only_tactical_bishop_actions(const board& chess_board, action_list& actions, uint64_t pinned, uint64_t check_mask)
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

		uint64_t legal_bishop_actions = bishop_attacks[from][index] & enemy & check_mask;
		if ((1ull << from) & pinned)
			legal_bishop_actions &= aligned_mask[from][chess_board.king_square[color]];

		while (legal_bishop_actions)
		{
			int to = pop_lsb(legal_bishop_actions);

			uint16_t action = create_action(from, to, QUIET);
			actions.actions[actions.count++] = action;
		}
	}
}

static inline void generate_only_tactical_rook_actions(const board& chess_board, action_list& actions, uint64_t pinned, uint64_t check_mask)
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

		uint64_t legal_rook_actions = rook_attacks[from][index] & enemy & check_mask;
		if ((1ull << from) & pinned)
			legal_rook_actions &= aligned_mask[from][chess_board.king_square[color]];

		while (legal_rook_actions)
		{
			int to = pop_lsb(legal_rook_actions);

			uint16_t action = create_action(from, to, QUIET);
			actions.actions[actions.count++] = action;
		}
	}
}

static inline void generate_only_tactical_queen_actions(const board& chess_board, action_list& actions, uint64_t pinned, uint64_t check_mask)
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

		uint64_t legal_queen_actions = (rook_attacks[from][rook_index] | bishop_attacks[from][bishop_index]) & enemy & check_mask;
		if ((1ull << from) & pinned)
			legal_queen_actions &= aligned_mask[from][chess_board.king_square[color]];

		while (legal_queen_actions)
		{
			int to = pop_lsb(legal_queen_actions);

			uint16_t action = create_action(from, to, QUIET);
			actions.actions[actions.count++] = action;
		}
	}
}

static inline void generate_only_tactical_king_actions(const board& chess_board, action_list& actions)
{
	int color = chess_board.side_to_move;
	uint64_t enemy = chess_board.occupied[color ^ 1];

	int from = chess_board.king_square[color];
	uint64_t legal_king_actions = king_attacks[from] & enemy;

	while (legal_king_actions)
	{
		int to = pop_lsb(legal_king_actions);

		if (is_square_attacked(chess_board, to, color ^ 1, from))
			continue;

		uint16_t action = create_action(from, to, QUIET);
		actions.actions[actions.count++] = action;
	}
}

inline action_list generate_only_tactical_legal_actions(const board& chess_board)
{
	action_list actions;

	int color = chess_board.side_to_move;
	uint64_t occupied = chess_board.occupied[WHITE] | chess_board.occupied[BLACK];

	uint8_t king_sq = chess_board.king_square[color];

	uint64_t checkers = attackers_to(chess_board.pieces, king_sq, occupied) & chess_board.occupied[color ^ 1];
	uint64_t pinned = compute_pins(chess_board);
	uint64_t check_mask = ~0;

	if (checkers)
	{
		if (checkers & (checkers - 1))
		{
			check_mask = 0;
		}
		else
		{
			int square = lsb(checkers);
			check_mask = squares_between[king_sq][square] | (1ull << square);
		}
	}

	generate_only_tactical_king_actions(chess_board, actions);

	if (checkers && (checkers & (checkers - 1)))
		return actions;

	generate_only_tactical_pawn_actions(chess_board, actions, pinned, check_mask);
	generate_only_tactical_knight_actions(chess_board, actions, pinned, check_mask);
	generate_only_tactical_bishop_actions(chess_board, actions, pinned, check_mask);
	generate_only_tactical_rook_actions(chess_board, actions, pinned, check_mask);
	generate_only_tactical_queen_actions(chess_board, actions, pinned, check_mask);

	return actions;
}