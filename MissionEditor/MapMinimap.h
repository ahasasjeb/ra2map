/*
	FinalSun/FinalAlert 2 Mission Editor

	Copyright (C) 1999-2024 Electronic Arts, Inc.
	Authored by Matthias Wagner

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

// MapMinimap.h: the minimap DIB of the current map.
//
// Extracted from CMapData (which used to own the color buffer, the
// BITMAPINFO and the pixel math inline in its header). The class only
// deals with the DIB and the iso-cell -> minimap-pixel projection; the
// per-cell coloring rules (ground tile, overlay, house, start positions)
// stay with CMapData and call WriteCell with the final colors.

#pragma once

#include <vector>
#include "structs.h"

class CMapMinimap
{
public:
	CMapMinimap() = default;

	// Recreates the DIB for a map of mapWidth x mapHeight cells on an
	// iso grid of isoSize x isoSize.
	void Init(int mapWidth, int mapHeight, DWORD isoSize);

	// Returns the DIB for display.
	void GetDib(BYTE** lpData, BITMAPINFO* lpBI, int* pitch);

	// Computes the minimap pixel position of cell (i, e).
	bool GetCellPixelPos(int i, int e, int& x, int& y) const;

	// Writes the two pixels of cell (i, e). Bounds-checked; returns
	// false when the cell does not land on the minimap.
	bool WriteCell(int i, int e, const RGBTRIPLE& left, const RGBTRIPLE& right);

	bool IsInitialized() const
	{
		return !m_colors.empty();
	}
	int GetPitch() const
	{
		return m_pitch;
	}

private:
	int m_mapWidth = 0;
	int m_mapHeight = 0;
	DWORD m_isoSize = 0;

	std::vector<BYTE> m_colors;
	BITMAPINFO m_biinfo = {};
	int m_pitch = 0;
};
