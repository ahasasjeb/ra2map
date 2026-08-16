#pragma once

#include <vector>

struct TheaterConfigurationEntry
{
	CString name;
	int index;
};

// Returns the built-in theaters in the order configured by FAData/FSData's
// [Theaters] section. Omitting a theater disables it in new-map dialogs.
std::vector<TheaterConfigurationEntry> GetConfiguredTheaters(bool includeYuriTheaters);
