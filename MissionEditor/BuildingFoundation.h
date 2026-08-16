#pragma once

#include <vector>

struct BuildingFoundationCell
{
	int x;
	int y;
};

// Populates the cached footprint for one building type from art.ini. The
// resulting coordinates use the map's x/y axes and include rectangular and
// Ares-style Foundation=Custom definitions.
void UpdateBuildingFoundation(int buildingType, const CString& artName);
void ClearBuildingFoundations();
const std::vector<BuildingFoundationCell>& GetBuildingFoundation(int buildingType);
bool IsCustomBuildingFoundation(int buildingType);
