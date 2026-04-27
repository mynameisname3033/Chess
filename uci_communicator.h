#pragma once

#include <string>
#include "board.h"

struct go_params
{
	int wtime = -1;
	int btime = -1;
	int winc = 0;
	int binc = 0;

	int movetime = -1;
	int depth = -1;
	int nodes = -1;

	bool infinite = false;
};

void set_position(board& chess_board, const std::string& command);
go_params parse_go_command(const std::string& input);