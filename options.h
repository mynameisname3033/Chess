#pragma once

#include <unordered_map>
#include <string>

constexpr int MAX_DEPTH = 64;
constexpr int TT_SIZE = 2 << 25;

extern int MAX_THINKING_TIME_MS;
extern int MIN_THINKING_TIME_MS;
extern int TIME_DIVISOR;
extern int DEPTH_TIME_CUTOFF_PERCENT;

extern int MIN_LAR_INDEX;
extern int MIN_LAR_DEPTH_REMAINING;
extern int LAR_REDUCTION_DIVISOR;

extern int MAX_HEURISTIC_VALUE;

extern int BASE_LAP_INDEX;
extern int MAX_LAP_DEPTH_REMAINING;

extern int ASPIRATION_WINDOW;

extern int DELTA_PRUNING_MARGIN;

extern int MAX_RFP_DEPTH_REMAINING;
extern int RFP_MARGIN_MULTIPLIER;

extern int MAX_FP_DEPTH_REMAINING;
extern int FP_MARGIN_MULTIPLIER;

extern int MIN_NULL_PRUNING_DEPTH_REMAINING;
extern int BASE_NULL_PRUNING_VERIFICATION_REDUCTION;
extern int NULL_PRUNING_VERIFICATION_REDUCTION_DIVISOR;

extern int MIN_IID_DEPTH_REMAINING;
extern int IID_DEPTH_REDUCTION;

extern int MIN_CHECK_EXTENSION_DEPTH_REMAINING;
extern int MAX_CHECK_EXTENSIONS;

extern int PIECE_VALUES[6];

extern std::unordered_map<std::string, int*> option_map;