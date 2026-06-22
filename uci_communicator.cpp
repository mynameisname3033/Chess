#include <sstream>
#include <vector>
#include <cstdint>
#include <string>
#include "uci_communicator.h"
#include "action.h"
#include "board.h"

static std::vector<std::string> split(const std::string& s)
{
	std::stringstream ss(s);
	std::vector<std::string> tokens;
	std::string tok;

	while (ss >> tok)
		tokens.push_back(tok);

	return tokens;
}

void set_position(board& chess_board, const std::string& command)
{
	std::vector<std::string> tokens = split(command);

	if (tokens.size() < 2)
		return;

	int idx = 1;

	if (tokens[idx] == "startpos")
	{
		chess_board.load_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
		++idx;
	}
	else if (tokens[idx] == "fen")
	{
		if (tokens.size() < idx + 7)
			return;

		std::string fen;
		fen += tokens[idx + 1] + " ";
		fen += tokens[idx + 2] + " ";
		fen += tokens[idx + 3] + " ";
		fen += tokens[idx + 4] + " ";
		fen += tokens[idx + 5] + " ";
		fen += tokens[idx + 6];

		chess_board.load_fen(fen);

		idx += 7;
	}

	if (tokens[idx++] == "moves")
	{
		while (idx < (int)tokens.size())
		{
			uint16_t current_action = string_to_action(chess_board, tokens[idx++]);
			if (current_action != 0)
				chess_board.make_action(current_action);
		}
	}
}

go_params parse_go_command(const std::string& input)
{
	go_params params;
	std::vector<std::string> tokens = split(input);

	for (int i = 1; i < (int)tokens.size(); i++)
	{
		std::string t = tokens[i];

		if (t == "wtime" && i + 1 < (int)tokens.size())
			params.wtime = stoi(tokens[++i]);

		else if (t == "btime" && i + 1 < (int)tokens.size())
			params.btime = stoi(tokens[++i]);

		else if (t == "winc" && i + 1 < (int)tokens.size())
			params.winc = stoi(tokens[++i]);

		else if (t == "binc" && i + 1 < (int)tokens.size())
			params.binc = stoi(tokens[++i]);

		else if (t == "movetime" && i + 1 < (int)tokens.size())
			params.movetime = stoi(tokens[++i]);

		else if (t == "depth" && i + 1 < (int)tokens.size())
			params.depth = stoi(tokens[++i]);

		else if (t == "nodes" && i + 1 < (int)tokens.size())
			params.nodes = stoi(tokens[++i]);

		else if (t == "infinite")
			params.infinite = true;
	}

	return params;
}