#include <algorithm>
#include <random>
#include <cmath>
#include <cstdint>
#include <intrin0.inl.h>
#include "piece.h"
#include "init.h"
#include "bits.h"

int dist_to_edge[64][8];
uint64_t aligned_mask[64][64];
uint64_t squares_between[64][64];

uint64_t pawn_attacks[COLOR_NB][64];
uint64_t knight_attacks[64];
uint64_t king_attacks[64];

uint64_t rook_attacks[64][4096];
uint64_t bishop_attacks[64][512];

uint64_t rook_masks[64];
uint64_t bishop_masks[64];

uint64_t rook_magics[64];
uint64_t bishop_magics[64];

int rook_shifts[64];
int bishop_shifts[64];

static void init_pawn_attacks()
{
	for (int square = 0; square < 64; ++square)
	{
		uint64_t white_attacks = 0;
		uint64_t black_attacks = 0;

		int rank = square >> 3;
		int file = square & 7;

		if (rank < 7 && file > 0)
			white_attacks |= (1ull << ((rank + 1) * 8 + (file - 1)));
		if (rank < 7 && file < 7)
			white_attacks |= (1ull << ((rank + 1) * 8 + (file + 1)));
		if (rank > 0 && file > 0)
			black_attacks |= (1ull << ((rank - 1) * 8 + (file - 1)));
		if (rank > 0 && file < 7)
			black_attacks |= (1ull << ((rank - 1) * 8 + (file + 1)));

		pawn_attacks[WHITE][square] = white_attacks;
		pawn_attacks[BLACK][square] = black_attacks;
	}
}

static void init_knight_attacks()
{
	for (int square = 0; square < 64; ++square)
	{
		uint64_t attacks = 0;
		int rank = square >> 3;
		int file = square & 7;

		const int knight_actions[8][2] =
		{
			{2, 1}, {1, 2}, {-1, 2}, {-2, 1},
			{-2, -1}, {-1, -2}, {1, -2}, {2, -1}
		};

		for (const auto& action : knight_actions)
		{
			int new_rank = rank + action[0];
			int new_file = file + action[1];
			if (new_rank >= 0 && new_rank < 8 && new_file >= 0 && new_file < 8)
			{
				attacks |= (1ull << (new_rank * 8 + new_file));
			}
		}

		knight_attacks[square] = attacks;
	}
}

static void init_king_attacks()
{
	for (int square = 0; square < 64; ++square)
	{
		uint64_t attacks = 0;
		int rank = square >> 3;
		int file = square & 7;

		const int king_actions[8][2] =
		{
			{1, 0}, {1, 1}, {0, 1}, {-1, 1},
			{-1, 0}, {-1, -1}, {0, -1}, {1, -1}
		};

		for (const auto& action : king_actions)
		{
			int new_rank = rank + action[0];
			int new_file = file + action[1];
			if (new_rank >= 0 && new_rank < 8 && new_file >= 0 && new_file < 8)
			{
				attacks |= (1ull << (new_rank * 8 + new_file));
			}
		}

		king_attacks[square] = attacks;
	}
}

static void compute_rook_masks()
{
	for (int square = 0; square < 64; ++square)
	{
		uint64_t mask = 0;

		for (int dir = 4; dir < 8; ++dir)
		{
			int to = square;

			for (int n = 1; n < dist_to_edge[square][dir]; ++n)
			{
				to += dirs[dir];
				mask |= (1ull << to);
			}
		}

		rook_shifts[square] = 64 - __popcnt64(mask);
		rook_masks[square] = mask;
	}
}

static void compute_bishop_masks()
{
	for (int square = 0; square < 64; ++square)
	{
		uint64_t mask = 0;

		for (int dir = 0; dir < 4; ++dir)
		{
			int to = square;

			for (int n = 1; n < dist_to_edge[square][dir]; ++n)
			{
				to += dirs[dir];
				mask |= (1ull << to);
			}
		}
		
		bishop_shifts[square] = 64 - __popcnt64(mask);
		bishop_masks[square] = mask;
	}
}

static __forceinline uint64_t compute_bishop_attack(int square, uint64_t blockers)
{
	uint64_t attacks = 0;

	for (int dir = 0; dir < 4; ++dir)
	{
		int to = square;
		for (int n = 1; n <= dist_to_edge[square][dir]; ++n)
		{
			to += dirs[dir];
			uint64_t to_mask = 1ull << to;
			attacks |= to_mask;
			if (blockers & to_mask)
				break;
		}
	}

	return attacks;
}

static __forceinline uint64_t compute_rook_attack(int square, uint64_t blockers)
{
	uint64_t attacks = 0;

	for (int dir = 4; dir < 8; ++dir)
	{
		int to = square;

		for (int n = 1; n <= dist_to_edge[square][dir]; ++n)
		{
			to += dirs[dir];
			uint64_t to_mask = 1ull << to;
			attacks |= to_mask;
			if (blockers & to_mask)
				break;
		}
	}

	return attacks;
}

static __forceinline uint64_t subset_to_bb(int subset, uint64_t mask)
{
	uint64_t bb = 0;
	int bit_index = 0;

	while (mask)
	{
		int square = lsb(mask);
		mask &= mask - 1;
		if (subset & (1 << bit_index++))
			bb |= (1ull << square);
	}

	return bb;
}

static __forceinline bool test_bishop_magic(uint64_t magic, int square)
{
	if (__popcnt64((bishop_masks[square] * magic) & 0xFF00000000000000ull) < 6)
		return false;

	static uint64_t used[512];
	static bool filled[512];

	memset(used, 0, sizeof(used));
	memset(filled, 0, sizeof(filled));

	int relevant_bits = __popcnt64(bishop_masks[square]);
	int expected_size = 1 << relevant_bits;

	for (int subset = 0; subset < expected_size; ++subset)
	{
		uint64_t blockers = subset_to_bb(subset, bishop_masks[square]);
		uint64_t attack = compute_bishop_attack(square, blockers);
		int index = (blockers * magic) >> bishop_shifts[square];

		if (!filled[index])
		{
			used[index] = attack;
			filled[index] = true;
		}
		else if (used[index] != attack)
		{
			return false;
		}
	}

	memcpy(bishop_attacks[square], used, expected_size * sizeof(uint64_t));
	return true;
}

static __forceinline bool test_rook_magic(uint64_t magic, int square)
{
	if (__popcnt64((rook_masks[square] * magic) & 0xFF00000000000000ull) < 6)
		return false;

	static uint64_t used[4096];
	static bool filled[4096];

	memset(used, 0, sizeof(used));
	memset(filled, 0, sizeof(filled));

	int relevant_bits = __popcnt64(rook_masks[square]);
	int expected_size = 1 << relevant_bits;

	for (int subset = 0; subset < expected_size; ++subset)
	{
		uint64_t blockers = subset_to_bb(subset, rook_masks[square]);
		uint64_t attack = compute_rook_attack(square, blockers);
		int index = (blockers * magic) >> rook_shifts[square];

		if (!filled[index])
		{
			used[index] = attack;
			filled[index] = true;
		}
		else if (used[index] != attack)
		{
			return false;
		}
	}

	memcpy(rook_attacks[square], used, expected_size * sizeof(uint64_t));
	return true;
}

static void init_magic_numbers()
{
	std::mt19937_64 rng(123456);

	for (int square = 0; square < 64; ++square)
	{
		while (true)
		{
			uint64_t magic = rng() & rng() & rng();
			if (test_bishop_magic(magic, square))
			{
				bishop_magics[square] = magic;
				break;
			}
		}

		while (true)
		{
			uint64_t magic = rng() & rng() & rng();
			if (test_rook_magic(magic, square))
			{
				rook_magics[square] = magic;
				break;
			}
		}
	}
}

static void init_dist_to_edge()
{
	for (int square = 0; square < 64; ++square)
	{
		int rank = square >> 3;
		int file = square & 7;

		dist_to_edge[square][0] = std::min(7 - rank, 7 - file);
		dist_to_edge[square][1] = std::min(7 - rank, file);
		dist_to_edge[square][2] = std::min(rank, 7 - file);
		dist_to_edge[square][3] = std::min(rank, file);
		dist_to_edge[square][4] = 7 - rank;
		dist_to_edge[square][5] = rank;
		dist_to_edge[square][6] = 7 - file;
		dist_to_edge[square][7] = file;
	}
}

static __forceinline int sign(int x) { return (x > 0) - (x < 0); }

static void init_aligned_mask()
{
	for (int a = 0; a < 64; ++a)
	{
		for (int b = 0; b < 64; ++b)
		{
			aligned_mask[a][b] = 0;

			if (a == b)
			{
				aligned_mask[a][b] = (1ull << a);
				continue;
			}

			int ra = a >> 3, fa = a & 7;
			int rb = b >> 3, fb = b & 7;

			int dr = sign(rb - ra);
			int df = sign(fb - fa);

			if (!((dr == 0 && df != 0) ||
				(df == 0 && dr != 0) ||
				(abs(rb - ra) == abs(fb - fa))))
			{
				continue;
			}

			uint64_t mask = 0;

			int r = ra, f = fa;
			while (r >= 0 && r < 8 && f >= 0 && f < 8)
			{
				int sq = r * 8 + f;
				mask |= (1ull << sq);
				r += dr;
				f += df;
			}

			r = ra - dr;
			f = fa - df;
			while (r >= 0 && r < 8 && f >= 0 && f < 8)
			{
				int sq = r * 8 + f;
				mask |= (1ull << sq);
				r -= dr;
				f -= df;
			}

			aligned_mask[a][b] = mask;
		}
	}
}

static void init_squares_between()
{
	for (int a = 0; a < 64; a++)
	{
		for (int b = 0; b < 64; b++)
		{
			squares_between[a][b] = 0;

			if (a == b)
				continue;

			int file_a = a & 7;
			int rank_a = a >> 3;

			int file_b = b & 7;
			int rank_b = b >> 3;

			int df = file_b - file_a;
			int dr = rank_b - rank_a;

			int step = 0;

			if (df == 0)
			{
				step = (dr > 0) ? 8 : -8;
			}
			else if (dr == 0)
			{
				step = (df > 0) ? 1 : -1;
			}
			else if (abs(df) == abs(dr))
			{
				if (df > 0 && dr > 0) step = 9;
				if (df < 0 && dr > 0) step = 7;
				if (df > 0 && dr < 0) step = -7;
				if (df < 0 && dr < 0) step = -9;
			}
			else
			{
				continue;
			}

			int square = a + step;
			while (square != b)
			{
				squares_between[a][b] |= 1ull << square;
				square += step;
			}
		}
	}
}

void init_action_generator()
{
	init_dist_to_edge();
	init_aligned_mask();
	init_squares_between();

	init_pawn_attacks();
	init_knight_attacks();
	init_king_attacks();

	compute_rook_masks();
	compute_bishop_masks();
	init_magic_numbers();
}