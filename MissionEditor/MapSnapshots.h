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

// MapSnapshots.h: undo/redo history of map field changes.
//
// Extracted from CMapData (which used to own the snapshot buffers, the
// history ring and the restore loops itself). The manager only knows
// about raw field buffers; overlays money bookkeeping and minimap
// invalidation stay with CMapData through the per-cell callbacks.

#pragma once

#include <functional>
#include "FieldData.h"

struct SNAPSHOTDATA
{
	SNAPSHOTDATA();
	// frees all allocated field buffers and resets the pointers, safe to call multiple times
	void Free();
	int left;
	int top;
	int bottom;
	int right;

	// FIELDDATA::bRedrawTerrain is a one-bit flag.  Keeping it as a byte in a
	// snapshot preserves its 0/1 value while avoiding the four-byte Win32 BOOL
	// representation for every captured cell.
	BYTE* bRedrawTerrain;
	BYTE* overlay;
	BYTE* overlaydata;
	WORD* wGround;
	WORD* bMapData;
	BYTE* bSubTile;
	BYTE* bHeight;
	BYTE* bMapData2;
	BYTE* bRNDData;
	//CIniFile mapfile;
};

class CMapSnapshots
{
public:
	CMapSnapshots();
	~CMapSnapshots();
	CMapSnapshots(const CMapSnapshots&) = delete;
	CMapSnapshots& operator=(const CMapSnapshots&) = delete;

	// Frees everything and resets the cursor (used when the map is
	// destroyed or recreated).
	void Clear();
	// Alias for Clear(): resets history to empty.
	void Reset()
	{
		Clear();
	}

	// Captures the given rectangle of `fielddata` (isoSize x isoSize
	// fields). When bEraseFollowing is set, snapshots after the current
	// cursor (a redone branch) are discarded first.
	void TakeSnapshot(FIELDDATA* fielddata, DWORD isoSize, BOOL bEraseFollowing, int left, int top, int right, int bottom);

	bool CanUndo() const
	{
		return dwSnapShotCount > 0 && m_cursnapshot >= 0;
	}
	bool CanRedo() const
	{
		return dwSnapShotCount > m_cursnapshot + 1 && dwSnapShotCount > 0;
	}

	// `onBeforeRestore(x, y)` runs while the cell still holds its current
	// values; `onAfterRestore(x, y)` runs after the snapshot values have
	// been written back. Both return whether the operation did anything.
	bool Undo(FIELDDATA* fielddata, DWORD isoSize,
		const std::function<void(int, int)>& onBeforeRestore,
		const std::function<void(int, int)>& onAfterRestore);
	bool Redo(FIELDDATA* fielddata, DWORD isoSize,
		const std::function<void(int, int)>& onBeforeRestore,
		const std::function<void(int, int)>& onAfterRestore);

	static const int MAX_SNAPSHOTS = 64;

private:
	void RestoreSnapshot(FIELDDATA* fielddata, DWORD isoSize, const SNAPSHOTDATA& ss,
		const std::function<void(int, int)>& onBeforeRestore,
		const std::function<void(int, int)>& onAfterRestore);

	SNAPSHOTDATA* m_snapshots;
	DWORD dwSnapShotCount;
	int m_cursnapshot;
};
