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
#include "TextDrawer.h"
#include <afxwin.h>
#include <algorithm>
#include "Vec2.h"

namespace
{
    bool RequiresGdiText(const std::string& text)
    {
        return std::any_of(text.begin(), text.end(), [](unsigned char c)
        {
            return c != '\n' && (c < 32 || c > 126);
        });
    }

    unsigned int ColorRefToBgr(COLORREF col)
    {
        return (static_cast<unsigned int>(GetRValue(col)) << 16)
            | (static_cast<unsigned int>(GetGValue(col)) << 8)
            | static_cast<unsigned int>(GetBValue(col));
    }

    std::shared_ptr<RgbaBitmap> RasterizeGdiBitmap(
        int width,
        int height,
        COLORREF bkCol,
        const auto& drawCallback)
    {
        if (width <= 0 || height <= 0)
            return nullptr;

        HDC memDC = ::CreateCompatibleDC(nullptr);
        if (!memDC)
            return nullptr;

        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = width;
        bi.bmiHeader.biHeight = -height; // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* rawBits = nullptr;
        HBITMAP dib = ::CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &rawBits, nullptr, 0);
        if (!dib || !rawBits)
        {
            if (dib) ::DeleteObject(dib);
            ::DeleteDC(memDC);
            return nullptr;
        }

        HGDIOBJ oldBmp = ::SelectObject(memDC, dib);
        const unsigned int bkBgr = ColorRefToBgr(bkCol);
        const size_t pixelCount = static_cast<size_t>(width) * height;
        auto* bits = static_cast<unsigned int*>(rawBits);
        std::fill(bits, bits + pixelCount, bkBgr);

        const bool ok = drawCallback(memDC);
        ::GdiFlush();

        auto bitmap = std::make_shared<RgbaBitmap>();
        if (ok)
        {
            bitmap->id = NextRgbaBitmapId();
            bitmap->width = width;
            bitmap->height = height;
            bitmap->pixels.resize(pixelCount);
            for (size_t i = 0; i < pixelCount; ++i)
            {
                const unsigned int bgr = bits[i] & 0x00ffffffu;
                bitmap->pixels[i] = (bgr == bkBgr) ? 0u : (bgr | 0xff000000u);
            }
        }

        ::SelectObject(memDC, oldBmp);
        ::DeleteObject(dib);
        ::DeleteDC(memDC);
        return ok ? bitmap : nullptr;
    }
}

TextDrawer::TextDrawer(int fontSizeInPoints, COLORREF col, COLORREF shadowCol):
    m_fontSizeInPoints(fontSizeInPoints),
    m_col(col),
    m_shadowCol(shadowCol),
    m_bkCol(col == RGB(10, 10, 10) ? RGB(11, 11, 11) : RGB(10, 10, 10))
{
    CClientDC dc(nullptr);
    auto fontSizeInPixels = -MulDiv(fontSizeInPoints, dc.GetDeviceCaps(LOGPIXELSY), 72);
    m_fontSizeInPixels = fontSizeInPixels;

    CFont f;
    f.CreateFont(fontSizeInPixels, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, VARIABLE_PITCH, "COURIER NEW");

    std::string s;
    for (char c = 32; c <= 126; ++c)
        s.push_back(c);

    CFont* pOldFont = dc.SelectObject(&f);
    const auto extent = dc.GetTextExtent(s.c_str(), static_cast<int>(s.size()));
    dc.SelectObject(pOldFont);

    if (extent.cx <= 0 || extent.cy <= 0)
        return;

    m_charExtent.set(extent.cx / static_cast<int>(s.size()), extent.cy);

    auto atlas = RasterizeGdiBitmap(extent.cx, extent.cy * 2, m_bkCol, [&](HDC hDC) -> bool
    {
        HGDIOBJ oldFont = ::SelectObject(hDC, f);
        ::SetBkMode(hDC, TRANSPARENT);
        bool ok = true;
        if (shadowCol != CLR_INVALID)
        {
            ::SetTextColor(hDC, shadowCol);
            if (!::TextOutA(hDC, 0, extent.cy, s.c_str(), static_cast<int>(s.size())))
                ok = false;
        }
        if (ok)
        {
            ::SetTextColor(hDC, col);
            if (!::TextOutA(hDC, 0, 0, s.c_str(), static_cast<int>(s.size())))
                ok = false;
        }
        ::SelectObject(hDC, oldFont);
        return ok;
    });

    if (atlas)
        m_fontAtlas = std::move(*atlas);
}

bool TextDrawer::isValid() const
{
    return !m_fontAtlas.empty();
}

TextDrawer::CachedString& TextDrawer::GetCachedString(const std::string& text)
{
    const auto it = m_stringCache.find(text);
    if (it != m_stringCache.end())
    {
        m_lruOrder.splice(m_lruOrder.end(), m_lruOrder, it->second.lruPos);
        return it->second;
    }

    while (m_stringCache.size() >= m_maxCacheEntries)
    {
        if (m_lruOrder.empty())
            break;
        m_stringCache.erase(m_lruOrder.front());
        m_lruOrder.pop_front();
    }

    CachedString cached;
    cached.lruPos = m_lruOrder.end();

    const auto insert = [&]() -> CachedString&
    {
        const auto insIt = m_stringCache.try_emplace(text, cached).first;
        insIt->second.lruPos = m_lruOrder.insert(m_lruOrder.end(), text);
        return insIt->second;
    };

    const auto extent = GetExtent(text);
    cached.w = extent.x;
    cached.h = extent.y;
    if (cached.w <= 0 || cached.h <= 0)
        return insert();

    CFont font;
    font.CreateFont(m_fontSizeInPixels, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, VARIABLE_PITCH, "Microsoft YaHei UI");

    auto renderLayer = [&](COLORREF color) -> std::shared_ptr<RgbaBitmap>
    {
        return RasterizeGdiBitmap(cached.w, cached.h, m_bkCol, [&](HDC hDC) -> bool
        {
            HGDIOBJ oldFont = ::SelectObject(hDC, font);
            ::SetBkMode(hDC, TRANSPARENT);
            ::SetTextColor(hDC, color);
            RECT rect{ 0, 0, cached.w, cached.h };
            ::DrawTextA(hDC, text.c_str(), static_cast<int>(text.size()), &rect,
                DT_LEFT | DT_TOP | DT_NOPREFIX);
            ::SelectObject(hDC, oldFont);
            return true;
        });
    };

    cached.main = renderLayer(m_col);
    if (m_shadowCol != CLR_INVALID)
        cached.shadow = renderLayer(m_shadowCol);

    return insert();
}

void TextDrawer::RenderText(VulkanScene& scene, int x, int y, const std::string& text, bool centered)
{
    if (!isValid() || text.empty())
        return;

    const auto shadowOffset = 1 + m_fontSizeInPixels / 32;
    const int lineOffset = m_charExtent.y / 4;
    ProjectedVec cur(x, y);
    const int cw = m_charExtent.x;
    const int ch = m_charExtent.y;

    if (centered)
    {
        cur -= GetExtent(text) / 2;
    }

    if (RequiresGdiText(text))
    {
        auto& cached = GetCachedString(text);
        if (cached.shadow)
            scene.Bitmap(*cached.shadow, cur.x + shadowOffset, cur.y + shadowOffset);
        if (cached.main)
            scene.Bitmap(*cached.main, cur.x, cur.y);
        return;
    }

    // Draw ASCII directly from the single uploaded glyph atlas so dynamic
    // strings (such as stats/FPS counters) never upload per-frame GPU assets.
    const int lineStartX = cur.x;
    for (const auto c : text)
    {
        if (c == '\n')
        {
            cur.set(lineStartX, cur.y + ch + lineOffset);
        }
        else if (c == ' ')
        {
            cur.x += cw;
        }
        else if (c > 32 && c <= 126)
        {
            const int i = c - 32;
            if (m_shadowCol != CLR_INVALID)
            {
                const RECT sShadow{ i * cw, ch, i * cw + cw, ch + ch };
                scene.Bitmap(m_fontAtlas, cur.x + shadowOffset, cur.y + shadowOffset, &sShadow);
            }
            const RECT sMain{ i * cw, 0, i * cw + cw, ch };
            scene.Bitmap(m_fontAtlas, cur.x, cur.y, &sMain);
            cur.x += cw;
        }
    }
}

ProjectedVec TextDrawer::GetExtent(const std::string& text) const
{
    if (RequiresGdiText(text))
    {
        HDC hDC = ::GetDC(nullptr);
        if (hDC)
        {
            CFont font;
            font.CreateFont(m_fontSizeInPixels, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
                NONANTIALIASED_QUALITY, VARIABLE_PITCH, "Microsoft YaHei UI");
            HGDIOBJ oldFont = SelectObject(hDC, font);
            RECT rect{ 0, 0, 0, 0 };
            DrawTextA(hDC, text.c_str(), static_cast<int>(text.size()), &rect,
                DT_LEFT | DT_TOP | DT_NOPREFIX | DT_CALCRECT);
            SelectObject(hDC, oldFont);
            ::ReleaseDC(nullptr, hDC);
            return ProjectedVec(rect.right - rect.left, rect.bottom - rect.top);
        }
    }

    ProjectedVec cur(0, 0);
    const int lineOffset = m_charExtent.y / 4;
    const int cw = m_charExtent.x;
    const int ch = m_charExtent.y;
    ProjectedVec maxpos(0, 0);
    for (const auto c : text)
    {
        if (c == '\n')
        {
            cur.set(0, cur.y + ch + lineOffset);
        }
        else if (c >= 32 && c <= 126)
        {
            cur.x += cw;
            maxpos.set(max(maxpos.x, cur.x), max(maxpos.y, cur.y + ch));
        }
    }
    return maxpos;
}
