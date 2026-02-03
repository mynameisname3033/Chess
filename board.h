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
	uint64_t hash;

	int king_square[COLOR_NB];
	int8_t en_passant_square;
	uint8_t castling_rights;
	int side_to_move;

	void print(int highlight) const;

	bool load_fen(const std::string& fen);
	std::string get_fen() const;

	void make_action(uint16_t action);
};
