#include <cstdint>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include "action_generator.h"
#include "board.h"
#include "action.h"
#include "init.h"
#include "search.h"
#include "zobrist_hash.h"
#include "uci_communicator.h"
#include "parameters.h"
#include "options.h"

static uint64_t perft(const board& chess_board, int depth)
{
	if (depth == 0)
		return 1;

	action_list legal_actions = generate_legal_actions(chess_board);
	uint64_t nodes = 0;

	for (int i = 0; i < legal_actions.count; ++i)
	{
		uint16_t action = legal_actions.actions[i];

		board temp = chess_board;
		temp.make_action(action);
		nodes += perft(temp, depth - 1);
	}

	return nodes;
}

static uint64_t perft_divide(board& chess_board, int depth)
{
	uint64_t total_nodes = 0;
	action_list legal_actions = generate_legal_actions(chess_board);

	for (int i = 0; i < legal_actions.count; ++i)
	{
		uint16_t action = legal_actions.actions[i];

		board temp = chess_board;
		temp.make_action(action);

		uint64_t nodes = perft(temp, depth - 1);
		std::cout << action_to_string(action, temp.side_to_move) << ", nodes: " << nodes << std::endl;

		total_nodes += nodes;
	}

	return total_nodes;
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

int main()
{
	init_parameters("C:/Users/akhil/c++/repos/Chess/nn_train/nnue_params8.bin");
	init_zobrist_rng();
	init_action_generator();
	init_LAR_table();
	reset_engine();

	board chess_board = {};
	chess_board.load_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

	//chess_board.load_fen("5k1r/1p2n1p1/p1nB1pQ1/2N4p/2B5/1PP1P3/P2P1PPP/RN2K2R w KQ - 12 24");
	//chess_board.load_fen("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8");
	//chess_board.load_fen("8/2p5/8/KP1p2kr/5p2/8/4P1P1/6R1 w - - 0 1");
	//chess_board.load_fen("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
	//chess_board.load_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -");
	//chess_board.load_fen("1k6/p4pp1/8/6n1/8/8/pK6/7q b - - 1 52");

	//chess_board.load_fen("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2");

	//std::cout << chess_board.get_fen() << std::endl;
	//chess_board.print(-1);

	//auto start = std::chrono::steady_clock::now();

	//uint64_t n_moves = perft_divide(chess_board, 5);

	//auto end = std::chrono::steady_clock::now();
	//auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
	//std::cout << "Time taken: " << (duration.count() / 1000000) << " milliseconds" << std::endl;
	//std::cout << "Total moves: " << n_moves << std::endl << std::endl;

	std::ios::sync_with_stdio(false);
	std::cin.tie(nullptr);

	std::string input;

	while (std::getline(std::cin, input))
	{
		if (input == "uci")
		{
			std::cout << "id name MyEngine" << std::endl;
			std::cout << "id author Akhil" << std::endl;

			std::cout << "option name MAX_THINKING_TIME_MS type spin default 15000 min 0 max 10000000" << std::endl;
			std::cout << "option name MIN_THINKING_TIME_MS type spin default 150 min 0 max 10000000" << std::endl;
			std::cout << "option name TIME_DIVISOR type spin default 25 min 0 max 500" << std::endl;

			std::cout << "option name MIN_LAR_INDEX type spin default 6 min 0 max 218" << std::endl;
			std::cout << "option name MIN_LAR_DEPTH_REMAINING type spin default 3 min 0 max " << MAX_DEPTH << std::endl;
			std::cout << "option name LAR_REDUCTION_DIVISOR type spin default 7 min 0 max 100" << std::endl;

			std::cout << "option name MAX_HEURISTIC_VALUE type spin default 16384 min 0 max 1000000" << std::endl;
			std::cout << "option name HISTORY_REDUCTION_DIVISOR type spin default 4000 min 0 max 100000" << std::endl;

			std::cout << "option name BASE_LAP_INDEX type spin default 3 min 0 max 218" << std::endl;
			std::cout << "option name MAX_LAP_DEPTH_REMAINING type spin default 2 min 0 max " << MAX_DEPTH << std::endl;

			std::cout << "option name ASPIRATION_WINDOW type spin default 35 min 5 max 500" << std::endl;

			std::cout << "option name MAX_RFP_DEPTH_REMAINING type spin default 4 min 0 max " << MAX_DEPTH << std::endl;
			std::cout << "option name RFP_MARGIN_MULTIPLIER type spin default 150 min -2500 max 2500" << std::endl;

			std::cout << "option name MAX_FP_DEPTH_REMAINING type spin default 2 min 0 max " << MAX_DEPTH << std::endl;
			std::cout << "option name FP_MARGIN_MULTIPLIER type spin default 150 min -2500 max 2500" << std::endl;

			std::cout << "option name MIN_NULL_PRUNING_DEPTH_REMAINING type spin default 3 min 0 max " << MAX_DEPTH << std::endl;
			std::cout << "option name BASE_NULL_PRUNING_VERIFICATION_REDUCTION type spin default 3 min 0 max 10" << std::endl;
			std::cout << "option name NULL_PRUNING_VERIFICATION_REDUCTION_DIVISOR type spin default 6 min 0 max 100" << std::endl;

			std::cout << "option name MIN_IID_DEPTH_REMAINING type spin default 6 min 0 max " << MAX_DEPTH << std::endl;
			std::cout << "option name IID_DEPTH_REDUCTION type spin default 3 min 0 max 10" << std::endl;

			std::cout << "option name MIN_CHECK_EXTENSION_DEPTH_REMAINING type spin default 1 min 0 max " << MAX_DEPTH << std::endl;
			std::cout << "option name MAX_CHECK_EXTENSIONS type spin default 2 min 0 max 10" << std::endl;

			std::cout << "uciok" << std::endl;
		}
		else if (input == "isready")
		{
			std::cout << "readyok" << std::endl;
		}
		else if (input.rfind("setoption", 0) == 0)
		{
			std::vector<std::string> tokens = split(input);

			std::string name;
			int value = 0;

			for (int i = 0; i < tokens.size(); i++)
			{
				if (tokens[i] == "name" && i + 1 < tokens.size())
					name = tokens[i + 1];

				if (tokens[i] == "value" && i + 1 < tokens.size())
					value = stoi(tokens[i + 1]);
			}

			*option_map[name] = value;
			init_LAR_table();
		}
		else if (input == "ucinewgame")
		{
			chess_board.load_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
			reset_engine();
		}
		else if (input.rfind("position", 0) == 0)
		{
			set_position(chess_board, input);
		}
		else if (input.rfind("go", 0) == 0)
		{
			action_list legal_actions = generate_legal_actions(chess_board);

			if (legal_actions.count == 0)
			{
				std::cout << "bestmove 0000" << std::endl;
				continue;
			}

			go_params params = parse_go_command(input);
			uint16_t best = get_best_action(chess_board, legal_actions, params);
			std::string best_str = action_to_string(best, chess_board.side_to_move);

			std::cout << "bestmove " << best_str << std::endl;
		}
		else if (input == "quit")
		{
			break;
		}
	}

	destroy_parameters();
	return 0;
}