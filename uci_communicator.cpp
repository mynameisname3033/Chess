#include <sstream>
#include <vector>
#include <cstdint>
#include <string>
#include <climits>
#include "uci_communicator.h"
#include "action.h"
#include "board.h"
#include "action_generator.h"

static int parse_int(const std::string& s, int fallback)
{
	try
	{
		long long v = std::stoll(s);
		if (v > INT_MAX) return INT_MAX;
		if (v < INT_MIN) return INT_MIN;
		return (int)v;
	}
	catch (...)
	{
		return fallback;
	}
}

static long long parse_ll(const std::string& s, long long fallback)
{
	try
	{
		return std::stoll(s);
	}
	catch (...)
	{
		return fallback;
	}
}

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
		++idx;

		std::string fen;
		int fields = 0;

		while (idx < (int)tokens.size() && tokens[idx] != "moves")
		{
			if (fields > 0)
				fen += " ";
			fen += tokens[idx++];
			++fields;
		}

		if (fields < 4)
			return;

		if (fields == 4)
			fen += " 0 1";
		else if (fields == 5)
			fen += " 1";

		chess_board.load_fen(fen);
	}

	if (idx < (int)tokens.size() && tokens[idx++] == "moves")
	{
		while (idx < (int)tokens.size())
		{
			uint16_t current_action = string_to_action(chess_board, tokens[idx++]);
			if (current_action == 0)
				break;

			action_list legal_actions = generate_legal_actions(chess_board);
			bool is_legal = false;
			for (int i = 0; i < legal_actions.count; ++i)
			{
				if (legal_actions.actions[i] == current_action)
				{
					is_legal = true;
					break;
				}
			}

			if (!is_legal)
				break;

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
			params.wtime = parse_int(tokens[++i], -1);

		else if (t == "btime" && i + 1 < (int)tokens.size())
			params.btime = parse_int(tokens[++i], -1);

		else if (t == "winc" && i + 1 < (int)tokens.size())
			params.winc = parse_int(tokens[++i], 0);

		else if (t == "binc" && i + 1 < (int)tokens.size())
			params.binc = parse_int(tokens[++i], 0);

		else if (t == "movestogo" && i + 1 < (int)tokens.size())
			params.movestogo = parse_int(tokens[++i], -1);

		else if (t == "movetime" && i + 1 < (int)tokens.size())
			params.movetime = parse_int(tokens[++i], -1);

		else if (t == "depth" && i + 1 < (int)tokens.size())
			params.depth = parse_int(tokens[++i], -1);

		else if (t == "nodes" && i + 1 < (int)tokens.size())
			params.nodes = parse_ll(tokens[++i], -1);

		else if (t == "infinite")
			params.infinite = true;

		else if (t == "ponder")
			params.ponder = true;
	}

	return params;
}