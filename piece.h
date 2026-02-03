#pragma once

enum color { WHITE, BLACK, COLOR_NB };
enum piece { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, PIECE_NB };

constexpr char type_to_piece_char[PIECE_NB] = { 'P','N','B','R','Q','K' };
inline int piece_char_to_type(char piece_char)
{
	switch (piece_char)
	{
	case 'P':
	case 'p':
		return PAWN;
	case 'N':
	case 'n':
		return KNIGHT;
	case 'B':
	case 'b':
		return BISHOP;
	case 'R':
	case 'r':
		return ROOK;
	case 'Q':
	case 'q':
		return QUEEN;
	case 'K':
	case 'k':
		return KING;
	default:
		return PIECE_NB;
	}
}

inline uint8_t create_full_piece(int color, int piece) { return (color << 3) | piece; }
inline int full_piece_color(uint8_t p) { return p >> 3; }
inline int full_piece_piece(uint8_t p) { return p & 0b111; }