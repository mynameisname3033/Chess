#pragma once

#include <cstdint>

enum color { WHITE, BLACK, COLOR_NB };
enum piece { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, PIECE_NB };

inline constexpr char piece_to_char[PIECE_NB] = { 'P','N','B','R','Q','K' };

__forceinline uint8_t create_full_piece(int color, int piece) { return (color << 3) | piece; }
__forceinline int full_piece_color(uint8_t full_piece) { return full_piece >> 3; }
__forceinline int full_piece_piece(uint8_t full_piece) { return full_piece & 0b111; }