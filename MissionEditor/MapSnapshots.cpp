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
#include "MapSnapshots.h"

CMapSnapshots::CMapSnapshots()
{
	m_cursnapshot = -1;
}

CMapSnapshots::~CMapSnapshots()
{
	Clear();
}

void CMapSnapshots::Clear()
{
	m_snapshots.clear();
	m_cursnapshot = -1;
}

/*
Copies a part of the map into a snapshot. The snapshot is used to
reverse the changes when the user hits undo.

Be aware that this won´t make a copy of any units etc.

This is used for undo and similar things (like displaying and immediatly removing tiles when moving
the mouse on the map before placing a tile).
This method is very fast, as long as you don´t copy the whole map all the time.
*/
void CMapSnapshots::TakeSnapshot(FIELDDATA* fielddata, DWORD isoSize, BOOL bEraseFollowing, int left, int top, int right, int bottom)
{
	if (left < 0) left = 0;
	if (top < 0) top = 0;
	if (right > (int)isoSize) right = (int)isoSize;
	if (bottom > (int)isoSize) bottom = (int)isoSize;

	if (right == 0) right = (int)isoSize;
	if (bottom == 0) bottom = (int)isoSize;

	if (bEraseFollowing)
	{
		m_snapshots.erase(m_snapshots.begin() + m_cursnapshot + 1, m_snapshots.end());
	}

	if (m_snapshots.size() == MAX_SNAPSHOTS)
	{
		m_snapshots.erase(m_snapshots.begin());
	}

	m_snapshots.emplace_back();
	m_cursnapshot = static_cast<int>(m_snapshots.size()) - 1;
	SNAPSHOTDATA& ss = m_snapshots.back();
	// ss.mapfile=m_mapfile;
	int width, height;
	width = right - left;
	height = bottom - top;

	const std::size_t size = static_cast<std::size_t>(width) * height;
	ss.left = left;
	ss.top = top;
	ss.right = right;
	ss.bottom = bottom;
	ss.bHeight.resize(size);
	ss.bMapData.resize(size);
	ss.bSubTile.resize(size);
	ss.bMapData2.resize(size);
	ss.wGround.resize(size);
	ss.overlay.resize(size);
	ss.overlaydata.resize(size);
	ss.bRedrawTerrain.resize(size);
	ss.bRNDData.resize(size);
	int i;
	int e;
	for (e = 0;e < height;e++)
	{
		for (i = 0;i < width;i++)
		{
			int pos_w, pos_r;
			pos_w = i + e * width;
			pos_r = left + i + (top + e) * (int)isoSize;
			ss.bHeight[pos_w] = fielddata[pos_r].bHeight;
			ss.bMapData[pos_w] = fielddata[pos_r].bMapData;
			ss.bSubTile[pos_w] = fielddata[pos_r].bSubTile;
			ss.bMapData2[pos_w] = fielddata[pos_r].bMapData2;
			ss.wGround[pos_w] = fielddata[pos_r].wGround;
			ss.overlay[pos_w] = fielddata[pos_r].overlay;
			ss.overlaydata[pos_w] = fielddata[pos_r].overlaydata;
			ss.bRedrawTerrain[pos_w] = fielddata[pos_r].bRedrawTerrain;
			ss.bRNDData[pos_w] = fielddata[pos_r].bRNDImage;
		}
	}

}

void CMapSnapshots::RestoreSnapshot(FIELDDATA* fielddata, DWORD isoSize, const SNAPSHOTDATA& ss,
	const std::function<void(int, int)>& onBeforeRestore,
	const std::function<void(int, int)>& onAfterRestore)
{
	const int left = ss.left;
	const int top = ss.top;
	const int width = ss.right - left;
	const int height = ss.bottom - top;

	int i, e;
	for (e = 0;e < height;e++)
	{
		for (i = 0;i < width;i++)
		{
			int pos_w, pos_r;
			pos_r = i + e * width;
			pos_w = left + i + (top + e) * (int)isoSize;

			if (onBeforeRestore)
				onBeforeRestore(left + i, top + e);

			fielddata[pos_w].bHeight = ss.bHeight[pos_r];
			fielddata[pos_w].bMapData = ss.bMapData[pos_r];
			fielddata[pos_w].bSubTile = ss.bSubTile[pos_r];
			fielddata[pos_w].bMapData2 = ss.bMapData2[pos_r];
			fielddata[pos_w].wGround = ss.wGround[pos_r];
			fielddata[pos_w].overlay = ss.overlay[pos_r];
			fielddata[pos_w].overlaydata = ss.overlaydata[pos_r];
			fielddata[pos_w].bRedrawTerrain = ss.bRedrawTerrain[pos_r];
			fielddata[pos_w].bRNDImage = ss.bRNDData[pos_r];

			if (onAfterRestore)
				onAfterRestore(left + i, top + e);
		}
	}
}

/*
Just uses the last SnapShot to reverse changes on the map.
Very fast
*/
bool CMapSnapshots::Undo(FIELDDATA* fielddata, DWORD isoSize,
	const std::function<void(int, int)>& onBeforeRestore,
	const std::function<void(int, int)>& onAfterRestore)
{
	if (m_snapshots.empty()) return false;
	if (m_cursnapshot < 0) return false;

	m_cursnapshot -= 1;

	RestoreSnapshot(fielddata, isoSize, m_snapshots[m_cursnapshot + 1], onBeforeRestore, onAfterRestore);

	return true;
}

/*
Opposite of Undo(). If possible, redoes the changes.
Very fast.
*/
bool CMapSnapshots::Redo(FIELDDATA* fielddata, DWORD isoSize,
	const std::function<void(int, int)>& onBeforeRestore,
	const std::function<void(int, int)>& onAfterRestore)
{
	if (!CanRedo()) return false;

	m_cursnapshot += 1;

	if (m_cursnapshot + 1 >= static_cast<int>(m_snapshots.size())) m_cursnapshot = static_cast<int>(m_snapshots.size()) - 2;

	RestoreSnapshot(fielddata, isoSize, m_snapshots[m_cursnapshot + 1], onBeforeRestore, onAfterRestore);

	return true;
}
