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

#pragma once

enum class LineStyle: int
{
	Standard,
	Dotted_2,
	Dotted_3,
	Dotted_4,
};

class VulkanScene;

class LineDrawer
{
public:
	explicit LineDrawer(VulkanScene& scene);

	void MoveTo(int to_x, int to_y);
	void LineTo(int to_x, int to_y, int color, LineStyle style = LineStyle::Standard, bool invert = false);
	void DrawLine(int from_x, int from_y, int to_x, int to_y, int color, LineStyle style = LineStyle::Standard, bool invert = false);
	void Rectangle(int from_x, int from_y, int to_x, int to_y, int color, LineStyle style = LineStyle::Standard);

private:
	VulkanScene& m_scene;
	int m_last_x;
	int m_last_y;
};
