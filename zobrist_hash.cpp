#include <random>
#include "zobrist_hash.h"

using namespace std;

uint64_t zobrist_piece[COLOR_NB][6][64];
uint64_t zobrist_side;
uint64_t zobrist_castle[16];
uint64_t zobrist_ep[8];

void init_zobrist_rng()
{
	mt19937_64 rng(123456);

	for (int color = 0; color < COLOR_NB; ++color)
	{
		for (int piece = 0; piece < PIECE_NB; ++piece)
		{
			for (int square = 0; square < 64; ++square)
			{
				zobrist_piece[color][piece][square] = rng();
			}
		}
	}

	zobrist_side = rng();

	for (int i = 0; i < 16; ++i)
	{
		zobrist_castle[i] = rng();
	}

	for (int i = 0; i < 8; ++i)
	{
		zobrist_ep[i] = rng();
	}
}