#pragma once

#include "MapTool.h"

#include <array>

constexpr size_t PROPERTY_BRUSH_FIELD_COUNT = 14;

enum class PropertyBrushObjectType
{
	Structure,
	Infantry,
	Unit,
	Aircraft
};

struct PropertyBrushSettings
{
	PropertyBrushObjectType objectType = PropertyBrushObjectType::Structure;
	std::array<CString, PROPERTY_BRUSH_FIELD_COUNT> values{};
	std::array<bool, PROPERTY_BRUSH_FIELD_COUNT> selected{};

	bool HasSelectedFields() const;
};

size_t GetPropertyBrushFieldCount(PropertyBrushObjectType objectType);

class PropertyBrushTool final : public MapTool
{
public:
	PropertyBrushTool(CMapData& map, CIsoView& view, const PropertyBrushSettings& settings);

	bool onRButtonUp(const ProjectedCoords&, const MapCoords&, MapToolMouseFlags) override;
	void onLButtonUp(const ProjectedCoords&, const MapCoords& mapCoords, MapToolMouseFlags flags) override;
	void onMouseMove(const ProjectedCoords&, const MapCoords& mapCoords, MapToolMouseFlags flags) override;
	bool onKeyDown(UINT key) override;

private:
	bool Apply(const MapCoords& cell);
	bool ApplyStructure(DWORD position);
	bool ApplyInfantry(DWORD position);
	bool ApplyUnit(DWORD position);
	bool ApplyAircraft(DWORD position);
	PropertyBrushSettings m_settings;
	MapCoords m_lastAppliedCell{ -1, -1 };
};
