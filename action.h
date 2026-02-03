#pragma once

#include <cstdint>
#include <string>
#include "piece.h"

enum action_flags : uint8_t
{
	QUIET = 0,

	DOUBLE_PAWN = 1,
	EN_PASSANT = 2,

	PROMO_KNIGHT = 4,
	PROMO_BISHOP = 5,
	PROMO_ROOK = 6,
	PROMO_QUEEN = 7,

	CASTLE_WHITE_KINGSIDE = 8,
	CASTLE_WHITE_QUEENSIDE = 9,
	CASTLE_BLACK_KINGSIDE = 10,
	CASTLE_BLACK_QUEENSIDE = 11
};

inline uint16_t create_action(int from, int to, int flags) { return from | (to << 6) | (flags << 12); }
inline void set_action_flags(uint16_t& action, int flags) { action = (action & 0x0FFF) | (flags << 12); }

inline int from_sq(uint16_t action) { return action & 0x3F; }
inline int to_sq(uint16_t action) { return (action >> 6) & 0x3F; }
inline int flags(uint16_t action) { return (action >> 12) & 0xF; }

inline bool is_promo(int flags) { return flags & PROMO_KNIGHT; }
inline int promo_piece(int flags) { return flags - PROMO_KNIGHT + KNIGHT; }

std::string action_to_string(uint16_t action, int color);