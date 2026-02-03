#pragma once

#include "board.h"
#include "action_list.h"

uint16_t bot_play(const board& chess_board, action_list& legal_actions);