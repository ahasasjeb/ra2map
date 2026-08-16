#include "StdAfx.h"
#include "MultiSelectionTool.h"

#include "MapData.h"
#include "IsoView.h"
#include "variables.h"

#include <algorithm>

extern ACTIONDATA AD;

MultiSelectionTool::MultiSelectionTool(CMapData& map, CIsoView& view)
	: MapTool(map, view)
	, m_selected(static_cast<size_t>(map.GetIsoSize()) * map.GetIsoSize(), 0)
{
}

bool MultiSelectionTool::IsSelected(const MapCoords& cell) const
{
	const int size = static_cast<int>(getMap().GetIsoSize());
	return cell.x >= 0 && cell.y >= 0 && cell.x < size && cell.y < size &&
		m_selected[static_cast<size_t>(cell.x) + static_cast<size_t>(cell.y) * size] != 0;
}

void MultiSelectionTool::SetSelected(const MapCoords& cell, bool selected)
{
	const int size = static_cast<int>(getMap().GetIsoSize());
	if (cell.x < 0 || cell.y < 0 || cell.x >= size || cell.y >= size)
		return;
	m_selected[static_cast<size_t>(cell.x) + static_cast<size_t>(cell.y) * size] = selected ? 1 : 0;
}

std::vector<MapCoords> MultiSelectionTool::SelectedCells() const
{
	std::vector<MapCoords> cells;
	const int size = static_cast<int>(getMap().GetIsoSize());
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x)
			if (m_selected[static_cast<size_t>(x) + static_cast<size_t>(y) * size])
				cells.emplace_back(x, y);
	return cells;
}

void MultiSelectionTool::onLButtonUp(const ProjectedCoords&, const MapCoords& mapCoords, MapToolMouseFlags flags)
{
	if (!getMap().isInside(mapCoords))
		return;

	if ((flags & MapToolMouseFlags::SHIFT) == MapToolMouseFlags::SHIFT && m_anchor.x >= 0)
	{
		const int left = std::min(m_anchor.x, mapCoords.x);
		const int right = std::max(m_anchor.x, mapCoords.x);
		const int top = std::min(m_anchor.y, mapCoords.y);
		const int bottom = std::max(m_anchor.y, mapCoords.y);
		const bool selected = (flags & MapToolMouseFlags::CTRL) != MapToolMouseFlags::CTRL;
		for (int y = top; y <= bottom; ++y)
			for (int x = left; x <= right; ++x)
				SetSelected(MapCoords(x, y), selected);
	}
	else
	{
		SetSelected(mapCoords, !IsSelected(mapCoords));
		m_anchor = mapCoords;
	}
	getView().RedrawWindow(nullptr, nullptr, RDW_INVALIDATE);
}

bool MultiSelectionTool::onRButtonUp(const ProjectedCoords&, const MapCoords&, MapToolMouseFlags)
{
	std::fill(m_selected.begin(), m_selected.end(), 0);
	m_anchor = MapCoords(-1, -1);
	getView().RedrawWindow(nullptr, nullptr, RDW_INVALIDATE);
	return true;
}

void MultiSelectionTool::render()
{
	for (const auto& cell : SelectedCells())
	{
		const auto draw = getView().GetRenderTargetCoordinates(cell);
		getView().DrawCell(draw.x, draw.y, 1, 1, RGB(255, 64, 192), TRUE);
	}
}

void MultiSelectionTool::ChangeHeight(int delta)
{
	const auto cells = SelectedCells();
	if (cells.empty())
		return;
	getMap().TakeSnapshot();
	for (const auto& cell : cells)
	{
		const DWORD position = cell.x + cell.y * getMap().GetIsoSize();
		const int height = static_cast<int>(getMap().GetHeightAt(position)) + delta;
		getMap().SetHeightAt(position, static_cast<BYTE>(std::clamp(height, 0, MAXHEIGHT)));
	}
	getMap().TakeSnapshot();
	getMap().Undo();
	getView().RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
}

bool MultiSelectionTool::onKeyDown(UINT key)
{
	if (key == VK_ESCAPE)
	{
		AD.reset();
		getView().RedrawWindow(nullptr, nullptr, RDW_INVALIDATE);
		return true;
	}
	if (key == VK_UP)
	{
		ChangeHeight(1);
		return true;
	}
	if (key == VK_DOWN)
	{
		ChangeHeight(-1);
		return true;
	}
	if (key == 'C' && (GetKeyState(VK_CONTROL) & 0x8000))
	{
		getMap().CopySelection(SelectedCells());
		return true;
	}
	if (key == 'D' && (GetKeyState(VK_CONTROL) & 0x8000))
	{
		std::fill(m_selected.begin(), m_selected.end(), 0);
		getView().RedrawWindow(nullptr, nullptr, RDW_INVALIDATE);
		return true;
	}
	return false;
}
