#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include "action.h"
#include "board.h"
#include "piece.h"

std::string action_to_string(uint16_t action, int color)
{
	int from = from_sq(action);
	int to = to_sq(action);

	char file_from = 'a' + (from & 7);
	char rank_from = '1' + (from >> 3);
	char file_to = 'a' + (to & 7);
	char rank_to = '1' + (to >> 3);

	std::string action_str;
	action_str += file_from;
	action_str += rank_from;
	action_str += file_to;
	action_str += rank_to;

	uint8_t action_flags = flags(action);

	if (is_promo(action_flags))
	{
		char promo = tolower(piece_to_char[promo_piece(action_flags)]);
		action_str += promo;
	}

	return action_str;
}

uint16_t string_to_action(const board& chess_board, const std::string& action_str)
{
	if (action_str.size() < 4)
		return 0;

	int from = (action_str[1] - '1') * 8 + (action_str[0] - 'a');
	int to = (action_str[3] - '1') * 8 + (action_str[2] - 'a');

	uint16_t action = create_action(from, to, QUIET);

	uint8_t moving_piece = chess_board.squares[from] != 0xFF ? full_piece_piece(chess_board.squares[from]) : 0;
	int color = chess_board.squares[from] != 0xFF ? full_piece_color(chess_board.squares[from]) : WHITE;

	if (moving_piece == KING)
	{
		if (color == WHITE)
		{
			if (from == 4 && to == 6) { set_action_flags(action, CASTLE_WHITE_KINGSIDE); return action; }
			if (from == 4 && to == 2) { set_action_flags(action, CASTLE_WHITE_QUEENSIDE); return action; }
		}
		else
		{
			if (from == 60 && to == 62) { set_action_flags(action, CASTLE_BLACK_KINGSIDE); return action; }
			if (from == 60 && to == 58) { set_action_flags(action, CASTLE_BLACK_QUEENSIDE); return action; }
		}
	}

	if (moving_piece == PAWN)
	{
		int start_rank = color == WHITE ? 1 : 6;
		int double_push_rank = color == WHITE ? 3 : 4;

		if (abs(to - from) == 16 && (from >> 3) == start_rank)
		{
			set_action_flags(action, DOUBLE_PAWN);
			return action;
		}

		if (to == chess_board.en_passant_square)
		{
			set_action_flags(action, EN_PASSANT);
			return action;
		}

		if ((color == WHITE && to >> 3 == 7) || (color == BLACK && to >> 3 == 0))
		{
			if (action_str.size() == 5)
			{
				char promo = action_str[4];
				int promo_piece = 0;
				if (promo == 'n') promo_piece = KNIGHT;
				if (promo == 'b') promo_piece = BISHOP;
				if (promo == 'r') promo_piece = ROOK;
				if (promo == 'q') promo_piece = QUEEN;

				if (promo_piece != 0)
				{
					int promo_flag = promo_piece - KNIGHT + PROMO_KNIGHT;
					set_action_flags(action, promo_flag);
					return action;
				}
			}
		}
	}

	return action;
}