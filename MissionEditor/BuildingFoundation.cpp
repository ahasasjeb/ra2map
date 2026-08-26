#include "StdAfx.h"
#include "BuildingFoundation.h"

#include "variables.h"

#include <array>
#include <algorithm>

namespace
{
	std::array<std::vector<BuildingFoundationCell>, TypeTableCapacity> Foundations;

	void AddRectangularFoundation(int buildingType, int width, int height)
	{
		width = std::max(width, 1);
		height = std::max(height, 1);
		auto& cells = Foundations[buildingType];
		cells.clear();
		cells.reserve(static_cast<size_t>(width) * height);
		for (int x = 0; x < height; ++x)
			for (int y = 0; y < width; ++y)
				cells.push_back({ x, y });
	}
}

void ClearBuildingFoundations()
{
	for (auto& cells : Foundations)
		cells.clear();
}

void UpdateBuildingFoundation(int buildingType, const CString& artName)
{
	if (buildingType < 0 || buildingType >= TypeTableCapacity)
		return;

	auto& cells = Foundations[buildingType];
	cells.clear();

	const CIniFileSection* section = art.GetSection(artName);
	if (section == nullptr)
	{
		AddRectangularFoundation(buildingType, 1, 1);
		buildinginfo[buildingType].w = 1;
		buildinginfo[buildingType].h = 1;
		return;
	}

	CString foundation = section->GetValueByName("Foundation", CString("1x1"));
	if (foundation.CompareNoCase("Custom") != 0)
	{
		int width = 1;
		int height = 1;
		if (sscanf_s(foundation, "%dx%d", &width, &height) != 2 &&
			sscanf_s(foundation, "%dX%d", &width, &height) != 2)
		{
			width = 1;
			height = 1;
		}
		width = std::clamp(width, 1, 255);
		height = std::clamp(height, 1, 255);
		buildinginfo[buildingType].w = static_cast<BYTE>(width);
		buildinginfo[buildingType].h = static_cast<BYTE>(height);
		AddRectangularFoundation(buildingType, width, height);
		return;
	}

	const int width = std::clamp(atoi(section->GetValueByName("Foundation.X", CString("1"))), 1, 255);
	const int height = std::clamp(atoi(section->GetValueByName("Foundation.Y", CString("1"))), 1, 255);
	buildinginfo[buildingType].w = static_cast<BYTE>(width);
	buildinginfo[buildingType].h = static_cast<BYTE>(height);

	const int maximumEntries = width * height;
	for (int i = 0; i < maximumEntries; ++i)
	{
		CString key;
		key.Format("Foundation.%d", i);
		const CString value = section->GetValueByName(key, CString());
		if (value.IsEmpty() || value.CompareNoCase("End") == 0)
			break;

		int artX = 0;
		int artY = 0;
		if (sscanf_s(value, "%d,%d", &artX, &artY) != 2)
			continue;
		if (artX < 0 || artX >= width || artY < 0 || artY >= height)
			continue;

		// Foundation coordinates use art-space axes; the legacy editor stores
		// its rectangular dimensions transposed relative to map x/y.
		cells.push_back({ artY, artX });
	}

	if (cells.empty())
		cells.push_back({ 0, 0 });
}

const std::vector<BuildingFoundationCell>& GetBuildingFoundation(int buildingType)
{
	static const std::vector<BuildingFoundationCell> Fallback{ { 0, 0 } };
	if (buildingType < 0 || buildingType >= TypeTableCapacity || Foundations[buildingType].empty())
		return Fallback;
	return Foundations[buildingType];
}
