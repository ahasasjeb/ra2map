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
#include "RustCore.h"

namespace
{
	constexpr std::size_t SNAPSHOT_CELL_SIZE = 11;
}

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
	if (right <= left || bottom <= top || fielddata == nullptr)
		return;

	if (bEraseFollowing)
	{
		// Compute the offset before seeking: `begin() + m_cursnapshot + 1`
		// evaluates (begin() + m_cursnapshot) first, which seeks before begin
		// when the cursor is still -1 and newer MSVC STL debug checks reject
		// that even though the final offset would be valid. Seeking an
		// iterator of an empty vector is likewise rejected; skipping the
		// erase in both cases preserves the original no-op behavior.
		const int eraseFrom = m_cursnapshot + 1;
		if (eraseFrom >= 0 && eraseFrom < static_cast<int>(m_snapshots.size()))
			m_snapshots.erase(m_snapshots.begin() + eraseFrom, m_snapshots.end());
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
	std::vector<BYTE> fields(size * SNAPSHOT_CELL_SIZE);
	int i;
	int e;
	for (e = 0;e < height;e++)
	{
		for (i = 0;i < width;i++)
		{
			int pos_w, pos_r;
			pos_w = i + e * width;
			pos_r = left + i + (top + e) * (int)isoSize;
			const std::size_t pos = static_cast<std::size_t>(pos_w);
			fields[0 * size + pos] = fielddata[pos_r].bHeight;
			fields[1 * size + pos] = static_cast<BYTE>(fielddata[pos_r].bMapData);
			fields[2 * size + pos] = static_cast<BYTE>(fielddata[pos_r].bMapData >> 8);
			fields[3 * size + pos] = fielddata[pos_r].bSubTile;
			fields[4 * size + pos] = fielddata[pos_r].bMapData2;
			fields[5 * size + pos] = static_cast<BYTE>(fielddata[pos_r].wGround);
			fields[6 * size + pos] = static_cast<BYTE>(fielddata[pos_r].wGround >> 8);
			fields[7 * size + pos] = fielddata[pos_r].overlay;
			fields[8 * size + pos] = fielddata[pos_r].overlaydata;
			fields[9 * size + pos] = static_cast<BYTE>(fielddata[pos_r].bRedrawTerrain);
			fields[10 * size + pos] = static_cast<BYTE>(fielddata[pos_r].bRNDImage);
		}
	}
	std::size_t packedSize = 0;
	if (rs_snapshot_pack(fields.data(), fields.size(), nullptr, 0, &packedSize) != RS_ERR_SMALL_BUFFER)
	{
		m_snapshots.pop_back();
		m_cursnapshot = static_cast<int>(m_snapshots.size()) - 1;
		return;
	}
	ss.packedFields.resize(packedSize);
	if (rs_snapshot_pack(fields.data(), fields.size(), ss.packedFields.data(), ss.packedFields.size(), &packedSize) != RS_OK)
	{
		m_snapshots.pop_back();
		m_cursnapshot = static_cast<int>(m_snapshots.size()) - 1;
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
	const std::size_t cellCount = static_cast<std::size_t>(width) * height;
	std::vector<BYTE> fields(cellCount * SNAPSHOT_CELL_SIZE);
	if (rs_snapshot_unpack(ss.packedFields.data(), ss.packedFields.size(), fields.data(), fields.size()) != RS_OK)
		return;

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

			const std::size_t pos = static_cast<std::size_t>(pos_r);
			fielddata[pos_w].bHeight = fields[0 * cellCount + pos];
			fielddata[pos_w].bMapData = static_cast<WORD>(fields[1 * cellCount + pos] | (fields[2 * cellCount + pos] << 8));
			fielddata[pos_w].bSubTile = fields[3 * cellCount + pos];
			fielddata[pos_w].bMapData2 = fields[4 * cellCount + pos];
			fielddata[pos_w].wGround = static_cast<WORD>(fields[5 * cellCount + pos] | (fields[6 * cellCount + pos] << 8));
			fielddata[pos_w].overlay = fields[7 * cellCount + pos];
			fielddata[pos_w].overlaydata = fields[8 * cellCount + pos];
			fielddata[pos_w].bRedrawTerrain = fields[9 * cellCount + pos];
			fielddata[pos_w].bRNDImage = fields[10 * cellCount + pos];

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
