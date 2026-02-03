#pragma once

#include "board.h"
#include "action_list.h"

extern long long pawn_time;
extern long long knight_time;
extern long long bishop_time;
extern long long rook_time;
extern long long queen_time;
extern long long king_time;
extern long long legality_check_time;

action_list generate_legal_actions(const board& chess_board);
inline bool is_square_attacked(const board& chess_board, int square, int by_color);