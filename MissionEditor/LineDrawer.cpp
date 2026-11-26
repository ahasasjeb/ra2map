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

#include "StdAfx.h"
#include "LineDrawer.h"
#include "VulkanScene.h"

namespace
{
	constexpr int ModLookup[] = { 1, 2, 3, 4 };
}

LineDrawer::LineDrawer(VulkanScene& scene)
	: m_scene(scene)
	, m_last_x(0)
	, m_last_y(0)
{
}

void LineDrawer::MoveTo(int to_x, int to_y)
{
	m_last_x = to_x;
	m_last_y = to_y;
}

void LineDrawer::LineTo(int to_x, int to_y, int color, LineStyle style, bool invert)
{
	DrawLine(m_last_x, m_last_y, to_x, to_y, color, style, invert);
}

void LineDrawer::DrawLine(int from_x, int from_y, int to_x, int to_y, int color, LineStyle style, bool invert)
{
	m_last_x = to_x;
	m_last_y = to_y;
	m_scene.Line(from_x, from_y, to_x, to_y, static_cast<unsigned int>(color), ModLookup[static_cast<int>(style)], invert);
}

void LineDrawer::Rectangle(int from_x, int from_y, int to_x, int to_y, int color, LineStyle style)
{
	MoveTo(from_x, from_y);
	LineTo(to_x, from_y, color, style);
	LineTo(to_x, to_y, color, style);
	LineTo(from_x, to_y, color, style);
	LineTo(from_x, from_y, color, style);
}
