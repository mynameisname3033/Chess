#define _CRT_SECURE_NO_WARNINGS

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <iostream>
#ifdef _MSC_VER
	#include <malloc.h>
#endif
#include "parameters.h"

alignas(32) int16_t* __restrict embeddings = nullptr;
alignas(32) int8_t* __restrict fc1_w = nullptr;
alignas(32) int32_t* __restrict fc1_b = nullptr;
alignas(32) float* __restrict fc2_w = nullptr;
alignas(32) float* __restrict fc2_b = nullptr;
alignas(32) float* __restrict fc3_w = nullptr;
alignas(32) float* __restrict fc3_b = nullptr;

void init_parameters(const std::string& filename)
{
	FILE* f = fopen(filename.c_str(), "rb");
	if (!f)
	{
		std::cerr << "Failed to open parameters file" << std::endl;
		exit(1);
	}

	embeddings = (int16_t*)_mm_malloc(sizeof(int16_t) * EMBEDDINGS * EMBEDDING_DIM, 32);
	fc1_w = (int8_t*)_mm_malloc(sizeof(int8_t) * H1 * INPUT, 32);
	fc1_b = (int32_t*)_mm_malloc(sizeof(int32_t) * H1, 32);
	fc2_w = (float*)_mm_malloc(sizeof(float) * H2 * H1, 32);
	fc2_b = (float*)_mm_malloc(sizeof(float) * H2, 32);
	fc3_w = (float*)_mm_malloc(sizeof(float) * OUTPUT * H2, 32);
	fc3_b = (float*)_mm_malloc(sizeof(float) * OUTPUT, 32);

	auto check = [&](size_t expected, size_t got)
	{
		if (expected != got)
		{
			std::cerr << "Parameter file corrupted" << std::endl;
			exit(1);
		}
	};

	check(EMBEDDINGS * EMBEDDING_DIM, fread(embeddings, sizeof(int16_t), EMBEDDINGS * EMBEDDING_DIM, f));
	check(H1 * INPUT, fread(fc1_w, sizeof(int8_t), H1 * INPUT, f));
	check(H1, fread(fc1_b, sizeof(int32_t), H1, f));
	check(H2 * H1, fread(fc2_w, sizeof(float), H2 * H1, f));
	check(H2, fread(fc2_b, sizeof(float), H2, f));
	check(OUTPUT * H2, fread(fc3_w, sizeof(float), OUTPUT * H2, f));
	check(OUTPUT, fread(fc3_b, sizeof(float), OUTPUT, f));

	fclose(f);
}

void destroy_parameters()
{
	_mm_free(embeddings);
	_mm_free(fc1_w);
	_mm_free(fc1_b);
	_mm_free(fc2_w);
	_mm_free(fc2_b);
	_mm_free(fc3_w);
	_mm_free(fc3_b);
}