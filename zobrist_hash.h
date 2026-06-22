#pragma once

#include <cstdint>
#include "piece.h"

struct board;

extern uint64_t zobrist_piece[COLOR_NB][PIECE_NB][64];
extern uint64_t zobrist_side;
extern uint64_t zobrist_castle[16];
extern uint64_t zobrist_ep[8];

void init_zobrist_rng();
uint64_t zobrist_hash(const board& chess_board);