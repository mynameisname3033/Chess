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
		alignas(32) float accumulator[COLOR_NB][INPUT];

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
			const float* __restrict row = embeddings + index * EMBEDDING_DIM;

			for (int i = 0; i + 32 <= EMBEDDING_DIM; i += 32)
			{
				__m256 a0 = _mm256_load_ps(accumulator[perspective] + i + 0);
				__m256 a1 = _mm256_load_ps(accumulator[perspective] + i + 8);
				__m256 a2 = _mm256_load_ps(accumulator[perspective] + i + 16);
				__m256 a3 = _mm256_load_ps(accumulator[perspective] + i + 24);

				__m256 b0 = _mm256_load_ps(row + i + 0);
				__m256 b1 = _mm256_load_ps(row + i + 8);
				__m256 b2 = _mm256_load_ps(row + i + 16);
				__m256 b3 = _mm256_load_ps(row + i + 24);

				_mm256_store_ps(accumulator[perspective] + i + 0, _mm256_add_ps(a0, b0));
				_mm256_store_ps(accumulator[perspective] + i + 8, _mm256_add_ps(a1, b1));
				_mm256_store_ps(accumulator[perspective] + i + 16, _mm256_add_ps(a2, b2));
				_mm256_store_ps(accumulator[perspective] + i + 24, _mm256_add_ps(a3, b3));
			}
		}

		__forceinline void sub_embedding(int perspective, int index)
		{
			const float* __restrict row = embeddings + index * EMBEDDING_DIM;

			for (int i = 0; i + 32 <= EMBEDDING_DIM; i += 32)
			{
				__m256 a0 = _mm256_load_ps(accumulator[perspective] + i + 0);
				__m256 a1 = _mm256_load_ps(accumulator[perspective] + i + 8);
				__m256 a2 = _mm256_load_ps(accumulator[perspective] + i + 16);
				__m256 a3 = _mm256_load_ps(accumulator[perspective] + i + 24);

				__m256 b0 = _mm256_load_ps(row + i + 0);
				__m256 b1 = _mm256_load_ps(row + i + 8);
				__m256 b2 = _mm256_load_ps(row + i + 16);
				__m256 b3 = _mm256_load_ps(row + i + 24);

				_mm256_store_ps(accumulator[perspective] + i + 0, _mm256_sub_ps(a0, b0));
				_mm256_store_ps(accumulator[perspective] + i + 8, _mm256_sub_ps(a1, b1));
				_mm256_store_ps(accumulator[perspective] + i + 16, _mm256_sub_ps(a2, b2));
				_mm256_store_ps(accumulator[perspective] + i + 24, _mm256_sub_ps(a3, b3));
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
			alignas(32) float h1[H1];
			alignas(32) float h2[H2];

			const float* __restrict us_input = accumulator[color];
			const float* __restrict them_input = accumulator[color ^ 1];

			const __m256 zero256 = _mm256_setzero_ps();
			const __m256 ones256 = _mm256_set1_ps(1.0f);
			const __m128 zero128 = _mm_setzero_ps();

			for (int o = 0; o < H1; o += 4)
			{
				_mm_prefetch((const char*)(fc1_w + (o + 4) * INPUT), _MM_HINT_T0);
				_mm_prefetch((const char*)(fc1_w + (o + 5) * INPUT), _MM_HINT_T0);

				__m256 sums[4];
				for (int i = 0; i < 4; ++i)
				{
					sums[i] = zero256;
				}

				for (int i = 0; i < EMBEDDING_DIM; i += 8)
				{
					__m256 raw_in = _mm256_load_ps(us_input + i);

					__m256 clamped = _mm256_min_ps(_mm256_max_ps(raw_in, zero256), ones256);
					__m256 activated_in = _mm256_mul_ps(clamped, clamped);

					sums[0] = _mm256_fmadd_ps(activated_in, _mm256_load_ps(fc1_w + (o + 0) * INPUT + i), sums[0]);
					sums[1] = _mm256_fmadd_ps(activated_in, _mm256_load_ps(fc1_w + (o + 1) * INPUT + i), sums[1]);
					sums[2] = _mm256_fmadd_ps(activated_in, _mm256_load_ps(fc1_w + (o + 2) * INPUT + i), sums[2]);
					sums[3] = _mm256_fmadd_ps(activated_in, _mm256_load_ps(fc1_w + (o + 3) * INPUT + i), sums[3]);
				}

				for (int i = 0; i < EMBEDDING_DIM; i += 8)
				{
					__m256 raw_in = _mm256_load_ps(them_input + i);

					__m256 clamped = _mm256_min_ps(_mm256_max_ps(raw_in, zero256), ones256);
					__m256 activated_in = _mm256_mul_ps(clamped, clamped);

					sums[0] = _mm256_fmadd_ps(activated_in, _mm256_load_ps(fc1_w + (o + 0) * INPUT + EMBEDDING_DIM + i), sums[0]);
					sums[1] = _mm256_fmadd_ps(activated_in, _mm256_load_ps(fc1_w + (o + 1) * INPUT + EMBEDDING_DIM + i), sums[1]);
					sums[2] = _mm256_fmadd_ps(activated_in, _mm256_load_ps(fc1_w + (o + 2) * INPUT + EMBEDDING_DIM + i), sums[2]);
					sums[3] = _mm256_fmadd_ps(activated_in, _mm256_load_ps(fc1_w + (o + 3) * INPUT + EMBEDDING_DIM + i), sums[3]);
				}

				float temp[4];
				for (int i = 0; i < 4; ++i)
				{
					temp[i] = hsum_ps_avx(sums[i]) + fc1_b[o + i];
				}
				__m128 out = _mm_loadu_ps(temp);
				__m128 relu = _mm_max_ps(out, zero128);
				_mm_store_ps(&h1[o], relu);
			}

			for (int o = 0; o < H2; o += 4)
			{
				_mm_prefetch((const char*)(fc2_w + (o + 4) * H1), _MM_HINT_T0);
				_mm_prefetch((const char*)(fc2_w + (o + 5) * H1), _MM_HINT_T0);

				__m256 sums[4];
				for (int i = 0; i < 4; ++i)
				{
					sums[i] = zero256;
				}

				for (int i = 0; i < H1; i += 8)
				{
					__m256 in = _mm256_load_ps(h1 + i);
					sums[0] = _mm256_fmadd_ps(in, _mm256_load_ps(fc2_w + (o + 0) * H1 + i), sums[0]);
					sums[1] = _mm256_fmadd_ps(in, _mm256_load_ps(fc2_w + (o + 1) * H1 + i), sums[1]);
					sums[2] = _mm256_fmadd_ps(in, _mm256_load_ps(fc2_w + (o + 2) * H1 + i), sums[2]);
					sums[3] = _mm256_fmadd_ps(in, _mm256_load_ps(fc2_w + (o + 3) * H1 + i), sums[3]);
				}

				float temp[4];
				for (int i = 0; i < 4; ++i)
				{
					temp[i] = hsum_ps_avx(sums[i]) + fc2_b[o + i];
				}
				__m128 out = _mm_loadu_ps(temp);
				__m128 relu = _mm_max_ps(out, zero128);
				_mm_store_ps(&h2[o], relu);
			}

			__m256 final_sum = zero256;
			for (int i = 0; i < H2; i += 8)
			{
				__m256 v_w = _mm256_load_ps(fc3_w + i);
				__m256 v_h = _mm256_load_ps(h2 + i);
				final_sum = _mm256_fmadd_ps(v_w, v_h, final_sum);
			}

			float raw = hsum_ps_avx(final_sum) + fc3_b[0];
			raw = fminf(fmaxf(raw, -0.999999f), 0.999999f);
			return (int)(atanh(raw) * 400.0f);
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
				for (int i = 0; i < INPUT; ++i)
				{
					if (abs(accumulator[perspective][i] - temp.accumulator[perspective][i]) > 1e-6)
					{
						std::cout << "Mismatch at perspective " << perspective << " index " << i << ": " << accumulator[perspective][i] << " vs " << temp.accumulator[perspective][i] << std::endl;
						return;
					}
				}
			}
		}
	};