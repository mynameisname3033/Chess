#pragma once

#include <iostream>
#include <cstdint>
#include <string>
#include <cctype>
#include <sstream>
#include "piece.h"
#include "action.h"
#include "zobrist_hash.h"

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

	uint8_t king_square[COLOR_NB];
	int8_t en_passant_square;
	uint8_t castling_rights;

	int side_to_move;
	int halfmove_clock;

	inline void print(int highlight = -1) const
	{
		int rankStart = side_to_move == WHITE ? 7 : 0;
		int rankEnd = side_to_move == WHITE ? -1 : 8;
		int rankStep = side_to_move == WHITE ? -1 : 1;

		int fileStart = side_to_move == WHITE ? 0 : 7;
		int fileEnd = side_to_move == WHITE ? 8 : -1;
		int fileStep = side_to_move == WHITE ? 1 : -1;

		for (int rank = rankStart; rank != rankEnd; rank += rankStep)
		{
			for (int file = fileStart; file != fileEnd; file += fileStep)
			{
				int square = rank * 8 + file;

				if (square == highlight)
					std::cout << "\033[48;2;0;180;0m";
				else if ((rank + file) % 2 == 0)
					std::cout << "\033[48;2;130;130;130m";
				else
					std::cout << "\033[48;2;70;70;70m";

				bool printed = false;
				uint8_t full_piece = squares[square];

				if (full_piece != 0xFF)
				{
					int color = full_piece_color(full_piece);
					int piece = full_piece_piece(full_piece);

					if (color == WHITE)
						std::cout << "\033[38;2;245;245;245m" << piece_to_char[piece] << " ";
					else
						std::cout << "\033[38;2;0;0;0m" << piece_to_char[piece] << " ";

					printed = true;
				}

				if (!printed)
					std::cout << "  ";
			}

			std::cout << "\033[0m " << rank + 1 << std::endl;
		}

		for (int file = fileStart; file != fileEnd; file += fileStep)
		{
			std::cout << (char)(file + 'a') << " ";
		}
		std::cout << std::endl;
	}

	inline bool load_fen(const std::string& fen)
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

		hash = zobrist_hash(*this);

		return true;
	}

	inline std::string get_fen() const
	{
		std::string fen;

		for (int rank = 7; rank >= 0; --rank)
		{
			int empty_count = 0;
			for (int file = 0; file < 8; ++file)
			{
				int square = rank * 8 + file;
				uint8_t full_piece = squares[square];

				if (full_piece == 0xFF)
				{
					++empty_count;
				}
				else
				{
					if (empty_count > 0)
					{
						fen += std::to_string(empty_count);
						empty_count = 0;
					}

					int color = full_piece_color(full_piece);
					int piece = full_piece_piece(full_piece);
					char piece_char = piece_to_char[piece];

					if (color == WHITE)
						fen += piece_char;
					else
						fen += tolower(piece_char);
				}
			}
			if (empty_count > 0)
				fen += std::to_string(empty_count);
			if (rank > 0)
				fen += '/';
		}

		fen += ' ';
		fen += side_to_move == WHITE ? 'w' : 'b';
		fen += ' ';

		if (castling_rights == 0)
		{
			fen += '-';
		}
		else
		{
			if (castling_rights & CASTLE_WHITE_KINGSIDE_RIGHT)
				fen += 'K';
			if (castling_rights & CASTLE_WHITE_QUEENSIDE_RIGHT)
				fen += 'Q';
			if (castling_rights & CASTLE_BLACK_KINGSIDE_RIGHT)
				fen += 'k';
			if (castling_rights & CASTLE_BLACK_QUEENSIDE_RIGHT)
				fen += 'q';
		}

		fen += ' ';

		if (en_passant_square == -1)
		{
			fen += '-';
		}
		else
		{
			int file = en_passant_square & 7;
			int rank = en_passant_square >> 3;
			fen += static_cast<char>('a' + file);
			fen += static_cast<char>('1' + rank);
		}

		return fen;
	}

	inline void make_action(uint16_t action)
	{
		int to = to_sq(action);

		hash ^= zobrist_side;
		if (en_passant_square != -1)
			hash ^= zobrist_ep[en_passant_square & 7];

		int from = from_sq(action);
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

		uint8_t full_captured_piece = squares[to];
		if (full_captured_piece != 0xFF)
		{
			int captured_piece = full_piece_piece(full_captured_piece);
			int captured_color = full_piece_color(full_captured_piece);

			pieces[captured_color][captured_piece] &= ~to_mask;
			occupied[captured_color] &= ~to_mask;

			hash ^= zobrist_piece[captured_color][captured_piece][to];

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

				side_to_move ^= 1;

				return;
			}

			if (action_flags == DOUBLE_PAWN)
			{
				en_passant_square = color == WHITE ? to - 8 : to + 8;

				if (en_passant_square != -1)
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
	}

	inline void make_null_action()
	{
		hash ^= zobrist_side;

		if (en_passant_square != -1)
			hash ^= zobrist_ep[en_passant_square & 7];

		en_passant_square = -1;
		side_to_move ^= 1;
	}
};