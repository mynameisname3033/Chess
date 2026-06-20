#pragma once

#include <cstdint>
#include "piece.h"

extern int dist_to_edge[64][8];
extern uint64_t aligned_mask[64][64];
extern uint64_t squares_between[64][64];

extern uint64_t pawn_attacks[COLOR_NB][64];
extern uint64_t knight_attacks[64];
extern uint64_t king_attacks[64];

extern uint64_t rook_attacks[64][4096];
extern uint64_t bishop_attacks[64][512];

extern uint64_t rook_masks[64];
extern uint64_t bishop_masks[64];

extern uint64_t rook_magics[64];
extern uint64_t bishop_magics[64];

extern int rook_shifts[64];
extern int bishop_shifts[64];

static inline constexpr int dirs[8] = { 9, 7, -7, -9, 8, -8, 1, -1 };

void init_action_generator();