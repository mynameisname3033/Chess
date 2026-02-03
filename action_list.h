#pragma once

#include <cstdint>

struct action_list
{
	uint16_t moves[256];
	int count = 0;
};