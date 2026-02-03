#pragma once

#include <cstdint>
#include "piece.h"
#include "init.h"
#include "board.h"

extern uint64_t zobrist_piece[COLOR_NB][PIECE_NB][64];
extern uint64_t zobrist_side;
extern uint64_t zobrist_castle[16];
extern uint64_t zobrist_ep[8];

void init_zobrist_rng();

inline uint64_t zobrist_hash(const board& chess_board)
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
		int en_passant_file = chess_board.en_passant_square % 8;
		key ^= zobrist_ep[en_passant_file];
	}

	return key;
}