#include "StdAfx.h"
#include "WaypointCodec.h"

#include <climits>

int GetWaypoint(const char* text)
{
	if (text == nullptr || *text == '\0')
		return -1;

	unsigned long long value = 0;
	for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(text); *cursor; ++cursor)
	{
		const int upper = std::toupper(*cursor);
		if (upper < 'A' || upper > 'Z')
			return -1;

		value = value * 26 + static_cast<unsigned>(upper - 'A' + 1);
		if (value > static_cast<unsigned long long>(INT_MAX) + 1)
			return -1;
	}

	return static_cast<int>(value - 1);
}

CString GetWaypoint(int waypoint)
{
	if (waypoint < 0)
		return CString();

	unsigned int value = static_cast<unsigned int>(waypoint) + 1;
	char buffer[16]{};
	char* cursor = buffer + sizeof(buffer) - 1;
	while (value > 0)
	{
		const unsigned int digit = (value - 1) % 26;
		*--cursor = static_cast<char>('A' + digit);
		value = (value - 1) / 26;
	}

	return CString(cursor);
}
