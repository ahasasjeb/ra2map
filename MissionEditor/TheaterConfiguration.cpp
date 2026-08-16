#include "StdAfx.h"
#include "TheaterConfiguration.h"

#include "variables.h"

#include <array>

std::vector<TheaterConfigurationEntry> GetConfiguredTheaters(bool includeYuriTheaters)
{
#ifdef RA2_MODE
	const int availableCount = includeYuriTheaters ? 6 : 3;
#else
	const int availableCount = 2;
	UNREFERENCED_PARAMETER(includeYuriTheaters);
#endif

	const std::array<CString, 6> names = {
		THEATER0, THEATER1, THEATER2, THEATER3, THEATER4, THEATER5
	};
	std::vector<TheaterConfigurationEntry> result;
	std::array<bool, 6> used{};

	const CIniFileSection* configured = g_data.GetSection("Theaters");
	if (configured != nullptr && !configured->values.empty())
	{
		for (const auto& entry : configured->values)
		{
			CString requested = entry.second;
			requested.Trim();
			for (int index = 0; index < availableCount; ++index)
			{
				if (!used[index] && requested.CompareNoCase(names[index]) == 0)
				{
					result.push_back({ names[index], index });
					used[index] = true;
					break;
				}
			}
		}
	}

	// An absent or wholly invalid section keeps the stock editor behavior.
	if (result.empty())
		for (int index = 0; index < availableCount; ++index)
			result.push_back({ names[index], index });

	return result;
}
