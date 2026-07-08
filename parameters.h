#pragma once

#include <string>
#include <cstdint>

inline constexpr int EMBEDDINGS = 22532;
inline constexpr int EMBEDDING_DIM = 384;
inline constexpr int INPUT = EMBEDDING_DIM * 2;
inline constexpr int H1 = 32;
inline constexpr int H2 = 32;
inline constexpr int OUTPUT = 1;

inline constexpr int QA = 255;
inline constexpr int QB = 64;

extern alignas(32) int16_t* __restrict embeddings;
extern alignas(32) int8_t* __restrict fc1_w;
extern alignas(32) int32_t* __restrict fc1_b;
extern alignas(32) float* __restrict fc2_w;
extern alignas(32) float* __restrict fc2_b;
extern alignas(32) float* __restrict fc3_w;
extern alignas(32) float* __restrict fc3_b;

void init_parameters(const std::string& filename);
void destroy_parameters();