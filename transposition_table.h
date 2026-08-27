#pragma once

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <xmmintrin.h>
#include "options.h"

static inline constexpr int DEFAULT_HASH_MB = 512;
static inline constexpr int CLUSTER_SIZE = 4;

enum TTFlag : uint8_t { EXACT, LOWERBOUND, UPPERBOUND };

struct alignas(16) TTEntry
{
	uint64_t key;
	uint16_t best_action;
	int16_t score;
	int16_t static_eval;
	uint8_t depth_remaining;
	uint8_t flag : 2;
	uint8_t is_quiescence : 1;
	uint8_t generation : 5;
};

static_assert(sizeof(TTEntry) == 16, "TTEntry must stay one quarter cache line");

struct alignas(64) TTCluster
{
	TTEntry entries[CLUSTER_SIZE];
};

struct transposition_table
{
	private:
		TTCluster* table;
		uint64_t mask;

		uint64_t used = 0;
		uint64_t total_slots = 0;

		uint8_t current_generation = 0;

		__forceinline int replace_score(const TTEntry& e) const
		{
			int depth = e.is_quiescence ? -1 : e.depth_remaining;
			int age = (current_generation - e.generation) & 31;
			return depth - (age * GEN_AGE_WEIGHT);
		}

	public:

		__forceinline void new_generation()
		{
			current_generation = (current_generation + 1) & 31;
		}

		transposition_table(int mb = DEFAULT_HASH_MB) : table(nullptr), mask(0)
		{
			resize(mb);
		}

		~transposition_table()
		{
			delete[] table;
		}

		// Reallocates the table to (approximately, rounded down to a power
		// of two clusters) the requested size in MB. Wipes all entries.
		__forceinline void resize(int mb)
		{
			uint64_t bytes = (uint64_t)std::max(mb, 1) * 1024ull * 1024ull;
			uint64_t cluster_count = bytes / sizeof(TTCluster);
			if (cluster_count < 1)
				cluster_count = 1;

			uint64_t pow2 = 1;
			while ((pow2 << 1) <= cluster_count)
				pow2 <<= 1;

			delete[] table;
			table = new TTCluster[pow2];
			mask = pow2 - 1;
			total_slots = pow2 * CLUSTER_SIZE;
			clear();
		}

		__forceinline void prefetch(uint64_t key) const
		{
			_mm_prefetch((const char*)&table[key & mask], _MM_HINT_T0);
		}

		__forceinline TTEntry* probe(uint64_t key)
		{
			TTCluster& cluster = table[key & mask];
			for (int i = 0; i < CLUSTER_SIZE; ++i)
			{
				if (cluster.entries[i].key == key)
				{
					cluster.entries[i].generation = current_generation;
					return &cluster.entries[i];
				}
			}
			return nullptr;
		}

		__forceinline void add(uint64_t key, uint16_t best_action, int16_t score, int16_t static_eval, uint8_t depth_remaining, uint8_t flag, bool is_quiescence)
		{
			TTCluster& cluster = table[key & mask];
			int target_index = -1;

			for (int i = 0; i < CLUSTER_SIZE; ++i)
			{
				if (cluster.entries[i].key == key)
				{
					target_index = i;
					break;
				}
			}

			if (target_index != -1)
			{
				TTEntry& e = cluster.entries[target_index];
				e.generation = current_generation;

				if ((depth_remaining + 2 > (int)e.depth_remaining) || flag == EXACT || (e.is_quiescence && !is_quiescence))
				{
					if (best_action != 0)
						e.best_action = best_action;

					e.score = score;
					e.depth_remaining = depth_remaining;
					e.flag = flag & 0x03;
					e.is_quiescence = is_quiescence;
				}
				else if (best_action != 0)
				{
					e.best_action = best_action;
				}

				if (static_eval != -INF)
				{
					e.static_eval = static_eval;
				}

				return;
			}

			for (int i = 0; i < CLUSTER_SIZE; ++i)
			{
				if (cluster.entries[i].key == 0)
				{
					target_index = i;
					++used;
					break;
				}
			}

			if (target_index == -1)
			{
				target_index = 0;
				int worst_score = replace_score(cluster.entries[0]);

				for (int i = 1; i < CLUSTER_SIZE; ++i)
				{
					int s = replace_score(cluster.entries[i]);
					if (s < worst_score)
					{
						worst_score = s;
						target_index = i;
					}
				}
			}

			TTEntry& e = cluster.entries[target_index];
			e.key = key;
			e.best_action = best_action;
			e.score = score;
			e.static_eval = static_eval;
			e.depth_remaining = depth_remaining;
			e.flag = flag & 0x03;
			e.is_quiescence = is_quiescence;
			e.generation = current_generation;
		}

		__forceinline int hashfull() const
		{
			return (1000 * used) / total_slots;
		}

		__forceinline void clear()
		{
			std::memset(table, 0, sizeof(TTCluster) * (mask + 1));
			used = 0;
			current_generation = 0;
		}
	};