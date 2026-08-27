#pragma once

#include <cstdint>
#include <string>
#include <cctype>
#include <sstream>
#include <intrin0.inl.h>
#include "piece.h"
#include "action.h"
#include "zobrist_hash.h"
#include "options.h"

enum castling_rights : uint8_t
{
	CASTLE_WHITE_KINGSIDE_RIGHT = 1 << 0,
	CASTLE_WHITE_QUEENSIDE_RIGHT = 1 << 1,
	CASTLE_BLACK_KINGSIDE_RIGHT = 1 << 2,
	CASTLE_BLACK_QUEENSIDE_RIGHT = 1 << 3
};

struct board
{
	uint64_t pieces[COLOR_NB][PIECE_NB];
	uint64_t occupied[COLOR_NB];
	uint8_t squares[64];
	uint64_t hash;
	uint64_t pawn_king_hash;

	uint64_t repetition_stack[128];
	int repetition_idx;
	int halfmove_clock;

	uint8_t king_square[COLOR_NB];
	int8_t en_passant_square;
	uint8_t castling_rights;

	int side_to_move;

	__forceinline bool load_fen(const std::string& fen)
	{
		for (int color = 0; color < COLOR_NB; ++color)
		{
			occupied[color] = 0;
			for (int piece = 0; piece < PIECE_NB; ++piece)
				pieces[color][piece] = 0;
		}

		for (int square = 0; square < 64; ++square)
			squares[square] = 0xFF;

		castling_rights = 0;
		en_passant_square = -1;

		std::istringstream iss(fen);
		std::string placement, side, castling, ep;
		iss >> placement >> side >> castling >> ep;

		int fen_halfmove = 0;
		if (iss >> fen_halfmove)
		{
			halfmove_clock = fen_halfmove;
		}
		else
		{
			halfmove_clock = 0;
		}

		int square = 56;

		for (char c : placement)
		{
			if (c == '/')
			{
				square -= 16;
			}
			else if (isdigit(c))
			{
				square += (c - '0');
			}
			else
			{
				int color = isupper(c) ? WHITE : BLACK;
				int piece;

				switch (tolower(c))
				{
					case 'p': piece = PAWN;   break;
					case 'n': piece = KNIGHT; break;
					case 'b': piece = BISHOP; break;
					case 'r': piece = ROOK;   break;
					case 'q': piece = QUEEN;  break;
					case 'k': piece = KING;   break;
					default: return false;
				}

				uint64_t mask = 1ull << square;
				pieces[color][piece] |= mask;
				occupied[color] |= mask;
				squares[square] = create_full_piece(color, piece);

				if (piece == KING)
					king_square[color] = square;

				++square;
			}
		}

		side_to_move = (side == "w") ? WHITE : BLACK;

		if (castling != "-")
		{
			for (char c : castling)
			{
				switch (c)
				{
					case 'K': castling_rights |= CASTLE_WHITE_KINGSIDE_RIGHT; break;
					case 'Q': castling_rights |= CASTLE_WHITE_QUEENSIDE_RIGHT; break;
					case 'k': castling_rights |= CASTLE_BLACK_KINGSIDE_RIGHT; break;
					case 'q': castling_rights |= CASTLE_BLACK_QUEENSIDE_RIGHT; break;
				}
			}
		}

		if (ep != "-")
		{
			int file = ep[0] - 'a';
			int rank = ep[1] - '1';
			en_passant_square = rank * 8 + file;
		}

		pawn_king_hash = 0;
		for (int color = 0; color < COLOR_NB; ++color)
		{
			uint64_t pawns = pieces[color][PAWN];
			while (pawns)
			{
				int sq = (int)_tzcnt_u64(pawns);
				pawns &= pawns - 1;
				pawn_king_hash ^= zobrist_piece[color][PAWN][sq];
			}
			pawn_king_hash ^= zobrist_piece[color][KING][king_square[color]];
		}

		hash = zobrist_hash(*this);

		repetition_idx = 0;
		repetition_stack[repetition_idx++] = hash;

		return true;
	}

	__forceinline void make_action(uint16_t action)
	{
		hash ^= zobrist_side;
		if (en_passant_square != -1)
			hash ^= zobrist_ep[en_passant_square & 7];

		int from = from_sq(action);
		int to = to_sq(action);
		int action_flags = flags(action);

		int piece = full_piece_piece(squares[from]);
		int color = full_piece_color(squares[from]);

		uint64_t from_mask = 1ull << from;
		uint64_t to_mask = 1ull << to;

		pieces[color][piece] &= ~from_mask;
		occupied[color] &= ~from_mask;

		pieces[color][piece] |= to_mask;
		occupied[color] |= to_mask;

		hash ^= zobrist_piece[color][piece][from];
		hash ^= zobrist_piece[color][piece][to];

		if (piece == PAWN || piece == KING)
		{
			pawn_king_hash ^= zobrist_piece[color][piece][from];
			pawn_king_hash ^= zobrist_piece[color][piece][to];
		}

		uint8_t full_captured_piece = squares[to];
		if (full_captured_piece != 0xFF)
		{
			int captured_piece = full_piece_piece(full_captured_piece);
			int captured_color = full_piece_color(full_captured_piece);

			pieces[captured_color][captured_piece] &= ~to_mask;
			occupied[captured_color] &= ~to_mask;

			hash ^= zobrist_piece[captured_color][captured_piece][to];

			if (captured_piece == PAWN)
				pawn_king_hash ^= zobrist_piece[captured_color][PAWN][to];

			if (captured_piece == ROOK)
			{
				hash ^= zobrist_castle[castling_rights];

				if (to == 0)
					castling_rights &= ~CASTLE_WHITE_QUEENSIDE_RIGHT;
				if (to == 7)
					castling_rights &= ~CASTLE_WHITE_KINGSIDE_RIGHT;
				if (to == 56)
					castling_rights &= ~CASTLE_BLACK_QUEENSIDE_RIGHT;
				if (to == 63)
					castling_rights &= ~CASTLE_BLACK_KINGSIDE_RIGHT;

				hash ^= zobrist_castle[castling_rights];
			}
		}

		if ((piece == PAWN) || (full_captured_piece != 0xFF) || (action_flags == EN_PASSANT))
		{
			halfmove_clock = 0;
			repetition_idx = 0;
		}
		else
		{
			halfmove_clock++;
		}

		en_passant_square = -1;

		if (piece == PAWN)
		{
			if (is_promo(action_flags))
			{
				int promo = promo_piece(action_flags);

				pieces[color][PAWN] &= ~to_mask;
				pieces[color][promo] |= to_mask;

				squares[to] = create_full_piece(color, promo);
				squares[from] = 0xFF;

				hash ^= zobrist_piece[color][PAWN][to];
				hash ^= zobrist_piece[color][promo][to];

				pawn_king_hash ^= zobrist_piece[color][PAWN][to];

				side_to_move ^= 1;

				if (repetition_idx < 128)
					repetition_stack[repetition_idx++] = hash;

				return;
			}

			if (action_flags == DOUBLE_PAWN)
			{
				en_passant_square = color == WHITE ? to - 8 : to + 8;
				hash ^= zobrist_ep[en_passant_square & 7];
			}

			if (action_flags == EN_PASSANT)
			{
				int captured_pawn_square = color == WHITE ? to - 8 : to + 8;
				uint64_t captured_pawn_mask = 1ull << captured_pawn_square;

				pieces[color ^ 1][PAWN] &= ~captured_pawn_mask;
				occupied[color ^ 1] &= ~captured_pawn_mask;

				squares[captured_pawn_square] = 0xFF;
				hash ^= zobrist_piece[color ^ 1][PAWN][captured_pawn_square];

				pawn_king_hash ^= zobrist_piece[color ^ 1][PAWN][captured_pawn_square];
			}
		}
		else if (piece == KING)
		{
			hash ^= zobrist_castle[castling_rights];

			if (color == WHITE)
				castling_rights &= ~(CASTLE_WHITE_KINGSIDE_RIGHT | CASTLE_WHITE_QUEENSIDE_RIGHT);
			else
				castling_rights &= ~(CASTLE_BLACK_KINGSIDE_RIGHT | CASTLE_BLACK_QUEENSIDE_RIGHT);

			hash ^= zobrist_castle[castling_rights];

			if (action_flags == CASTLE_WHITE_KINGSIDE)
			{
				pieces[WHITE][ROOK] &= ~(1ull << 7);
				occupied[WHITE] &= ~(1ull << 7);

				pieces[WHITE][ROOK] |= (1ull << 5);
				occupied[WHITE] |= (1ull << 5);

				squares[5] = create_full_piece(WHITE, ROOK);
				squares[7] = 0xFF;

				hash ^= zobrist_piece[WHITE][ROOK][7];
				hash ^= zobrist_piece[WHITE][ROOK][5];
			}
			else if (action_flags == CASTLE_WHITE_QUEENSIDE)
			{
				pieces[WHITE][ROOK] &= ~(1ull << 0);
				occupied[WHITE] &= ~(1ull << 0);

				pieces[WHITE][ROOK] |= (1ull << 3);
				occupied[WHITE] |= (1ull << 3);

				squares[3] = create_full_piece(WHITE, ROOK);
				squares[0] = 0xFF;

				hash ^= zobrist_piece[WHITE][ROOK][0];
				hash ^= zobrist_piece[WHITE][ROOK][3];
			}
			else if (action_flags == CASTLE_BLACK_KINGSIDE)
			{
				pieces[BLACK][ROOK] &= ~(1ull << 63);
				occupied[BLACK] &= ~(1ull << 63);

				pieces[BLACK][ROOK] |= (1ull << 61);
				occupied[BLACK] |= (1ull << 61);

				squares[61] = create_full_piece(BLACK, ROOK);
				squares[63] = 0xFF;

				hash ^= zobrist_piece[BLACK][ROOK][63];
				hash ^= zobrist_piece[BLACK][ROOK][61];
			}
			else if (action_flags == CASTLE_BLACK_QUEENSIDE)
			{
				pieces[BLACK][ROOK] &= ~(1ull << 56);
				occupied[BLACK] &= ~(1ull << 56);

				pieces[BLACK][ROOK] |= (1ull << 59);
				occupied[BLACK] |= (1ull << 59);

				squares[59] = create_full_piece(BLACK, ROOK);
				squares[56] = 0xFF;

				hash ^= zobrist_piece[BLACK][ROOK][56];
				hash ^= zobrist_piece[BLACK][ROOK][59];
			}

			king_square[color] = to;

			squares[to] = squares[from];
			squares[from] = 0xFF;

			side_to_move ^= 1;

			if (repetition_idx < 128)
				repetition_stack[repetition_idx++] = hash;

			return;
		}
		else if (piece == ROOK)
		{
			hash ^= zobrist_castle[castling_rights];

			if (from == 0)
				castling_rights &= ~CASTLE_WHITE_QUEENSIDE_RIGHT;
			else if (from == 7)
				castling_rights &= ~CASTLE_WHITE_KINGSIDE_RIGHT;
			else if (from == 56)
				castling_rights &= ~CASTLE_BLACK_QUEENSIDE_RIGHT;
			else if (from == 63)
				castling_rights &= ~CASTLE_BLACK_KINGSIDE_RIGHT;

			hash ^= zobrist_castle[castling_rights];
		}

		squares[to] = squares[from];
		squares[from] = 0xFF;

		side_to_move ^= 1;

		if (repetition_idx < 128)
			repetition_stack[repetition_idx++] = hash;
	}

	__forceinline void make_null_action()
	{
		hash ^= zobrist_side;

		if (en_passant_square != -1)
			hash ^= zobrist_ep[en_passant_square & 7];

		en_passant_square = -1;
		side_to_move ^= 1;

		++halfmove_clock;

		repetition_idx = 0;
		if (repetition_idx < 128)
			repetition_stack[repetition_idx++] = hash;
	}

	__forceinline bool is_draw(int ply) const
	{
		if (halfmove_clock >= 100)
			return true;

		int start = repetition_idx - 3;

		int root_idx = repetition_idx - ply - 1;
		int matches = 0;

		for (int i = start; i >= 0; i -= 2)
		{
			if (repetition_stack[i] == hash)
			{
				if (i >= root_idx)
					return true;

				if (++matches >= 2)
					return true;
			}
		}

		if (pieces[WHITE][PAWN] || pieces[BLACK][PAWN] ||
			pieces[WHITE][ROOK] || pieces[BLACK][ROOK] ||
			pieces[WHITE][QUEEN] || pieces[BLACK][QUEEN])
		{
			return false;
		}

		int white_knights = __popcnt64(pieces[WHITE][KNIGHT]);
		int black_knights = __popcnt64(pieces[BLACK][KNIGHT]);
		int white_bishops = __popcnt64(pieces[WHITE][BISHOP]);
		int black_bishops = __popcnt64(pieces[BLACK][BISHOP]);

		int total_minors = white_knights + black_knights + white_bishops + black_bishops;

		if (total_minors <= 1)
		{
			return true;
		}

		if (total_minors == 2 && white_bishops == 1 && black_bishops == 1)
		{
			int white_bishop_square = _tzcnt_u64(pieces[WHITE][BISHOP]);
			int black_bishop_square = _tzcnt_u64(pieces[BLACK][BISHOP]);

			int white_bishop_color = ((white_bishop_square >> 3) + (white_bishop_square & 7)) % 2;
			int black_bishop_color = ((black_bishop_square >> 3) + (black_bishop_square & 7)) % 2;

			if (white_bishop_color == black_bishop_color)
			{
				return true;
			}
		}

		return false;
	}
};