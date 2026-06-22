#include <random>
#include <cstdint>
#include <intrin0.inl.h>
#include "zobrist_hash.h"
#include "board.h"
#include "bits.h"
#include "piece.h"

uint64_t zobrist_piece[COLOR_NB][6][64];
uint64_t zobrist_side;
uint64_t zobrist_castle[16];
uint64_t zobrist_ep[8];

void init_zobrist_rng()
{
	std::random_device rd;
	std::mt19937_64 gen_64(rd());
	std::uniform_int_distribution<uint64_t> distrib_64;

	for (int color = 0; color < COLOR_NB; ++color)
	{
		for (int piece = 0; piece < PIECE_NB; ++piece)
		{
			for (int square = 0; square < 64; ++square)
			{
				zobrist_piece[color][piece][square] = distrib_64(gen_64);
			}
		}
	}

	zobrist_side = distrib_64(gen_64);

	for (int i = 0; i < 16; ++i)
	{
		zobrist_castle[i] = distrib_64(gen_64);
	}

	for (int i = 0; i < 8; ++i)
	{
		zobrist_ep[i] = distrib_64(gen_64);
	}
}

uint64_t zobrist_hash(const board& chess_board)
{
	uint64_t key = 0;

	for (int color = 0; color < COLOR_NB; ++color)
	{
		for (int piece = 0; piece < PIECE_NB; ++piece)
		{
			uint64_t bb = chess_board.pieces[color][piece];
			while (bb)
			{
				int square = pop_lsb(bb);
				key ^= zobrist_piece[color][piece][square];
			}
		}
	}

	if (chess_board.side_to_move == BLACK)
		key ^= zobrist_side;

	key ^= zobrist_castle[chess_board.castling_rights];

	if (chess_board.en_passant_square != -1)
	{
		int en_passant_file = chess_board.en_passant_square & 7;
		key ^= zobrist_ep[en_passant_file];
	}

	return key;
}