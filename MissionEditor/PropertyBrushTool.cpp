#include "StdAfx.h"
#include "PropertyBrushTool.h"

#include "MapData.h"
#include "IsoView.h"
#include "functions.h"
#include "variables.h"

extern ACTIONDATA AD;

namespace
{
	void ApplyValue(CString& target, const PropertyBrushSettings& settings, size_t index)
	{
		if (settings.selected[index] && !settings.values[index].IsEmpty())
			target = settings.values[index];
	}

	bool IsSameCell(const MapCoords& first, const MapCoords& second)
	{
		return first.x == second.x && first.y == second.y;
	}
}

bool PropertyBrushSettings::HasSelectedFields() const
{
	for (bool isSelected : selected)
		if (isSelected)
			return true;
	return false;
}

size_t GetPropertyBrushFieldCount(PropertyBrushObjectType objectType)
{
	switch (objectType)
	{
	case PropertyBrushObjectType::Structure: return 14;
	case PropertyBrushObjectType::Infantry: return 10;
	case PropertyBrushObjectType::Unit: return 11;
	case PropertyBrushObjectType::Aircraft: return 9;
	}
	return 0;
}

PropertyBrushTool::PropertyBrushTool(CMapData& map, CIsoView& view, const PropertyBrushSettings& settings)
	: MapTool(map, view)
	, m_settings(settings)
{
}

bool PropertyBrushTool::ApplyStructure(DWORD position)
{
	const int index = getMap().GetStructureAt(position);
	if (index < 0)
		return false;

	STRUCTURE structure;
	getMap().GetStructureData(index, &structure);
	const CString id = *getMap().GetIniFile().sections["Structures"].GetValueName(index);

	ApplyValue(structure.house, m_settings, 0);
	ApplyValue(structure.strength, m_settings, 1);
	ApplyValue(structure.direction, m_settings, 2);
	ApplyValue(structure.flag1, m_settings, 3);
	ApplyValue(structure.flag2, m_settings, 4);
	ApplyValue(structure.energy, m_settings, 5);
	ApplyValue(structure.upgradecount, m_settings, 6);
	ApplyValue(structure.spotlight, m_settings, 7);
	ApplyValue(structure.upgrade1, m_settings, 8);
	ApplyValue(structure.upgrade2, m_settings, 9);
	ApplyValue(structure.upgrade3, m_settings, 10);
	ApplyValue(structure.flag3, m_settings, 11);
	ApplyValue(structure.flag4, m_settings, 12);
	ApplyValue(structure.tag, m_settings, 13);

	getMap().DeleteStructure(index);
	return getMap().AddStructure(&structure, nullptr, nullptr, 0, id) != FALSE;
}

bool PropertyBrushTool::ApplyInfantry(DWORD position)
{
	std::array<int, SUBPOS_COUNT> indices{};
	for (int slot = 0; slot < SUBPOS_COUNT; ++slot)
		indices[slot] = getMap().GetInfantryAt(position, slot);

	bool applied = false;
	for (int index : indices)
	{
		if (index < 0)
			continue;

		INFANTRY infantry;
		getMap().GetInfantryData(index, &infantry);
		ApplyValue(infantry.house, m_settings, 0);
		ApplyValue(infantry.strength, m_settings, 1);
		ApplyValue(infantry.action, m_settings, 2);
		ApplyValue(infantry.direction, m_settings, 3);
		ApplyValue(infantry.flag1, m_settings, 4);
		ApplyValue(infantry.flag2, m_settings, 5);
		ApplyValue(infantry.flag3, m_settings, 6);
		ApplyValue(infantry.flag4, m_settings, 7);
		ApplyValue(infantry.flag5, m_settings, 8);
		ApplyValue(infantry.tag, m_settings, 9);

		getMap().DeleteInfantry(index);
		applied = getMap().AddInfantry(&infantry, nullptr, nullptr, 0, index) != FALSE || applied;
	}
	return applied;
}

bool PropertyBrushTool::ApplyUnit(DWORD position)
{
	const int index = getMap().GetUnitAt(position);
	if (index < 0)
		return false;

	UNIT unit;
	getMap().GetUnitData(index, &unit);
	const CString id = *getMap().GetIniFile().sections["Units"].GetValueName(index);

	ApplyValue(unit.house, m_settings, 0);
	ApplyValue(unit.strength, m_settings, 1);
	ApplyValue(unit.action, m_settings, 2);
	ApplyValue(unit.direction, m_settings, 3);
	ApplyValue(unit.flag1, m_settings, 4);
	ApplyValue(unit.flag2, m_settings, 5);
	ApplyValue(unit.flag3, m_settings, 6);
	ApplyValue(unit.flag4, m_settings, 7);
	ApplyValue(unit.flag5, m_settings, 8);
	ApplyValue(unit.flag6, m_settings, 9);
	ApplyValue(unit.tag, m_settings, 10);

	getMap().DeleteUnit(index);
	return getMap().AddUnit(&unit, nullptr, nullptr, 0, id) != FALSE;
}

bool PropertyBrushTool::ApplyAircraft(DWORD position)
{
	const int index = getMap().GetAirAt(position);
	if (index < 0)
		return false;

	AIRCRAFT aircraft;
	getMap().GetAircraftData(index, &aircraft);
	const CString id = *getMap().GetIniFile().sections["Aircraft"].GetValueName(index);

	ApplyValue(aircraft.house, m_settings, 0);
	ApplyValue(aircraft.strength, m_settings, 1);
	ApplyValue(aircraft.direction, m_settings, 2);
	ApplyValue(aircraft.action, m_settings, 3);
	ApplyValue(aircraft.flag1, m_settings, 4);
	ApplyValue(aircraft.flag2, m_settings, 5);
	ApplyValue(aircraft.flag3, m_settings, 6);
	ApplyValue(aircraft.flag4, m_settings, 7);
	ApplyValue(aircraft.tag, m_settings, 8);

	getMap().DeleteAircraft(index);
	return getMap().AddAircraft(&aircraft, nullptr, nullptr, 0, id) != FALSE;
}

bool PropertyBrushTool::Apply(const MapCoords& cell)
{
	const DWORD position = cell.x + cell.y * getMap().GetIsoSize();
	switch (m_settings.objectType)
	{
	case PropertyBrushObjectType::Structure: return ApplyStructure(position);
	case PropertyBrushObjectType::Infantry: return ApplyInfantry(position);
	case PropertyBrushObjectType::Unit: return ApplyUnit(position);
	case PropertyBrushObjectType::Aircraft: return ApplyAircraft(position);
	}
	return false;
}

void PropertyBrushTool::onLButtonUp(const ProjectedCoords&, const MapCoords& mapCoords, MapToolMouseFlags)
{
	if (!getMap().isInside(mapCoords) || IsSameCell(mapCoords, m_lastAppliedCell))
		return;

	if (Apply(mapCoords))
	{
		m_lastAppliedCell = mapCoords;
		getView().SetError(GetLanguageStringACP("PropertyBrushApplied"));
		getView().RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
	}
	else
		getView().SetError(GetLanguageStringACP("PropertyBrushWrongType"));
}

void PropertyBrushTool::onMouseMove(const ProjectedCoords&, const MapCoords& mapCoords, MapToolMouseFlags flags)
{
	if (!getMap().isInside(mapCoords) ||
		(flags & MapToolMouseFlags::LBUTTON) != MapToolMouseFlags::LBUTTON ||
		IsSameCell(mapCoords, m_lastAppliedCell))
	{
		return;
	}

	if (Apply(mapCoords))
	{
		m_lastAppliedCell = mapCoords;
		getView().RedrawWindow(nullptr, nullptr, RDW_INVALIDATE);
	}
}

bool PropertyBrushTool::onRButtonUp(const ProjectedCoords&, const MapCoords&, MapToolMouseFlags)
{
	AD.reset();
	getView().RedrawWindow(nullptr, nullptr, RDW_INVALIDATE);
	return true;
}

bool PropertyBrushTool::onKeyDown(UINT key)
{
	if (key != VK_ESCAPE)
		return false;
	AD.reset();
	getView().RedrawWindow(nullptr, nullptr, RDW_INVALIDATE);
	return true;
}
