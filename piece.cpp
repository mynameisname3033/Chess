#include "piece.h"

int char_to_piece(char piece_char)
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