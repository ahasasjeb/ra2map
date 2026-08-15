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

// FieldData.h: the per-cell map data structures.
//
// Extracted from MapData.h so that the snapshot manager (and other
// subsystems that operate on raw field buffers) do not have to depend
// on the whole CMapData class.

#pragma once

#include "structs.h"

struct NODEDATA
{
	NODEDATA();
	int type;
	int index;
	CString house;
};

// mapfielddata is the data of every field in an extracted isomappack!
struct MAPFIELDDATA
{
	unsigned short wX;
	unsigned short wY;
	WORD wGround;
	BYTE bData[3];
	BYTE bHeight;
	BYTE bData2[1];
};
#define MAPFIELDDATA_SIZE 11

// the extracted overlay pack is stored in a fixed 512x512 grid, independent of the map size
constexpr int OVERLAY_PACK_GRID_SIZE = 512;
constexpr int OVERLAY_PACK_GRID_AREA = OVERLAY_PACK_GRID_SIZE * OVERLAY_PACK_GRID_SIZE; // 262144

/*
struct TILEDATA{};

contains the information needed for one field of the map.
*/
struct FIELDDATA
{
	FIELDDATA();
	short unit; // unit number
	short infantry[SUBPOS_COUNT]; // infantry number
	short aircraft; // aircraft number
	short structure; // structure number 
	short structuretype; // structure type id
	short terrain; // terrain number
	int terraintype; // terrain type id
#ifdef SMUDGE_SUPP
	short smudge;
	int smudgetype;
#endif
	short waypoint; // waypoint number

	NODEDATA node; // node info
	BYTE overlay; // overlay number
	BYTE overlaydata; // overlay data info
	WORD wGround; // ground type (tile)
	WORD bMapData; // add. data
	BYTE bSubTile;
	BYTE bHeight; // height of tile
	BYTE bMapData2; // add. data2
	short celltag; // celltag uses	

	//std::uint16_t wTubeId; // tube ID
	//char cTubePart; // 0 is start, 1 is exit, and 2-101 are tube parts
	unsigned bReserved : 1; // for program usage
	unsigned bHide : 1;
	unsigned bRedrawTerrain : 1; // force redraw
	unsigned bCliffHack : 1;
	unsigned bRNDImage : 4; // for using a,b,c of tmp tiles
};
