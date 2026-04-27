#pragma once

#include <string>

constexpr int EMBEDDINGS = 45056;
constexpr int INPUT = 512;
constexpr int H1 = 32;
constexpr int H2 = 32;
constexpr int OUTPUT = 1;

extern alignas(32) float* __restrict embeddings;
extern alignas(32) float* __restrict fc1_w;
extern alignas(32) float* __restrict fc1_b;
extern alignas(32) float* __restrict fc2_w;
extern alignas(32) float* __restrict fc2_b;
extern alignas(32) float* __restrict fc3_w;
extern alignas(32) float* __restrict fc3_b;

void init_parameters(const std::string& filename);
void destroy_parameters();