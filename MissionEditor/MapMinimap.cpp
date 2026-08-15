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

#include "stdafx.h"
#include "MapMinimap.h"

void CMapMinimap::Init(int mapWidth, int mapHeight, DWORD isoSize)
{
	m_mapWidth = mapWidth;
	m_mapHeight = mapHeight;
	m_isoSize = isoSize;

	const int pwidth = mapWidth * 2;
	const int pheight = mapHeight;

	memset(&m_biinfo, 0, sizeof(BITMAPINFO));
	m_biinfo.bmiHeader.biBitCount = 24;
	m_biinfo.bmiHeader.biWidth = pwidth;
	m_biinfo.bmiHeader.biHeight = pheight;
	m_biinfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	m_biinfo.bmiHeader.biClrUsed = 0;
	m_biinfo.bmiHeader.biPlanes = 1;
	m_biinfo.bmiHeader.biCompression = BI_RGB;
	m_biinfo.bmiHeader.biClrImportant = 0;

	int pitch = pwidth * 3;
	if (pitch == 0)
	{
		m_colors.clear();
		m_pitch = 0;
		return;
	}

	if (pitch % sizeof(DWORD))
	{
		pitch += sizeof(DWORD) - (pwidth * 3) % sizeof(DWORD);
	}

	m_pitch = pitch;

	m_colors.resize((size_t)pitch * pheight);

	memset(m_colors.data(), 255, (size_t)pitch * pheight);
}

void CMapMinimap::GetDib(BYTE** lpData, BITMAPINFO* lpBI, int* pitch)
{
	*lpData = m_colors.data();
	*pitch = m_pitch;
	memcpy(lpBI, &m_biinfo, sizeof(BITMAPINFO));
}

bool CMapMinimap::GetCellPixelPos(int i, int e, int& x, int& y) const
{
	const int pheight = m_biinfo.bmiHeader.biHeight;

	const DWORD dwIsoSize = m_isoSize;
	y = e / 2 + i / 2;
	x = dwIsoSize - i + e;

	int tx, ty;
	tx = m_mapWidth;
	ty = m_mapHeight;

	ty = ty / 2 + tx / 2;
	tx = dwIsoSize - m_mapWidth + m_mapHeight;

	x -= tx;
	y -= ty;

	x += pheight;
	y += pheight / 2;

	return true;
}

bool CMapMinimap::WriteCell(int i, int e, const RGBTRIPLE& left, const RGBTRIPLE& right)
{
	if (m_colors.empty())
		return false;

	const int pwidth = m_biinfo.bmiHeader.biWidth;
	const int pheight = m_biinfo.bmiHeader.biHeight;
	const int pitch = m_pitch;

	int x = 0;
	int y = 0;
	GetCellPixelPos(i, e, x, y);
	y = pheight - y - 1;

	const int dwDrawPos = (x * 3 + y * pitch);
	const int size = pitch * pheight;

	if (dwDrawPos >= size || x >= pwidth || y >= pheight || x < 0 || y < 0) return false;
	if (dwDrawPos + 3 >= (int)m_colors.size()) return false;

	RGBTRIPLE& col = (RGBTRIPLE&)m_colors[dwDrawPos];
	RGBTRIPLE& col_r = (RGBTRIPLE&)m_colors[(dwDrawPos + sizeof(RGBTRIPLE)) < size ? dwDrawPos + sizeof(RGBTRIPLE) : dwDrawPos];
	col = left;
	col_r = right;

	return true;
}
