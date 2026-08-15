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

SNAPSHOTDATA::SNAPSHOTDATA()
{
	memset(this, 0, sizeof(SNAPSHOTDATA));
}

void SNAPSHOTDATA::Free()
{
	if (bRedrawTerrain) delete[] bRedrawTerrain;
	if (overlay) delete[] overlay;
	if (overlaydata) delete[] overlaydata;
	if (wGround) delete[] wGround;
	if (bMapData) delete[] bMapData;
	if (bSubTile) delete[] bSubTile;
	if (bHeight) delete[] bHeight;
	if (bMapData2) delete[] bMapData2;
	if (bRNDData) delete[] bRNDData;

	bRedrawTerrain = NULL;
	overlay = NULL;
	overlaydata = NULL;
	wGround = NULL;
	bMapData = NULL;
	bSubTile = NULL;
	bHeight = NULL;
	bMapData2 = NULL;
	bRNDData = NULL;
}

CMapSnapshots::CMapSnapshots()
{
	m_snapshots = NULL;
	dwSnapShotCount = 0;
	m_cursnapshot = -1;
}

CMapSnapshots::~CMapSnapshots()
{
	Clear();
}

void CMapSnapshots::Clear()
{
	for (DWORD i = 0; i < dwSnapShotCount; i++)
	{
		m_snapshots[i].Free();
	}

	if (m_snapshots) delete[] m_snapshots;
	m_snapshots = NULL;
	dwSnapShotCount = 0;
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
	DWORD dwOldSnapShotCount = dwSnapShotCount;

	if (left < 0) left = 0;
	if (top < 0) top = 0;
	if (right > (int)isoSize) right = (int)isoSize;
	if (bottom > (int)isoSize) bottom = (int)isoSize;

	if (right == 0) right = (int)isoSize;
	if (bottom == 0) bottom = (int)isoSize;

	int e;
	if (bEraseFollowing)
	{
		for (e = dwSnapShotCount - 1;e > m_cursnapshot;e--)
		{
			m_snapshots[e].Free();
		}
		dwSnapShotCount = m_cursnapshot + 1;
	}


	dwSnapShotCount += 1;
	m_cursnapshot++;

	if (dwSnapShotCount > MAX_SNAPSHOTS)
	{
		dwSnapShotCount = MAX_SNAPSHOTS;
		m_cursnapshot = MAX_SNAPSHOTS - 1;
		int i;
		m_snapshots[0].Free();
		for (i = 1;i < (int)dwSnapShotCount;i++)
		{
			m_snapshots[i - 1] = m_snapshots[i];
		}

	}
	else
	{
		SNAPSHOTDATA* b = new(SNAPSHOTDATA[dwSnapShotCount]);

		if (m_snapshots)
		{
			memcpy(b, m_snapshots, sizeof(SNAPSHOTDATA) * (dwSnapShotCount - 1));
			delete[] m_snapshots;
		}

		m_snapshots = b;
	}


	m_cursnapshot = dwSnapShotCount - 1;


	SNAPSHOTDATA ss = m_snapshots[dwSnapShotCount - 1];
	// ss.mapfile=m_mapfile;
	int width, height;
	width = right - left;
	height = bottom - top;

	int size = width * height;
	ss.left = left;
	ss.top = top;
	ss.right = right;
	ss.bottom = bottom;
	ss.bHeight = new(BYTE[size]);
	ss.bMapData = new(WORD[size]);
	ss.bSubTile = new(BYTE[size]);
	ss.bMapData2 = new(BYTE[size]);
	ss.wGround = new(WORD[size]);
	ss.overlay = new(BYTE[size]);
	ss.overlaydata = new(BYTE[size]);
	ss.bRedrawTerrain = new(BOOL[size]);
	ss.bRNDData = new(BYTE[size]);
	int i;
	for (i = 0;i < width;i++)
	{
		for (e = 0;e < height;e++)
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

	m_snapshots[dwSnapShotCount - 1] = ss;

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
	for (i = 0;i < width;i++)
	{
		for (e = 0;e < height;e++)
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
	if (dwSnapShotCount == 0) return false;
	if (m_cursnapshot < 0) return false;

	//dwSnapShotCount--;
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
	if (dwSnapShotCount <= (DWORD)(m_cursnapshot + 1) || !dwSnapShotCount) return false;

	m_cursnapshot += 1; // dwSnapShotCount-1;

	if (m_cursnapshot + 1 >= (int)dwSnapShotCount) m_cursnapshot = dwSnapShotCount - 2;

	RestoreSnapshot(fielddata, isoSize, m_snapshots[m_cursnapshot + 1], onBeforeRestore, onAfterRestore);

	return true;
}
