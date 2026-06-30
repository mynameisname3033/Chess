#pragma once

#include <unordered_map>
#include <string>
#include <limits>
#include "piece.h"

inline constexpr int MAX_DEPTH = 128;

inline constexpr int BIG_INF = std::numeric_limits<int>::max();
inline constexpr int INF = 32500;
inline constexpr int MATE_THRESHOLD = 32000;

extern int MAX_THINKING_TIME_MS;
extern int MIN_THINKING_TIME_MS;
extern int TIME_DIVISOR;

extern int NODE_TM_BASE;
extern int NODE_TM_SCALE;
extern int NODE_TM_MIN;
extern int NODE_TM_MAX;

extern int INC_FRACTION;
extern int HARD_LIMIT_MULTIPLIER;
extern int INSTABILITY_DIVISOR;
extern int MAX_INSTABILITY_FACTOR;
extern int STABILITY_DECREMENT;
extern int MAX_STABILITY_STEPS;
extern int MIN_TIME_FACTOR;
extern int SOFT_TO_HARD_MULTIPLIER;

extern int GEN_AGE_WEIGHT;

extern int MIN_LAR_INDEX;
extern int MIN_LAR_DEPTH_REMAINING;
extern int LAR_REDUCTION_DIVISOR;

extern int MAX_HEURISTIC_VALUE;

extern int HISTORY_REDUCTION_DIVISOR;
extern int CONTINUATION_REDUCTION_DIVISOR;
extern int CAPTURE_REDUCTION_DIVISOR;
extern int HEURISTIC_REDUCTION_THRESHOLD;

extern int CAPTURE_HISTORY_SEE_SCORE_DIVISOR;

extern int CORR_WEIGHT;
extern int CORR_GRAIN;
extern int CORR_MAX;

extern int BASE_LAP_INDEX;
extern int MAX_LAP_DEPTH_REMAINING;

extern int ASPIRATION_WINDOW;

extern int MAX_QUIESCENCE_DEPTH;
extern int MAX_QUIET_QUIESCENCE_CHECK_DEPTH;

extern int MAX_RFP_DEPTH_REMAINING;
extern int RFP_MARGIN_MULTIPLIER;

extern int MAX_FP_DEPTH_REMAINING;
extern int FP_MARGIN_MULTIPLIER;

extern int MAX_RAZOR_DEPTH_REMAINING;
extern int RAZOR_MARGIN;

extern int MIN_NULL_PRUNING_DEPTH_REMAINING;
extern int BASE_NULL_PRUNING_VERIFICATION_REDUCTION;
extern int NULL_PRUNING_VERIFICATION_REDUCTION_DIVISOR;

extern int MIN_SE_DEPTH_REMAINING;
extern int SE_DEPTH_REMAINING_MIN_REDUCTION;
extern int SE_MARGIN_MULTIPLIER;
extern int SE_REDUCTION_BASE;
extern int SE_REUCTION_DIVISOR;
extern int SE_DOUBLE_EXTENSION_MARGIN;

extern int MIN_IID_DEPTH_REMAINING;
extern int IID_DEPTH_REDUCTION;

extern int MIN_CHECK_EXTENSION_DEPTH_REMAINING;
extern int MAX_CHECK_EXTENSIONS;

extern int PIECE_VALUES[PIECE_NB];

extern std::unordered_map<std::string, int*> option_map;