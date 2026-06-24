#pragma once

#include <immintrin.h>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <ostream>
#include <cmath>
#include "parameters.h"
#include "bits.h"
#include "action.h"
#include "board.h"
#include "piece.h"

static inline constexpr int CASTLING_BASE = EMBEDDINGS - 4;

struct NNUE
{
	private:
		alignas(32) int16_t accumulator[COLOR_NB][EMBEDDING_DIM];

		static __forceinline float hsum_ps_avx(__m256 v)
		{
			__m128 lo = _mm256_castps256_ps128(v);
			__m128 hi = _mm256_extractf128_ps(v, 1);
			__m128 s = _mm_add_ps(lo, hi);
			s = _mm_add_ps(s, _mm_movehl_ps(s, s));
			s = _mm_add_ss(s, _mm_shuffle_ps(s, s, 1));
			return _mm_cvtss_f32(s);
		}

		static __forceinline int get_embedding_index(uint8_t king_square, int perspective, int color, int piece, int square)
		{
			int king_sq = (perspective == BLACK) ? (king_square ^ 56) : king_square;
			bool mirror = (king_sq & 7) >= 4;

			if (mirror)
				king_sq ^= 7;

			int king_bucket = ((king_sq >> 3) * 4) + (king_sq & 3);
			int actual_square = (perspective == BLACK) ? (square ^ 56) : square;

			if (mirror)
				actual_square ^= 7;

			int plane;

			if (piece == KING)
				plane = 10;
			else if (color == perspective)
				plane = piece;
			else
				plane = 5 + piece;

			return king_bucket * (11 * 64) + plane * 64 + actual_square;
		}

		static __forceinline int get_castling_embedding_index(int perspective, int castling_right_index)
		{
			if (perspective == WHITE)
				return CASTLING_BASE + castling_right_index;

			switch (castling_right_index)
			{
				case 0:
					return CASTLING_BASE + 2;
				case 1:
					return CASTLING_BASE + 3;
				case 2:
					return CASTLING_BASE + 0;
				default:
					return CASTLING_BASE + 1;
			}
		}

		__forceinline void add_embedding(int perspective, int index)
		{
			const int16_t* __restrict row = embeddings + index * EMBEDDING_DIM;
			int16_t* __restrict acc = accumulator[perspective];

			for (int i = 0; i < EMBEDDING_DIM; i += 16)
			{
				__m256i a = _mm256_load_si256((const __m256i*)(acc + i));
				__m256i b = _mm256_load_si256((const __m256i*)(row + i));
				_mm256_store_si256((__m256i*)(acc + i), _mm256_add_epi16(a, b));
			}
		}

		__forceinline void sub_embedding(int perspective, int index)
		{
			const int16_t* __restrict row = embeddings + index * EMBEDDING_DIM;
			int16_t* __restrict acc = accumulator[perspective];

			for (int i = 0; i < EMBEDDING_DIM; i += 16)
			{
				__m256i a = _mm256_load_si256((const __m256i*)(acc + i));
				__m256i b = _mm256_load_si256((const __m256i*)(row + i));
				_mm256_store_si256((__m256i*)(acc + i), _mm256_sub_epi16(a, b));
			}
		}

		__forceinline void add_piece(const uint8_t(&king_square)[COLOR_NB], int color, int piece, int square)
		{
			for (int perspective = 0; perspective < COLOR_NB; ++perspective)
			{
				if (piece == KING && color == perspective)
					continue;

				int idx = get_embedding_index(king_square[perspective], perspective, color, piece, square);
				add_embedding(perspective, idx);
			}
		}

		__forceinline void remove_piece(const uint8_t(&king_square)[COLOR_NB], int color, int piece, int square)
		{
			for (int perspective = 0; perspective < COLOR_NB; ++perspective)
			{
				if (piece == KING && color == perspective)
					continue;

				int idx = get_embedding_index(king_square[perspective], perspective, color, piece, square);
				sub_embedding(perspective, idx);
			}
		}

		__forceinline void add_castling_right(int perspective, int rights_index)
		{
			add_embedding(perspective, get_castling_embedding_index(perspective, rights_index));
		}

		__forceinline void remove_castling_right(int perspective, int rights_index)
		{
			sub_embedding(perspective, get_castling_embedding_index(perspective, rights_index));
		}

	public:
		__forceinline int forward(int color) const
		{
			const int16_t* __restrict us = accumulator[color];
			const int16_t* __restrict them = accumulator[color ^ 1];

			const __m256i zero = _mm256_setzero_si256();
			const __m256i qa = _mm256_set1_epi16((short)QA);

			alignas(32) float h1[H1];

			// FC1: SCReLU(acc)·w in integer. SCReLU = clamp(acc,0,QA)^2 (computed via
			// (clamp*w) then madd with clamp). Per-lane int32 accumulation is safe
			// (<=48 terms * 16.5M < 2^31); the final horizontal sum widens to int64.
			const float fc1_dequant = 1.0f / (float)((int64_t)QA * QA * QB);

			for (int o = 0; o < H1; o += 4)
			{
				const int8_t* __restrict w0 = fc1_w + (o + 0) * INPUT;
				const int8_t* __restrict w1 = fc1_w + (o + 1) * INPUT;
				const int8_t* __restrict w2 = fc1_w + (o + 2) * INPUT;
				const int8_t* __restrict w3 = fc1_w + (o + 3) * INPUT;

				__m256i a0 = _mm256_setzero_si256();
				__m256i a1 = _mm256_setzero_si256();
				__m256i a2 = _mm256_setzero_si256();
				__m256i a3 = _mm256_setzero_si256();

				for (int half = 0; half < 2; ++half)
				{
					const int16_t* __restrict in = half == 0 ? us : them;
					int off = half * EMBEDDING_DIM;

					for (int i = 0; i < EMBEDDING_DIM; i += 16)
					{
						// SCReLU activation, shared across the 4 outputs.
						__m256i c = _mm256_min_epi16(_mm256_max_epi16(_mm256_load_si256((const __m256i*)(in + i)), zero), qa);

						a0 = _mm256_add_epi32(a0, _mm256_madd_epi16(_mm256_mullo_epi16(c, _mm256_cvtepi8_epi16(_mm_loadu_si128((const __m128i*)(w0 + off + i)))), c));
						a1 = _mm256_add_epi32(a1, _mm256_madd_epi16(_mm256_mullo_epi16(c, _mm256_cvtepi8_epi16(_mm_loadu_si128((const __m128i*)(w1 + off + i)))), c));
						a2 = _mm256_add_epi32(a2, _mm256_madd_epi16(_mm256_mullo_epi16(c, _mm256_cvtepi8_epi16(_mm_loadu_si128((const __m128i*)(w2 + off + i)))), c));
						a3 = _mm256_add_epi32(a3, _mm256_madd_epi16(_mm256_mullo_epi16(c, _mm256_cvtepi8_epi16(_mm_loadu_si128((const __m128i*)(w3 + off + i)))), c));
					}
				}

				__m256i acc[4] = { a0, a1, a2, a3 };
				for (int k = 0; k < 4; ++k)
				{
					__m256i s64 = _mm256_add_epi64(
						_mm256_cvtepi32_epi64(_mm256_castsi256_si128(acc[k])),
						_mm256_cvtepi32_epi64(_mm256_extracti128_si256(acc[k], 1)));
					__m128i s = _mm_add_epi64(_mm256_castsi256_si128(s64), _mm256_extracti128_si256(s64, 1));
					int64_t sum = (int64_t)_mm_cvtsi128_si64(s) + (int64_t)_mm_extract_epi64(s, 1) + fc1_b[o + k];

					float out1 = (float)sum * fc1_dequant;
					h1[o + k] = out1 > 0.0f ? out1 : 0.0f;
				}
			}

			// FC2 / FC3 in float (tiny layers).
			alignas(32) float h2[H2];
			for (int o = 0; o < H2; ++o)
			{
				float s = fc2_b[o];
				const float* __restrict w = fc2_w + o * H1;
				for (int i = 0; i < H1; ++i)
					s += w[i] * h1[i];
				h2[o] = s > 0.0f ? s : 0.0f;
			}

			float out = fc3_b[0];
			for (int i = 0; i < H2; ++i)
				out += fc3_w[i] * h2[i];

			out = fminf(fmaxf(out, -0.999999f), 0.999999f);
			return (int)(atanh(out) * 400.0f);
		}

		__forceinline void build_accumulator(const board& chess_board)
		{
			std::memset(accumulator, 0, sizeof(accumulator));

			for (int color = 0; color < COLOR_NB; ++color)
			{
				for (int piece = 0; piece < PIECE_NB; ++piece)
				{
					uint64_t pieces = chess_board.pieces[color][piece];
					while (pieces)
					{
						int square = pop_lsb(pieces);

						if (pieces)
						{
							int next_square = lsb(pieces);
							for (int perspective = 0; perspective < COLOR_NB; ++perspective)
							{
								int next_idx = get_embedding_index(chess_board.king_square[perspective], perspective, color, piece, next_square);
								_mm_prefetch((const char*)(embeddings + next_idx * EMBEDDING_DIM), _MM_HINT_T0);
							}
						}

						add_piece(chess_board.king_square, color, piece, square);
					}
				}
			}

			for (int perspective = 0; perspective < COLOR_NB; ++perspective)
			{
				if (chess_board.castling_rights & CASTLE_WHITE_KINGSIDE_RIGHT)
					add_castling_right(perspective, 0);

				if (chess_board.castling_rights & CASTLE_WHITE_QUEENSIDE_RIGHT)
					add_castling_right(perspective, 1);

				if (chess_board.castling_rights & CASTLE_BLACK_KINGSIDE_RIGHT)
					add_castling_right(perspective, 2);

				if (chess_board.castling_rights & CASTLE_BLACK_QUEENSIDE_RIGHT)
					add_castling_right(perspective, 3);
			}
		}

		__forceinline void update(const board& new_board, const board& prev_board, uint16_t action)
		{
			int from = from_sq(action);
			uint8_t full_piece = prev_board.squares[from];
			int piece = full_piece_piece(full_piece);

			if (piece == KING)
			{
				build_accumulator(new_board);

				return;
			}

			int to = to_sq(action);
			int color = full_piece_color(full_piece);
			int action_flags = flags(action);

			uint8_t full_captured_piece = prev_board.squares[to];
			if (full_captured_piece != 0xFF)
			{
				int captured_piece = full_piece_piece(full_captured_piece);
				int captured_color = full_piece_color(full_captured_piece);
				remove_piece(prev_board.king_square, captured_color, captured_piece, to);
			}

			uint8_t old_rights = prev_board.castling_rights;
			uint8_t new_rights = new_board.castling_rights;

			if ((old_rights ^ new_rights) != 0)
			{
				for (int perspective = 0; perspective < COLOR_NB; ++perspective)
				{
					if ((old_rights & CASTLE_WHITE_KINGSIDE_RIGHT) && !(new_rights & CASTLE_WHITE_KINGSIDE_RIGHT))
						remove_castling_right(perspective, 0);
					else if (!(old_rights & CASTLE_WHITE_KINGSIDE_RIGHT) && (new_rights & CASTLE_WHITE_KINGSIDE_RIGHT))
						add_castling_right(perspective, 0);

					if ((old_rights & CASTLE_WHITE_QUEENSIDE_RIGHT) && !(new_rights & CASTLE_WHITE_QUEENSIDE_RIGHT))
						remove_castling_right(perspective, 1);
					else if (!(old_rights & CASTLE_WHITE_QUEENSIDE_RIGHT) && (new_rights & CASTLE_WHITE_QUEENSIDE_RIGHT))
						add_castling_right(perspective, 1);

					if ((old_rights & CASTLE_BLACK_KINGSIDE_RIGHT) && !(new_rights & CASTLE_BLACK_KINGSIDE_RIGHT))
						remove_castling_right(perspective, 2);
					else if (!(old_rights & CASTLE_BLACK_KINGSIDE_RIGHT) && (new_rights & CASTLE_BLACK_KINGSIDE_RIGHT))
						add_castling_right(perspective, 2);

					if ((old_rights & CASTLE_BLACK_QUEENSIDE_RIGHT) && !(new_rights & CASTLE_BLACK_QUEENSIDE_RIGHT))
						remove_castling_right(perspective, 3);
					else if (!(old_rights & CASTLE_BLACK_QUEENSIDE_RIGHT) && (new_rights & CASTLE_BLACK_QUEENSIDE_RIGHT))
						add_castling_right(perspective, 3);
				}
			}

			if (piece == PAWN)
			{
				if (is_promo(action_flags))
				{
					int promo = promo_piece(action_flags);

					remove_piece(prev_board.king_square, color, piece, from);
					add_piece(new_board.king_square, color, promo, to);

					return;
				}
				else if (action_flags == EN_PASSANT)
				{
					int captured_pawn_square = color == WHITE ? to - 8 : to + 8;
					remove_piece(prev_board.king_square, color ^ 1, PAWN, captured_pawn_square);
				}
			}

			remove_piece(prev_board.king_square, color, piece, from);
			add_piece(new_board.king_square, color, piece, to);
		}

		__forceinline void test(const board& chess_board)
		{
			NNUE temp = *this;
			temp.build_accumulator(chess_board);

			for (int perspective = 0; perspective < COLOR_NB; ++perspective)
			{
				for (int i = 0; i < EMBEDDING_DIM; ++i)
				{
					if (accumulator[perspective][i] != temp.accumulator[perspective][i])
					{
						std::cout << "Mismatch at perspective " << perspective << " index " << i << ": " << accumulator[perspective][i] << " vs " << temp.accumulator[perspective][i] << std::endl;
						return;
					}
				}
			}
		}
	};