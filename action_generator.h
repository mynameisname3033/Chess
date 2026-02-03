#pragma once

#include "board.h"
#include "action_list.h"

void init_action_generator();
action_list generate_legal_actions(const board& chess_board, int color);