#pragma once

#include "MapTool.h"

class MultiSelectionTool final : public MapTool
{
public:
	MultiSelectionTool(CMapData& map, CIsoView& view);

	bool onRButtonUp(const ProjectedCoords&, const MapCoords&, MapToolMouseFlags) override;
	void onLButtonUp(const ProjectedCoords&, const MapCoords& mapCoords, MapToolMouseFlags flags) override;
	void render() override;
	bool onKeyDown(UINT key) override;

private:
	bool IsSelected(const MapCoords& cell) const;
	void SetSelected(const MapCoords& cell, bool selected);
	std::vector<MapCoords> SelectedCells() const;
	void ChangeHeight(int delta);

	std::vector<BYTE> m_selected;
	MapCoords m_anchor{ -1, -1 };
};
