#pragma once

#include <unordered_map>
#include "board.h"
#include "init.h"

enum TTFlag : uint8_t { EXACT, LOWERBOUND, UPPERBOUND };

struct TTEntry
{
	uint64_t key;
	uint16_t best_action;
	int score = 0;
	uint8_t depth_remaining = 0;
	uint8_t flag = EXACT;
};

struct transposition_table
{
	std::vector<TTEntry> table;
	uint64_t mask;

	transposition_table(size_t size)
	{
		table.resize(size);
		mask = size - 1;
	}

	inline TTEntry* probe(uint64_t key)
	{
		TTEntry& e = table[key & mask];
		if (e.key == key)
			return &e;
		return nullptr;
	}

	inline void add(uint64_t key, uint16_t best_action, int score, uint8_t depth_remaining, uint8_t flag)
	{
		TTEntry& e = table[key & mask];

		if (e.key != key || depth_remaining >= e.depth_remaining)
		{
			e.key = key;
			e.best_action = best_action;
			e.score = score;
			e.depth_remaining = depth_remaining;
			e.flag = flag;
		}
	}
};