#include "StdAfx.h"
#include "PropertyBrushTool.h"

#include "MapData.h"
#include "IsoView.h"
#include "FinalSunDlg.h"
#include "functions.h"
#include "variables.h"

extern ACTIONDATA AD;

PropertyBrushTool::PropertyBrushTool(CMapData& map, CIsoView& view)
	: MapTool(map, view)
{
}

bool PropertyBrushTool::Sample(const MapCoords& cell)
{
	const DWORD position = cell.x + cell.y * getMap().GetIsoSize();
	int index = getMap().GetStructureAt(position);
	if (index >= 0)
	{
		STRUCTURE value;
		getMap().GetStructureData(index, &value);
		m_properties = value;
		return true;
	}
	index = getMap().GetAirAt(position);
	if (index >= 0)
	{
		AIRCRAFT value;
		getMap().GetAircraftData(index, &value);
		m_properties = value;
		return true;
	}
	index = getMap().GetUnitAt(position);
	if (index >= 0)
	{
		UNIT value;
		getMap().GetUnitData(index, &value);
		m_properties = value;
		return true;
	}
	index = getMap().GetInfantryAt(position);
	if (index >= 0)
	{
		INFANTRY value;
		getMap().GetInfantryData(index, &value);
		m_properties = value;
		return true;
	}
	return false;
}

bool PropertyBrushTool::Apply(const MapCoords& cell)
{
	const DWORD position = cell.x + cell.y * getMap().GetIsoSize();
	CIniFile& ini = getMap().GetIniFile();

	if (const auto* source = std::get_if<STRUCTURE>(&m_properties))
	{
		const int index = getMap().GetStructureAt(position);
		if (index < 0) return false;
		STRUCTURE target;
		getMap().GetStructureData(index, &target);
		const CString id = *ini.sections["Structures"].GetValueName(index);
		STRUCTURE result = *source;
		result.type = target.type;
		result.x = target.x;
		result.y = target.y;
		getMap().DeleteStructure(index);
		return getMap().AddStructure(&result, nullptr, nullptr, 0, id) != FALSE;
	}
	if (const auto* source = std::get_if<AIRCRAFT>(&m_properties))
	{
		const int index = getMap().GetAirAt(position);
		if (index < 0) return false;
		AIRCRAFT target;
		getMap().GetAircraftData(index, &target);
		const CString id = *ini.sections["Aircraft"].GetValueName(index);
		AIRCRAFT result = *source;
		result.type = target.type;
		result.x = target.x;
		result.y = target.y;
		getMap().DeleteAircraft(index);
		return getMap().AddAircraft(&result, nullptr, nullptr, 0, id) != FALSE;
	}
	if (const auto* source = std::get_if<UNIT>(&m_properties))
	{
		const int index = getMap().GetUnitAt(position);
		if (index < 0) return false;
		UNIT target;
		getMap().GetUnitData(index, &target);
		const CString id = *ini.sections["Units"].GetValueName(index);
		UNIT result = *source;
		result.type = target.type;
		result.x = target.x;
		result.y = target.y;
		getMap().DeleteUnit(index);
		return getMap().AddUnit(&result, nullptr, nullptr, 0, id) != FALSE;
	}
	if (const auto* source = std::get_if<INFANTRY>(&m_properties))
	{
		const int index = getMap().GetInfantryAt(position);
		if (index < 0) return false;
		INFANTRY target;
		getMap().GetInfantryData(index, &target);
		const int id = atoi(*ini.sections["Infantry"].GetValueName(index));
		INFANTRY result = *source;
		result.type = target.type;
		result.x = target.x;
		result.y = target.y;
		result.pos = target.pos;
		getMap().DeleteInfantry(index);
		return getMap().AddInfantry(&result, nullptr, nullptr, 0, id) != FALSE;
	}
	return false;
}

void PropertyBrushTool::onLButtonUp(const ProjectedCoords&, const MapCoords& mapCoords, MapToolMouseFlags flags)
{
	if (!getMap().isInside(mapCoords))
		return;
	const bool forceSample = (flags & MapToolMouseFlags::CTRL) == MapToolMouseFlags::CTRL;
	if (forceSample || std::holds_alternative<std::monostate>(m_properties))
	{
		if (Sample(mapCoords))
			getView().SetError(GetLanguageStringACP("PropertyBrushSampled"));
		else
			getView().SetError(GetLanguageStringACP("PropertyBrushNoObject"));
		return;
	}
	if (Apply(mapCoords))
	{
		getView().SetError(GetLanguageStringACP("PropertyBrushApplied"));
		getView().RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
	}
	else
		getView().SetError(GetLanguageStringACP("PropertyBrushWrongType"));
}

bool PropertyBrushTool::onRButtonUp(const ProjectedCoords&, const MapCoords&, MapToolMouseFlags)
{
	m_properties = std::monostate{};
	getView().SetError(GetLanguageStringACP("PropertyBrushHelp"));
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
