#pragma once

#include <cstdint>
#include <string>
#include "board.h"
#include "uci_communicator.h"
#include "action.h"

void reset_engine();
void resize_tt_mb(int mb);
void init_LAR_table();
uint16_t get_best_action(const board& chess_board, action_list& legal_actions, const go_params& params);

void prepare_search(bool ponder);
void stop_search();
void ponderhit();
uint16_t get_ponder_action();
void uci_send(const std::string& line);