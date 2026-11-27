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

#include <afx.h>
#include <list>
#include <map>
#include <memory>
#include <string>
#include "Vec2.h"
#include "VulkanScene.h"

// Renders map overlay text through VulkanScene using a CPU-rasterized glyph
// atlas for ASCII strings and an LRU bitmap cache for localized (non-ASCII) strings.
class TextDrawer
{
public:
    TextDrawer(int fontSizeInPoints, COLORREF col, COLORREF shadowCol = CLR_INVALID);
    TextDrawer(const TextDrawer&) = delete;
    TextDrawer& operator=(const TextDrawer&) = delete;

    bool isValid() const;

    void RenderText(VulkanScene& scene, int x, int y, const std::string& text, bool centered = false);
    void ReleaseGpuAssets(VulkanScene& scene);

    ProjectedVec GetExtent(const std::string& text) const;
#ifdef _DEBUG
    void SetMaxCacheEntriesForTest(size_t limit) { m_maxCacheEntries = limit > 0 ? limit : 1; }
    size_t GetCachedStringCountForTest() const { return m_stringCache.size(); }
#endif

private:
    struct CachedString
    {
        std::shared_ptr<RgbaBitmap> main;
        std::shared_ptr<RgbaBitmap> shadow;
        std::list<std::string>::iterator lruPos;
        int w = 0;
        int h = 0;
    };

    CachedString& GetCachedString(VulkanScene& scene, const std::string& text);

    RgbaBitmap m_fontAtlas;
    ProjectedVec m_charExtent;
    int m_fontSizeInPoints;
    int m_fontSizeInPixels;
    COLORREF m_col;
    COLORREF m_shadowCol;
    COLORREF m_bkCol;

    std::map<std::string, CachedString> m_stringCache;
    std::list<std::string> m_lruOrder;
    size_t m_maxCacheEntries = 512;
};
