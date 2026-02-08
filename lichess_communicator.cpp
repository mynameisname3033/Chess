#include <sstream>
#include <iostream>
#include "lichess_communicator.h"
#include "action_list.h"
#include "action_generator.h"
#include "action.h"

using namespace std;

static vector<string> split(const string& s)
{
	stringstream ss(s);
	vector<string> tokens;
	string tok;

	while (ss >> tok)
		tokens.push_back(tok);

	return tokens;
}

void set_position(board& chess_board, const string& command)
{
	vector<string> tokens = split(command);

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

		string fen;
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
			uint16_t action = string_to_action(chess_board, tokens[idx++]);
			if (action != 0)
				chess_board.make_action(action);
		}
	}
}

go_params parse_go_command(const string& input)
{
    go_params params;
    vector<string> tokens = split(input);

    for (int i = 1; i < (int)tokens.size(); i++)
    {
        string t = tokens[i];

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