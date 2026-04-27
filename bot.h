#pragma once

#include <cstdint>
#include "board.h"
#include "uci_communicator.h"
#include "action.h"

void bot_reset();
void init_LAR_table();
uint16_t get_best_action(const board& chess_board, action_list& legal_actions, const go_params& params);