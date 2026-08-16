#pragma once

#include "MapTool.h"

#include <variant>

class PropertyBrushTool final : public MapTool
{
public:
	PropertyBrushTool(CMapData& map, CIsoView& view);

	bool onRButtonUp(const ProjectedCoords&, const MapCoords&, MapToolMouseFlags) override;
	void onLButtonUp(const ProjectedCoords&, const MapCoords& mapCoords, MapToolMouseFlags flags) override;
	bool onKeyDown(UINT key) override;

private:
	using Properties = std::variant<std::monostate, STRUCTURE, AIRCRAFT, UNIT, INFANTRY>;
	bool Sample(const MapCoords& cell);
	bool Apply(const MapCoords& cell);
	Properties m_properties;
};
