#include <intrin.h>
#include <iostream>
#include "action_generator.h"
#include "board.h"
#include "action.h"

using namespace std;

static constexpr int dirs[8] = { 9, 7, -7, -9, 8, -8, 1, -1 };
static int dist_to_edge[64][8];

static uint64_t pawn_attacks[COLOR_NB][64];
static uint64_t knight_attacks[64];
static uint64_t king_attacks[64];

inline static int lsb(const uint64_t& bb)
{
#ifdef _MSC_VER
	return _tzcnt_u64(bb);
#else
	return __builtin_ctzll(bb);
#endif
}

inline static int pop_lsb(uint64_t& bb)
{
	int square = lsb(bb);
	bb &= bb - 1;
	return square;
}

static void init_dist_to_edge()
{
	for (int square = 0; square < 64; ++square)
	{
		int rank = square / 8;
		int file = square % 8;

		dist_to_edge[square][0] = min(7 - rank, 7 - file);
		dist_to_edge[square][1] = min(7 - rank, file);
		dist_to_edge[square][2] = min(rank, 7 - file);
		dist_to_edge[square][3] = min(rank, file);
		dist_to_edge[square][4] = 7 - rank;
		dist_to_edge[square][5] = rank;
		dist_to_edge[square][6] = 7 - file;
		dist_to_edge[square][7] = file;
	}
}

static void init_pawn_attacks()
{
	for (int square = 0; square < 64; ++square)
	{
		uint64_t white_attacks = 0;
		uint64_t black_attacks = 0;

		int rank = square / 8;
		int file = square % 8;

		if (rank < 7 && file > 0)
			white_attacks |= (1ull << ((rank + 1) * 8 + (file - 1)));
		if (rank < 7 && file < 7)
			white_attacks |= (1ull << ((rank + 1) * 8 + (file + 1)));
		if (rank > 0 && file > 0)
			black_attacks |= (1ull << ((rank - 1) * 8 + (file - 1)));
		if (rank > 0 && file < 7)
			black_attacks |= (1ull << ((rank - 1) * 8 + (file + 1)));

		pawn_attacks[WHITE][square] = white_attacks;
		pawn_attacks[BLACK][square] = black_attacks;
	}
}

static void init_knight_attacks()
{
	for (int square = 0; square < 64; ++square)
	{
		uint64_t attacks = 0;
		int rank = square / 8;
		int file = square % 8;

		const int knight_actions[8][2] =
		{
			{2, 1}, {1, 2}, {-1, 2}, {-2, 1},
			{-2, -1}, {-1, -2}, {1, -2}, {2, -1}
		};

		for (const auto& action : knight_actions)
		{
			int new_rank = rank + action[0];
			int new_file = file + action[1];
			if (new_rank >= 0 && new_rank < 8 && new_file >= 0 && new_file < 8)
			{
				attacks |= (1ull << (new_rank * 8 + new_file));
			}
		}

		knight_attacks[square] = attacks;
	}
}

static void init_king_attacks()
{
	for (int square = 0; square < 64; ++square)
	{
		uint64_t attacks = 0;
		int rank = square / 8;
		int file = square % 8;

		const int king_actions[8][2] =
		{
			{1, 0}, {1, 1}, {0, 1}, {-1, 1},
			{-1, 0}, {-1, -1}, {0, -1}, {1, -1}
		};

		for (const auto& action : king_actions)
		{
			int new_rank = rank + action[0];
			int new_file = file + action[1];
			if (new_rank >= 0 && new_rank < 8 && new_file >= 0 && new_file < 8)
			{
				attacks |= (1ull << (new_rank * 8 + new_file));
			}
		}

		king_attacks[square] = attacks;
	}
}

void init_action_generator()
{
	init_dist_to_edge();
	init_pawn_attacks();
	init_knight_attacks();
	init_king_attacks();
}

static inline void add_promo_pieces(int from, int to, action_list& pseudo_legal_actions)
{
	for (int promo_piece = PROMO_KNIGHT; promo_piece <= PROMO_QUEEN; ++promo_piece)
	{
		pseudo_legal_actions.moves[pseudo_legal_actions.count++] = create_action(from, to, promo_piece);
	}
}

static inline void generate_pseudo_legal_pawn_actions(const board& chess_board, int color, action_list& pseudo_legal_actions)
{
	uint64_t pawns = chess_board.pieces[color][PAWN];
	uint64_t empty = ~(chess_board.occupied[WHITE] | chess_board.occupied[BLACK]);
	uint64_t enemy = chess_board.occupied[!color];

	uint64_t single_push = color == WHITE ? (pawns << 8) & empty : (pawns >> 8) & empty;

	while (single_push)
	{
		int to = pop_lsb(single_push);
		int from = color == WHITE ? to - 8 : to + 8;

		if ((to / 8 == 7 && color == WHITE) || (to / 8 == 0 && color == BLACK))
			add_promo_pieces(from, to, pseudo_legal_actions);
		else
			pseudo_legal_actions.moves[pseudo_legal_actions.count++] = create_action(from, to, QUIET);
	}

	uint64_t start_rank = color == WHITE ? 0x000000000000FF00ull : 0x00FF000000000000ull;
	uint64_t double_push = color == WHITE ? ((pawns & start_rank) << 16) & empty & (empty << 8) : ((pawns & start_rank) >> 16) & empty & (empty >> 8);

	while (double_push)
	{
		int to = pop_lsb(double_push);
		int from = color == WHITE ? to - 16 : to + 16;
		pseudo_legal_actions.moves[pseudo_legal_actions.count++] = create_action(from, to, DOUBLE_PAWN);
	}

	uint64_t left_capture = color == WHITE ? ((pawns & ~0x0101010101010101ull) << 7) & enemy : ((pawns & ~0x8080808080808080ull) >> 7) & enemy;
	uint64_t right_capture = color == WHITE ? ((pawns & ~0x8080808080808080ull) << 9) & enemy : ((pawns & ~0x0101010101010101ull) >> 9) & enemy;

	while (left_capture)
	{
		int to = pop_lsb(left_capture);
		int from = color == WHITE ? to - 7 : to + 7;

		if ((to / 8 == 7 && color == WHITE) || (to / 8 == 0 && color == BLACK))
			add_promo_pieces(from, to, pseudo_legal_actions);
		else
			pseudo_legal_actions.moves[pseudo_legal_actions.count++] = create_action(from, to, QUIET);
	}

	while (right_capture)
	{
		int to = pop_lsb(right_capture);
		int from = color == WHITE ? to - 9 : to + 9;

		if ((to / 8 == 7 && color == WHITE) || (to / 8 == 0 && color == BLACK))
			add_promo_pieces(from, to, pseudo_legal_actions);
		else
			pseudo_legal_actions.moves[pseudo_legal_actions.count++] = create_action(from, to, QUIET);
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
		pseudo_legal_actions.moves[pseudo_legal_actions.count++] = create_action(from, ep_square, EN_PASSANT);
	}

	if (right_ep)
	{
		int from = color == WHITE ? ep_square - 9 : ep_square + 9;
		pseudo_legal_actions.moves[pseudo_legal_actions.count++] = create_action(from, ep_square, EN_PASSANT);
	}
}

static inline void generate_pseudo_legal_knight_actions(const board& chess_board, int color, action_list& pseudo_legal_actions)
{
	uint64_t knights = chess_board.pieces[color][KNIGHT];

	while (knights)
	{
		int from = pop_lsb(knights);
		uint64_t legal_knight_actions = knight_attacks[from] & ~chess_board.occupied[color];

		while (legal_knight_actions)
		{
			int to = pop_lsb(legal_knight_actions);
			uint64_t to_mask = 1ull << to;

			uint16_t action = create_action(from, to, QUIET);
			pseudo_legal_actions.moves[pseudo_legal_actions.count++] = action;
		}
	}
}

static inline void generate_pseudo_legal_sliding_actions(const board& chess_board, int color, action_list& pseudo_legal_actions, int piece_type)
{
	uint64_t sliders = chess_board.pieces[color][piece_type];

	int starting_dir = piece_type == BISHOP ? 0 : piece_type == ROOK ? 4 : 0;
	int ending_dir = piece_type == BISHOP ? 4 : piece_type == ROOK ? 8 : 8;

	while (sliders)
	{
		int from = pop_lsb(sliders);

		for (int dir = starting_dir; dir < ending_dir; ++dir)
		{
			int to = from;

			for (int n = 1; n <= dist_to_edge[from][dir]; ++n)
			{
				to += dirs[dir];

				uint64_t to_mask = 1ull << to;
				if (chess_board.occupied[color] & to_mask)
					break;

				uint16_t action = create_action(from, to, QUIET);
				pseudo_legal_actions.moves[pseudo_legal_actions.count++] = action;

				if (chess_board.occupied[!color] & to_mask)
					break;
			}
		}
	}
}

static inline bool is_square_attacked(const board& chess_board, int square, int by_color)
{
	if (pawn_attacks[!by_color][square] & chess_board.pieces[by_color][PAWN])
		return true;

	if (knight_attacks[square] & chess_board.pieces[by_color][KNIGHT])
		return true;

	if (king_attacks[square] & chess_board.pieces[by_color][KING])
		return true;

	for (int dir = 0; dir < 4; ++dir)
	{
		int to = square;

		for (int n = 1; n <= dist_to_edge[square][dir]; ++n)
		{
			to += dirs[dir];
			uint64_t mask = 1ull << to;

			if ((chess_board.occupied[WHITE] | chess_board.occupied[BLACK]) & mask)
			{
				if (mask & (chess_board.pieces[by_color][BISHOP] | chess_board.pieces[by_color][QUEEN]))
					return true;
				break;
			}
		}
	}

	for (int dir = 4; dir < 8; ++dir)
	{
		int to = square;

		for (int n = 1; n <= dist_to_edge[square][dir]; ++n)
		{
			to += dirs[dir];
			uint64_t mask = 1ull << to;

			if ((chess_board.occupied[WHITE] | chess_board.occupied[BLACK]) & mask)
			{
				if (mask & (chess_board.pieces[by_color][ROOK] | chess_board.pieces[by_color][QUEEN]))
					return true;
				break;
			}
		}
	}

	return false;
}

static inline void generate_pseudo_legal_king_actions(const board& chess_board, int color, action_list& pseudo_legal_actions)
{
	uint64_t king = chess_board.pieces[color][KING];
	int from = lsb(king);
	uint64_t legal_king_actions = king_attacks[from] & ~chess_board.occupied[color];

	while (legal_king_actions)
	{
		int to = pop_lsb(legal_king_actions);
		uint64_t to_mask = 1ull << to;

		uint16_t action = create_action(from, to, QUIET);
		pseudo_legal_actions.moves[pseudo_legal_actions.count++] = action;
	}

	uint64_t occupied = chess_board.occupied[WHITE] | chess_board.occupied[BLACK];

	if (color == WHITE && (chess_board.castling_rights & CASTLE_WHITE_KINGSIDE_RIGHT))
	{
		if (!(occupied & ((1ull << 5) | (1ull << 6))))
		{
			if (!is_square_attacked(chess_board, 4, BLACK) &&
				!is_square_attacked(chess_board, 5, BLACK) &&
				!is_square_attacked(chess_board, 6, BLACK))
			{
				pseudo_legal_actions.moves[pseudo_legal_actions.count++] = create_action(4, 6, CASTLE_WHITE_KINGSIDE);
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
				pseudo_legal_actions.moves[pseudo_legal_actions.count++] = create_action(4, 2, CASTLE_WHITE_QUEENSIDE);
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
				pseudo_legal_actions.moves[pseudo_legal_actions.count++] = create_action(60, 62, CASTLE_BLACK_KINGSIDE);
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
				pseudo_legal_actions.moves[pseudo_legal_actions.count++] = create_action(60, 58, CASTLE_BLACK_QUEENSIDE);
			}
		}
	}
}


action_list generate_legal_actions(const board& chess_board, int color)
{
	action_list legal_actions;

	generate_pseudo_legal_pawn_actions(chess_board, color, legal_actions);
	generate_pseudo_legal_knight_actions(chess_board, color, legal_actions);
	generate_pseudo_legal_sliding_actions(chess_board, color, legal_actions, BISHOP);
	generate_pseudo_legal_sliding_actions(chess_board, color, legal_actions, ROOK);
	generate_pseudo_legal_sliding_actions(chess_board, color, legal_actions, QUEEN);
	generate_pseudo_legal_king_actions(chess_board, color, legal_actions);

	for (int i = 0; i < legal_actions.count;)
	{
		uint16_t action = legal_actions.moves[i];

		board temp = chess_board;
		temp.make_action(action);

		int king_square = lsb(temp.pieces[color][KING]);

		if (is_square_attacked(temp, king_square, !color))
			legal_actions.moves[i] = legal_actions.moves[--legal_actions.count];
		else
			++i;
	}

	return legal_actions;
}