#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include "piece.h"
#include "action.h"

enum castling_rights : uint8_t
{
	CASTLE_WHITE_KINGSIDE_RIGHT = 1 << 0,
	CASTLE_WHITE_QUEENSIDE_RIGHT = 1 << 1,
	CASTLE_BLACK_KINGSIDE_RIGHT = 1 << 2,
	CASTLE_BLACK_QUEENSIDE_RIGHT = 1 << 3
};

struct board
{
	uint64_t pieces[COLOR_NB][PIECE_NB];
	uint64_t occupied[COLOR_NB];
	uint8_t squares[64];

	int8_t en_passant_square;
	uint8_t castling_rights;

	void print(bool white_playing) const;
	bool load_fen(const std::string& fen);

	void make_action(uint16_t action);
};
