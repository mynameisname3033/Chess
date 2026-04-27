#pragma once

#include <cstdint>

enum TTFlag : uint8_t { EXACT, LOWERBOUND, UPPERBOUND };

struct TTEntry
{
	uint64_t key;
	uint16_t best_action;
	int score = 0;
	int static_eval = 0;
	uint8_t depth_remaining = 0;
	uint8_t flag = EXACT;
};

struct transposition_table
{
	private:
		TTEntry* table;
		uint64_t mask;

	public:
		uint64_t used = 0;

		transposition_table(int size)
		{
			table = new TTEntry[size];
			mask = size - 1;
			clear();
		}

		~transposition_table()
		{
			delete[] table;
		}

		inline void prefetch(uint64_t key) const
		{
			_mm_prefetch((const char*)&table[key & mask], _MM_HINT_T0);
		}

		inline TTEntry* probe(uint64_t key)
		{
			TTEntry& e = table[key & mask];
			if (e.key == key)
				return &e;
			return nullptr;
		}

		inline void add(uint64_t key, uint16_t best_action, int score, int static_eval, uint8_t depth_remaining, uint8_t flag)
		{
			TTEntry& e = table[key & mask];

			if (e.key != key || depth_remaining > e.depth_remaining || (depth_remaining == e.depth_remaining && flag == EXACT))
			{
				if (e.key == 0)
					++used;

				e.key = key;
				e.best_action = best_action;
				e.score = score;
				e.static_eval = static_eval;
				e.depth_remaining = depth_remaining;
				e.flag = flag;
			}
		}

		inline void clear()
		{
			memset(table, 0, sizeof(TTEntry) * (mask + 1));
			used = 0;
		}
};