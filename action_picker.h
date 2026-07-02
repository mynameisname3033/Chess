#pragma once

#include <cstdint>
#include <utility>
#include "action.h"

struct action_picker
{
    action_list* legal_actions;
    int scores[218];
    int current_index;
    uint16_t tt_action;
    bool scored;

    __forceinline action_picker(action_list& legal_actions, uint16_t tt_action) : legal_actions(&legal_actions), current_index(0), tt_action(tt_action), scored(false) {}

    template <typename ScorerFunc>
    __forceinline uint16_t next(ScorerFunc score_func)
    {
        if (current_index >= legal_actions->count)
            return 0;

        if (!scored)
        {
            if (tt_action != 0)
            {
                for (int i = current_index; i < legal_actions->count; ++i)
                {
                    if (legal_actions->actions[i] == tt_action)
                    {
                        std::swap(legal_actions->actions[current_index], legal_actions->actions[i]);
                        return legal_actions->actions[current_index++];
                    }
                }
            }

            score_func(scores, current_index, legal_actions->count);
            scored = true;
        }

        if (current_index >= legal_actions->count)
            return 0;

        int best_idx = current_index;
        int best_score = scores[current_index];

        for (int i = current_index + 1; i < legal_actions->count; ++i)
        {
            if (scores[i] > best_score)
            {
                best_score = scores[i];
                best_idx = i;
            }
        }

        std::swap(legal_actions->actions[current_index], legal_actions->actions[best_idx]);
        std::swap(scores[current_index], scores[best_idx]);

        return legal_actions->actions[current_index++];
    }
};