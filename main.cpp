#include <cstdint>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <thread>
#include <chrono>
#include "action_generator.h"
#include "board.h"
#include "action.h"
#include "init.h"
#include "search.h"
#include "nnue.h"
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

int main(int argc, char* argv[])
{
	std::string exe_dir;
	if (argc > 0 && argv[0])
	{
		std::string exe_path = argv[0];
		size_t last_slash = exe_path.find_last_of("/\\");
		if (last_slash != std::string::npos)
			exe_dir = exe_path.substr(0, last_slash + 1);
	}

	bool params_loaded =
		init_parameters("C:/Users/akhil/c++/repos/Chess/nn_train/nnue_params8_q.bin") ||
		init_parameters(exe_dir + "nnue_params8_q.bin") ||
		init_parameters("nnue_params8_q.bin");

	if (!params_loaded)
	{
		std::cerr << "Failed to open NNUE parameters file" << std::endl;
		return 1;
	}

	init_zobrist_rng();
	init_action_generator();
	init_LAR_table();
	reset_engine();

	board chess_board = {};
	chess_board.load_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

	std::ios::sync_with_stdio(false);
	std::cin.tie(nullptr);

	std::string input;

	std::thread search_thread;

	auto join_search = [&search_thread]()
	{
		if (search_thread.joinable())
		{
			stop_search();
			search_thread.join();
		}
	};

	while (std::getline(std::cin, input))
	{
		if (input == "uci")
		{
			std::ostringstream uci_info;

			uci_info << "id name MyEngine\n";
			uci_info << "id author Akhil\n";

			uci_info << "option name Ponder type check default false\n";
			uci_info << "option name Hash type spin default 512 min 1 max 16384\n";

			uci_info << "option name MOVE_OVERHEAD_MS type spin default 10 min 0 max 5000\n";
			uci_info << "option name MAX_THINKING_TIME_MS type spin default 15000 min 0 max 10000000\n";
			uci_info << "option name MIN_THINKING_TIME_MS type spin default 5 min 0 max 10000000\n";
			uci_info << "option name TIME_DIVISOR type spin default 25 min 1 max 500\n";

			uci_info << "option name NODE_TM_BASE type spin default 152 min 0 max 400\n";
			uci_info << "option name NODE_TM_SCALE type spin default 120 min 0 max 400\n";
			uci_info << "option name NODE_TM_MIN type spin default 60 min 1 max 400\n";
			uci_info << "option name NODE_TM_MAX type spin default 140 min 1 max 400\n";

			uci_info << "option name INC_FRACTION type spin default 75 min 0 max 200\n";
			uci_info << "option name HARD_LIMIT_MULTIPLIER type spin default 250 min 100 max 1000\n";
			uci_info << "option name INSTABILITY_DIVISOR type spin default 5 min 1 max 100\n";
			uci_info << "option name MAX_INSTABILITY_FACTOR type spin default 150 min 100 max 500\n";
			uci_info << "option name STABILITY_DECREMENT type spin default 2 min 0 max 50\n";
			uci_info << "option name MAX_STABILITY_STEPS type spin default 5 min 0 max 50\n";
			uci_info << "option name MIN_TIME_FACTOR type spin default 40 min 1 max 100\n";
			uci_info << "option name SOFT_TO_HARD_MULTIPLIER type spin default 2 min 1 max 10\n";

			uci_info << "option name GEN_AGE_WEIGHT type spin default 8 min 0 max 256\n";

			uci_info << "option name MIN_LAR_INDEX type spin default 4 min 0 max 218\n";
			uci_info << "option name MIN_LAR_DEPTH_REMAINING type spin default 3 min 0 max " << MAX_DEPTH << "\n";
			uci_info << "option name LAR_REDUCTION_DIVISOR type spin default 7 min 1 max 100\n";

			uci_info << "option name MAX_HISTORY_EXTENSION type spin default 1 min 0 max 4\n";
			uci_info << "option name HISTORY_EXTENSION_THRESHOLD type spin default 3 min 1 max 20\n";

			uci_info << "option name PV_DEPTH_ADDITION type spin default 2 min 0 max 10\n";

			uci_info << "option name MAX_HEURISTIC_VALUE type spin default 16384 min 0 max 1000000\n";

			uci_info << "option name HISTORY_REDUCTION_DIVISOR type spin default 4000 min 1 max 100000\n";
			uci_info << "option name CONTINUATION_REDUCTION_DIVISOR type spin default 3000 min 1 max 100000\n";
			uci_info << "option name CAPTURE_REDUCTION_DIVISOR type spin default 3000 min 1 max 100000\n";
			uci_info << "option name HEURISTIC_REDUCTION_THRESHOLD type spin default 20 min 0 max 100000\n";

			uci_info << "option name CAPTURE_HISTORY_SEE_SCORE_DIVISOR type spin default 16 min 1 max 4096\n";

			uci_info << "option name CORR_WEIGHT type spin default 8 min 1 max 1024\n";
			uci_info << "option name CORR_GRAIN type spin default 256 min 1 max 4096\n";
			uci_info << "option name CORR_MAX type spin default 16384 min 1 max 1048576\n";

			uci_info << "option name BASE_LAP_INDEX type spin default 3 min 0 max 218\n";
			uci_info << "option name MAX_LAP_DEPTH_REMAINING type spin default 2 min 0 max " << MAX_DEPTH << "\n";

			uci_info << "option name ASPIRATION_WINDOW type spin default 35 min 5 max 500\n";

			uci_info << "option name MAX_QUIESCENCE_DEPTH type spin default 30 min 0 max " << MAX_DEPTH << "\n";
			uci_info << "option name MAX_QUIET_QUIESCENCE_CHECK_DEPTH type spin default 2 min 0 max " << MAX_DEPTH << "\n";

			uci_info << "option name MAX_RFP_DEPTH_REMAINING type spin default 3 min 0 max " << MAX_DEPTH << "\n";
			uci_info << "option name RFP_MARGIN_MULTIPLIER type spin default 150 min -2500 max 2500\n";

			uci_info << "option name MAX_FP_DEPTH_REMAINING type spin default 1 min 0 max " << MAX_DEPTH << "\n";
			uci_info << "option name FP_MARGIN_MULTIPLIER type spin default 150 min -2500 max 2500\n";

			uci_info << "option name MAX_RAZOR_DEPTH_REMAINING type spin default 3 min 0 max " << MAX_DEPTH << "\n";
			uci_info << "option name RAZOR_MARGIN type spin default 300 min 0 max 2000\n";

			uci_info << "option name MIN_NULL_PRUNING_DEPTH_REMAINING type spin default 3 min 0 max " << MAX_DEPTH << "\n";
			uci_info << "option name BASE_NULL_PRUNING_VERIFICATION_REDUCTION type spin default 3 min 0 max 10\n";
			uci_info << "option name NULL_PRUNING_VERIFICATION_REDUCTION_DIVISOR type spin default 6 min 1 max 100\n";

			uci_info << "option name MIN_SE_DEPTH_REMAINING type spin default 6 min 0 max " << MAX_DEPTH << "\n";
			uci_info << "option name SE_DEPTH_REMAINING_MIN_REDUCTION type spin default 3 min 0 max 100\n";
			uci_info << "option name SE_MARGIN_MULTIPLIER type spin default 2 min 0 max 1000\n";
			uci_info << "option name SE_REDUCTION_BASE type spin default 1 min 0 max 100\n";
			uci_info << "option name SE_REUCTION_DIVISOR type spin default 2 min 1 max 100\n";
			uci_info << "option name SE_DOUBLE_EXTENSION_MARGIN type spin default 300 min 0 max 2000\n";

			uci_info << "option name MIN_IID_DEPTH_REMAINING type spin default 6 min 0 max " << MAX_DEPTH << "\n";
			uci_info << "option name IID_DEPTH_REDUCTION type spin default 3 min 0 max 10\n";

			uci_info << "option name MIN_CHECK_EXTENSION_DEPTH_REMAINING type spin default 2 min 0 max " << MAX_DEPTH << "\n";
			uci_info << "option name MAX_CHECK_EXTENSIONS type spin default 2 min 0 max 10\n";

			uci_info << "uciok";

			uci_send(uci_info.str());
		}
		else if (input == "isready")
		{
			uci_send("readyok");
		}
		else if (input == "eval")
		{
			NNUE net;
			net.build_accumulator(chess_board);
			uci_send("eval " + std::to_string(net.forward(chess_board.side_to_move)));
		}
		else if (input.rfind("setoption", 0) == 0)
		{
			join_search();

			std::vector<std::string> tokens = split(input);

			std::string name;
			std::string value_str;

			for (int i = 0; i < tokens.size(); i++)
			{
				if (tokens[i] == "name" && i + 1 < tokens.size())
					name = tokens[i + 1];

				if (tokens[i] == "value" && i + 1 < tokens.size())
					value_str = tokens[i + 1];
			}

			if (name == "Hash")
			{
				try
				{
					resize_tt_mb(std::stoi(value_str));
				}
				catch (...) {}
			}
			else
			{
				auto it = option_map.find(name);
				if (it != option_map.end())
				{
					try
					{
						*it->second = std::stoi(value_str);
						init_LAR_table();
					}
					catch (...) {}
				}
			}
		}
		else if (input == "ucinewgame")
		{
			join_search();

			chess_board.load_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
			reset_engine();
		}
		else if (input.rfind("position", 0) == 0)
		{
			join_search();

			set_position(chess_board, input);
		}
		else if (input.rfind("go", 0) == 0)
		{
			join_search();

			std::vector<std::string> go_tokens = split(input);

			if (go_tokens.size() >= 2 && go_tokens[1] == "perft")
			{
				int depth;
				try { depth = std::stoi(go_tokens.at(2)); }
				catch (...) { continue; }

				auto start = std::chrono::steady_clock::now();

				uint64_t total = 0;

				if (depth >= 1)
				{
					action_list perft_actions = generate_legal_actions(chess_board);
					for (int i = 0; i < perft_actions.count; ++i)
					{
						uint16_t action = perft_actions.actions[i];

						board temp = chess_board;
						temp.make_action(action);

						uint64_t n = perft(temp, depth - 1);
						std::cout << action_to_string(action, 0) << ": " << n << std::endl;

						total += n;
					}
				}
				else
				{
					total = 1;
				}

				auto end = std::chrono::steady_clock::now();
				long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

				std::cout << "\nNodes searched: " << total << std::endl;
				std::cout << "Time: " << ms << " ms" << std::endl;

				continue;
			}

			action_list legal_actions = generate_legal_actions(chess_board);

			if (legal_actions.count == 0)
			{
				uci_send("bestmove 0000");
				continue;
			}

			go_params params = parse_go_command(input);

			prepare_search(params.ponder);

			search_thread = std::thread([&chess_board, legal_actions, params]() mutable
			{
				uint16_t best = get_best_action(chess_board, legal_actions, params);

				std::string out = "bestmove " + action_to_string(best, 0);
				uint16_t ponder = get_ponder_action();
				if (ponder != 0)
					out += " ponder " + action_to_string(ponder, 0);

				uci_send(out);
			});
		}
		else if (input == "stop")
		{
			join_search();
		}
		else if (input == "ponderhit")
		{
			ponderhit();
		}
		else if (input == "quit")
		{
			break;
		}
	}

	join_search();

	destroy_parameters();
	return 0;
}