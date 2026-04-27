#pragma once

#include <cstdint>
#include <intrin0.inl.h>

inline int lsb(const uint64_t& bb)
{
	#ifdef _MSC_VER
		return _tzcnt_u64(bb);
	#else
		return __builtin_ctzll(bb);
	#endif
}

inline int pop_lsb(uint64_t& bb)
{
	int square = lsb(bb);
	bb &= bb - 1;
	return square;
}