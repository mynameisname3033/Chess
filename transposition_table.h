#pragma once

#include <unordered_map>
#include "board.h"
#include "init.h"

enum TTFlag : uint8_t { EXACT, LOWERBOUND, UPPERBOUND };

struct TTEntry
{
	uint16_t best_action;
	int score = 0;
	uint8_t depth_remaining = 0;
	uint8_t flag = EXACT;
};

struct transposition_table
{
	std::unordered_map<uint64_t, TTEntry> tt;

	transposition_table(int reserve);

	inline const TTEntry* probe(uint64_t key) const
	{
		auto it = tt.find(key);
		if (it == tt.end())
			return nullptr;

		return &it->second;
	}

	inline void add(uint64_t key, uint16_t best_action, int score, uint8_t depth_remaining, uint8_t flag)
	{
		auto result = tt.try_emplace(key, TTEntry { best_action, score, depth_remaining, flag } );
		if (result.second)
			return;

		TTEntry& entry = result.first->second;
		if (depth_remaining >= entry.depth_remaining)
		{
			entry.best_action = best_action;
			entry.score = score;
			entry.depth_remaining = depth_remaining;
			entry.flag = flag;
		}
	}
};