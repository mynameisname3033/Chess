#include <iostream>
#include <sstream>
#include <cctype>
#include "action_generator.h"
#include "action.h"

using namespace std;

void board::print(bool white_playing) const
{
	int rankStart = white_playing ? 7 : 0;
	int rankEnd = white_playing ? -1 : 8;
	int rankStep = white_playing ? -1 : 1;

	int fileStart = white_playing ? 0 : 7;
	int fileEnd = white_playing ? 8 : -1;
	int fileStep = white_playing ? 1 : -1;

	for (int rank = rankStart; rank != rankEnd; rank += rankStep)
	{
		for (int file = fileStart; file != fileEnd; file += fileStep)
		{
			uint64_t mask = 1ull << (rank * 8 + file);

			if ((rank + file) % 2 == 0)
				cout << "\033[48;2;130;130;130m";
			else
				cout << "\033[48;2;70;70;70m";

			bool printed = false;
			uint8_t full_piece = squares[rank * 8 + file];

			if (full_piece != 0xFF)
			{
				int color = full_piece_color(full_piece);
				int piece = full_piece_piece(full_piece);

				if (color == WHITE)
					cout << "\033[38;2;245;245;245m" << type_to_piece_char[piece] << " ";
				else
					cout << "\033[38;2;0;0;0m" << type_to_piece_char[piece] << " ";

				printed = true;
			}

			if (!printed)
				cout << "  ";
		}

		cout << "\033[0m" << endl;
	}
}

bool board::load_fen(const string& fen)
{
	for (int c = 0; c < COLOR_NB; ++c)
	{
		occupied[c] = 0;
		for (int p = 0; p < PIECE_NB; ++p)
			pieces[c][p] = 0;
	}

	for (int i = 0; i < 64; ++i)
		squares[i] = 0xFF;

	castling_rights = 0;
	en_passant_square = -1;

	istringstream iss(fen);
	string placement, side, castling, ep;
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

			square++;
		}
	}

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

	return true;
}

void board::make_action(uint16_t action)
{
	int from = from_sq(action);
	int to = to_sq(action);

	int piece = full_piece_piece(squares[from]);
	int color = full_piece_color(squares[from]);

	int action_flags = flags(action);

	uint64_t from_mask = 1ull << from;
	uint64_t to_mask = 1ull << to;

	pieces[color][piece] &= ~from_mask;
	occupied[color] &= ~from_mask;

	pieces[color][piece] |= to_mask;
	occupied[color] |= to_mask;

	if (piece == KING)
	{
		if (color == WHITE)
			castling_rights &= ~(CASTLE_WHITE_KINGSIDE_RIGHT | CASTLE_WHITE_QUEENSIDE_RIGHT);
		else
			castling_rights &= ~(CASTLE_BLACK_KINGSIDE_RIGHT | CASTLE_BLACK_QUEENSIDE_RIGHT);

		if (action_flags == CASTLE_WHITE_KINGSIDE)
		{
			pieces[WHITE][ROOK] &= ~(1ull << 7);
			pieces[WHITE][ROOK] |= (1ull << 5);
			occupied[WHITE] &= ~(1ull << 7);
			occupied[WHITE] |= (1ull << 5);
			squares[5] = create_full_piece(WHITE, ROOK);
			squares[7] = 0xFF;
		}
		else if (action_flags == CASTLE_WHITE_QUEENSIDE)
		{
			pieces[WHITE][ROOK] &= ~(1ull << 0);
			pieces[WHITE][ROOK] |= (1ull << 3);
			occupied[WHITE] &= ~(1ull << 0);
			occupied[WHITE] |= (1ull << 3);
			squares[3] = create_full_piece(WHITE, ROOK);
			squares[0] = 0xFF;
		}
		else if (action_flags == CASTLE_BLACK_KINGSIDE)
		{
			pieces[BLACK][ROOK] &= ~(1ull << 63);
			pieces[BLACK][ROOK] |= (1ull << 61);
			occupied[BLACK] &= ~(1ull << 63);
			occupied[BLACK] |= (1ull << 61);
			squares[61] = create_full_piece(BLACK, ROOK);
			squares[63] = 0xFF;
		}
		else if (action_flags == CASTLE_BLACK_QUEENSIDE)
		{
			pieces[BLACK][ROOK] &= ~(1ull << 56);
			pieces[BLACK][ROOK] |= (1ull << 59);
			occupied[BLACK] &= ~(1ull << 56);
			occupied[BLACK] |= (1ull << 59);
			squares[59] = create_full_piece(BLACK, ROOK);
			squares[56] = 0xFF;
		}
	}
	else if (piece == ROOK)
	{
		if (from == 0)
			castling_rights &= ~CASTLE_WHITE_QUEENSIDE_RIGHT;
		else if (from == 7)
			castling_rights &= ~CASTLE_WHITE_KINGSIDE_RIGHT;
		else if (from == 56)
			castling_rights &= ~CASTLE_BLACK_QUEENSIDE_RIGHT;
		else if (from == 63)
			castling_rights &= ~CASTLE_BLACK_KINGSIDE_RIGHT;
	}

	uint8_t full_captured_piece = squares[to];

	if (full_captured_piece != 0xFF)
	{
		int captured_piece = full_piece_piece(full_captured_piece);

		pieces[!color][captured_piece] &= ~to_mask;
		occupied[!color] &= ~to_mask;

		if (captured_piece == ROOK)
		{
			if (to == 0)
				castling_rights &= ~CASTLE_WHITE_QUEENSIDE_RIGHT;
			if (to == 7)
				castling_rights &= ~CASTLE_WHITE_KINGSIDE_RIGHT;
			if (to == 56)
				castling_rights &= ~CASTLE_BLACK_QUEENSIDE_RIGHT;
			if (to == 63)
				castling_rights &= ~CASTLE_BLACK_KINGSIDE_RIGHT;
		}
	}

	en_passant_square = -1;

	if (piece == PAWN)
	{
		int action_flags = flags(action);

		if (action_flags & DOUBLE_PAWN)
			en_passant_square = color == WHITE ? to - 8 : to + 8;

		if (action_flags & EN_PASSANT)
		{
			int captured_pawn_square = color == WHITE ? to - 8 : to + 8;
			uint64_t captured_pawn_mask = 1ull << captured_pawn_square;
			pieces[!color][PAWN] &= ~captured_pawn_mask;
			occupied[!color] &= ~captured_pawn_mask;
			squares[captured_pawn_square] = 0xFF;
		}

		if (is_promo(action_flags))
		{
			int promo_piece = promo_piece_type(action_flags);

			pieces[color][PAWN] &= ~to_mask;
			pieces[color][promo_piece] |= to_mask;

			squares[to] = create_full_piece(color, promo_piece);
			squares[from] = 0xFF;

			return;
		}
	}

	squares[to] = squares[from];
	squares[from] = 0xFF;
}