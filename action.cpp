#include <string>
#include "action.h"
#include "piece.h"

using namespace std;

string action_to_string(uint16_t action, int color)
{
	int from = from_sq(action);
	int to = to_sq(action);

	char file_from = 'a' + (from % 8);
	char rank_from = '1' + (from / 8);
	char file_to = 'a' + (to % 8);
	char rank_to = '1' + (to / 8);

	if (color == BLACK)
	{
		file_from = 'a' + (7 - (from % 8));
		rank_from = '1' + (7 - (from / 8));
		file_to = 'a' + (7 - (to % 8));
		rank_to = '1' + (7 - (to / 8));
	}

	return string() + file_from + rank_from + file_to + rank_to + ", flags: " + to_string(flags(action));
}