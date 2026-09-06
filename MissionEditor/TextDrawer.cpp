#include "TextDrawer.h"
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
#include "MissionEditorPackLib.h"
#include "VulkanScene.h"

namespace
{
    bool RequiresGdiText(const std::string& text)
    {
        return std::any_of(text.begin(), text.end(), [](unsigned char c)
        {
            return c != '\n' && (c < 32 || c > 126);
        });
    }

    bool FillSurface(IDirectDrawSurface4* surface, COLORREF color)
    {
        if (!surface)
            return false;

        DDPIXELFORMAT pixelFormat = {};
        pixelFormat.dwSize = sizeof(pixelFormat);
        if (surface->GetPixelFormat(&pixelFormat) != DD_OK)
            return false;

        FSunPackLib::ColorConverter converter(pixelFormat);
        DDBLTFX fx = {};
        fx.dwSize = sizeof(fx);
        fx.dwFillColor = converter.GetColor(color);
        return surface->Blt(nullptr, nullptr, nullptr, DDBLT_COLORFILL | DDBLT_WAIT, &fx) == DD_OK;
    }
}

TextDrawer::TextDrawer(IDirectDraw4* pDirectDraw, int fontSizeInPoints, COLORREF col, COLORREF shadowCol):
    m_fontSizeInPoints(fontSizeInPoints),
    m_col(col),
    m_shadowCol(shadowCol),
    m_bkCol(col == RGB(10, 10, 10) ? RGB(11, 11, 11) : RGB(10, 10, 10))
{
    // CClientDC(nullptr) wraps a DC for the desktop window and releases it in
    // its destructor, matching the GetDC(NULL)/ReleaseDC contract that the
    // previous code failed to honour.
    CClientDC dc(nullptr);
    auto fontSizeInPixels = -MulDiv(fontSizeInPoints, dc.GetDeviceCaps(LOGPIXELSY), 72);
    m_fontSizeInPixels = fontSizeInPixels;

    CFont f;
    f.CreateFont(fontSizeInPixels, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, VARIABLE_PITCH, "COURIER NEW");

    // Build a string that contains all required characters in order
    std::string s;
    for (char c = 32; c <= 126; ++c)
        s.push_back(c);

    // get the extent in pixels of all characters. Save and restore the
    // previously selected font: GDI refuses to delete a font that is still
    // selected into a DC, which would leak the HFONT owned by `f`.
    CFont* pOldFont = dc.SelectObject(&f);
    const auto extent = dc.GetTextExtent(s.c_str(), s.size());
    dc.SelectObject(pOldFont);

    // Now create the DirectDraw surface
    DDSURFACEDESC2 desc = { 0 };
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    desc.dwWidth = extent.cx;
    desc.dwHeight = extent.cy * 2;

    m_charExtent.set(extent.cx / s.size(), extent.cy);

    auto pSurface = CComPtr<IDirectDrawSurface4>();
    if (pDirectDraw->CreateSurface(&desc, &pSurface, nullptr) != DD_OK)
        return;

    desc.dwFlags |= DDSD_PIXELFORMAT;
    pSurface->GetSurfaceDesc(&desc);
    if (pSurface->Lock(NULL, &desc, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT | DDLOCK_NOSYSLOCK, NULL) == DD_OK)
    {
        FSunPackLib::ColorConverter c(desc.ddpfPixelFormat);
        std::int32_t backcolor = c.GetColor(m_bkCol);
        auto bytes_per_pixel = (desc.ddpfPixelFormat.dwRGBBitCount + 7) / 8;
        BYTE* const pImage = static_cast<BYTE*>(desc.lpSurface);
        for (int i=0; i < desc.dwWidth; ++i)
        {
            for (int e = 0; e < desc.dwHeight; ++e)
            {
                memcpy(&pImage[e * desc.lPitch + i * bytes_per_pixel], &backcolor, bytes_per_pixel);
            }
        }
        pSurface->Unlock(NULL);
    }


    
  
    HDC hDC;
    if (pSurface->GetDC(&hDC) != DD_OK)
        return;

    // Draw the string with all characters onto the surface. The surface DC
    // and the previously selected font must be restored on every exit path,
    // otherwise we leak the DC and the HFONT owned by `f`. The guard releases
    // the DC and restores the font when its scope ends (including on return).
    {
        HGDIOBJ hOldFont = SelectObject(hDC, f);
        SetBkMode(hDC, TRANSPARENT);

        struct SurfaceDcGuard
        {
            IDirectDrawSurface4* surface;
            HDC dc;
            HGDIOBJ oldFont;
            ~SurfaceDcGuard()
            {
                SelectObject(dc, oldFont);
                surface->ReleaseDC(dc);
            }
        } guard{ pSurface, hDC, hOldFont };

        if (shadowCol != CLR_INVALID)
        {
            SetTextColor(hDC, shadowCol);
            if (!TextOutA(hDC, 0, extent.cy, s.c_str(), s.size()))
                return;
        }
        SetTextColor(hDC, col);
        if (!TextOutA(hDC, 0, 0, s.c_str(), s.size()))
            return;
    }

    // set transparency key to top left
    FSunPackLib::SetColorKey(pSurface, CLR_INVALID);

    // Everything fine, pass ownership of surface to m_fontSurface
    m_fontSurface.Attach(pSurface.Detach());
}

bool TextDrawer::isValid() const
{
    return m_fontSurface != nullptr;
}

TextDrawer::CachedString& TextDrawer::GetCachedString(const std::string& text)
{
    const auto it = m_stringCache.find(text);
    if (it != m_stringCache.end())
    {
        // mark as most recently used; splice keeps all iterators valid
        m_lruOrder.splice(m_lruOrder.end(), m_lruOrder, it->second.lruPos);
        return it->second;
    }

    // evict least recently used entries one by one instead of dropping the
    // whole cache (a full clear forces every hot label to be re-rendered on
    // the next frame, which shows up as a periodic hitch)
    while (m_stringCache.size() >= m_maxCacheEntries)
    {
        if (m_lruOrder.empty())
            break;
        m_stringCache.erase(m_lruOrder.front());
        m_lruOrder.pop_front();
    }

    CachedString cached;
    // a valid, copyable default: overwritten with the real node on insertion
    cached.lruPos = m_lruOrder.end();

    // inserts the (possibly incomplete) entry into the cache and registers it
    // as most recently used; failed entries are cached as negative results so
    // they are not retried every frame and age out through normal eviction
    const auto insert = [&]() -> CachedString&
    {
        const auto insIt = m_stringCache.try_emplace(text, cached).first;
        insIt->second.lruPos = m_lruOrder.insert(m_lruOrder.end(), text);
        return insIt->second;
    };

    const auto extent = GetExtent(text);
    cached.w = extent.x;
    cached.h = extent.y;
    if (cached.w <= 0 || cached.h <= 0 || !m_fontSurface)
        return insert();

    CComPtr<IDirectDraw4> pDD;
    if (m_fontSurface->GetDDInterface(reinterpret_cast<void**>(&pDD)) != DD_OK)
        return insert();

    DDSURFACEDESC2 desc = { 0 };
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    desc.dwWidth = cached.w;
    desc.dwHeight = cached.h;

    if (pDD->CreateSurface(&desc, &cached.main, nullptr) != DD_OK)
        return insert();

    if (m_shadowCol != CLR_INVALID)
    {
        if (pDD->CreateSurface(&desc, &cached.shadow, nullptr) != DD_OK)
            return insert();
    }

    // DirectDraw does not guarantee the contents of a newly created surface.
    // Clear every cached string to one known color before drawing glyphs, then
    // use that same color as the source key. Otherwise the uninitialized area
    // appears as an opaque black rectangle around longer labels.
    if (!FillSurface(cached.main, m_bkCol) || (cached.shadow && !FillSurface(cached.shadow, m_bkCol)))
        return insert();

    if (RequiresGdiText(text))
    {
        auto drawText = [&](IDirectDrawSurface4* surface, COLORREF color)
        {
            if (!surface)
                return;

            HDC hDC = nullptr;
            if (surface->GetDC(&hDC) != DD_OK)
                return;

            CFont font;
            font.CreateFont(m_fontSizeInPixels, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
                NONANTIALIASED_QUALITY, VARIABLE_PITCH, "Microsoft YaHei UI");
            HGDIOBJ oldFont = SelectObject(hDC, font);
            SetBkMode(hDC, TRANSPARENT);
            SetTextColor(hDC, color);
            RECT rect{ 0, 0, cached.w, cached.h };
            DrawTextA(hDC, text.c_str(), static_cast<int>(text.size()), &rect,
                DT_LEFT | DT_TOP | DT_NOPREFIX);
            SelectObject(hDC, oldFont);
            surface->ReleaseDC(hDC);
        };

        drawText(cached.main, m_col);
        drawText(cached.shadow, m_shadowCol);
    }
    else
    {

        const int cw = m_charExtent.x;
        const int ch = m_charExtent.y;
        const int lineOffset = ch / 4;
        int curx = 0;
        int cury = 0;
        for (const auto c : text)
        {
            if (c == '\n')
            {
                curx = 0;
                cury += ch + lineOffset;
            }
            else if (c >= 32 && c <= 126)
            {
                const auto i = c - 32;
                RECT sMain{ i * cw, 0, i * cw + cw, ch };
                RECT sShadow{ i * cw, ch, i * cw + cw, ch + ch };
                cached.main->BltFast(curx, cury, m_fontSurface, &sMain, DDBLTFAST_SRCCOLORKEY | DDBLTFAST_WAIT);
                if (cached.shadow)
                    cached.shadow->BltFast(curx, cury, m_fontSurface, &sShadow, DDBLTFAST_SRCCOLORKEY | DDBLTFAST_WAIT);
                curx += cw;
            }
        }
    }

    FSunPackLib::SetColorKey(cached.main, m_bkCol);
    if (cached.shadow)
        FSunPackLib::SetColorKey(cached.shadow, m_bkCol);

    return insert();
}

void TextDrawer::RenderText(IDirectDrawSurface4* target, int x, int y, const std::string& text, bool centered)
{
    if (!isValid())
        return;

    auto shadowOffset = 1 + m_fontSizeInPixels / 32;

    const int lineOffset = m_charExtent.y / 4;
    ProjectedVec cur(x, y);
    const int cw = m_charExtent.x;
    const int ch = m_charExtent.y;

    if (centered)
    {
        cur -= GetExtent(text) / 2;
    }

    if (auto scene = VulkanScene::Current(); scene && scene->IsTarget(target))
    {
        auto& cached = GetCachedString(text);
        if (cached.shadow) scene->Bitmap(cached.shadow, cur.x + shadowOffset, cur.y + shadowOffset);
        if (cached.main) scene->Bitmap(cached.main, cur.x, cur.y);
        return;
    }

    // Some DirectDraw drivers ignore the source color key when a cached GDI
    // string is copied with BltFast, leaving an opaque rectangle behind. Draw
    // localized strings directly onto the destination surface instead. These
    // strings are rare (currently the fixed map-credit label), so this avoids
    // the driver bug without slowing down the many ASCII object labels.
    if (RequiresGdiText(text))
    {
        HDC hDC = nullptr;
        if (target->GetDC(&hDC) == DD_OK)
        {
            CFont font;
            font.CreateFont(m_fontSizeInPixels, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
                NONANTIALIASED_QUALITY, VARIABLE_PITCH, "Microsoft YaHei UI");
            HGDIOBJ oldFont = SelectObject(hDC, font);
            SetBkMode(hDC, TRANSPARENT);

            auto drawAt = [&](int drawX, int drawY, COLORREF color)
            {
                RECT rect{ drawX, drawY, drawX, drawY };
                SetTextColor(hDC, color);
                DrawTextA(hDC, text.c_str(), static_cast<int>(text.size()), &rect,
                    DT_LEFT | DT_TOP | DT_NOPREFIX | DT_NOCLIP);
            };

            if (m_shadowCol != CLR_INVALID)
            {
                drawAt(cur.x - shadowOffset, cur.y - shadowOffset, m_shadowCol);
                drawAt(cur.x, cur.y - shadowOffset, m_shadowCol);
                drawAt(cur.x + shadowOffset, cur.y - shadowOffset, m_shadowCol);
                drawAt(cur.x - shadowOffset, cur.y, m_shadowCol);
                drawAt(cur.x + shadowOffset, cur.y, m_shadowCol);
                drawAt(cur.x - shadowOffset, cur.y + shadowOffset, m_shadowCol);
                drawAt(cur.x, cur.y + shadowOffset, m_shadowCol);
                drawAt(cur.x + shadowOffset, cur.y + shadowOffset, m_shadowCol);
            }
            drawAt(cur.x, cur.y, m_col);

            SelectObject(hDC, oldFont);
            target->ReleaseDC(hDC);
            return;
        }
    }

    if (m_stringCache.count(text) != 0 || text.size() >= 4)
    {
        // for longer strings it pays off to render them once and blit them as a whole
        CachedString& cached = GetCachedString(text);
        if (cached.main)
        {
            if (cached.shadow && m_shadowCol != CLR_INVALID)
                target->BltFast(cur.x + shadowOffset, cur.y + shadowOffset, cached.shadow, nullptr, DDBLTFAST_SRCCOLORKEY | DDBLTFAST_WAIT);
            target->BltFast(cur.x, cur.y, cached.main, nullptr, DDBLTFAST_SRCCOLORKEY | DDBLTFAST_WAIT);
            return;
        }
    }

    for (const auto c : text)
    {
        if (c == '\n')
        {
            cur.set(x, cur.y + ch + lineOffset);
        }
        else if (c >= 32 && c <= 126)
        {
            auto i = c - 32;

            
            if (m_shadowCol != CLR_INVALID)
            {
                RECT s_shadow{ i * cw, ch, i * cw + cw, ch + ch };
                
                target->BltFast(cur.x + 0 * shadowOffset, cur.y + 1 * shadowOffset, m_fontSurface, &s_shadow, DDBLTFAST_SRCCOLORKEY | DDBLTFAST_WAIT);
                target->BltFast(cur.x + 0 * shadowOffset, cur.y - 1 * shadowOffset, m_fontSurface, &s_shadow, DDBLTFAST_SRCCOLORKEY | DDBLTFAST_WAIT);
                target->BltFast(cur.x + 1 * shadowOffset, cur.y + 0 * shadowOffset, m_fontSurface, &s_shadow, DDBLTFAST_SRCCOLORKEY | DDBLTFAST_WAIT);
                target->BltFast(cur.x - 1 * shadowOffset, cur.y + 0 * shadowOffset, m_fontSurface, &s_shadow, DDBLTFAST_SRCCOLORKEY | DDBLTFAST_WAIT);

                target->BltFast(cur.x + 1 * shadowOffset, cur.y + 1 * shadowOffset, m_fontSurface, &s_shadow, DDBLTFAST_SRCCOLORKEY | DDBLTFAST_WAIT);
                target->BltFast(cur.x - 1 * shadowOffset, cur.y + 1 * shadowOffset, m_fontSurface, &s_shadow, DDBLTFAST_SRCCOLORKEY | DDBLTFAST_WAIT);
                target->BltFast(cur.x + 1 * shadowOffset, cur.y - 1 * shadowOffset, m_fontSurface, &s_shadow, DDBLTFAST_SRCCOLORKEY | DDBLTFAST_WAIT);
                target->BltFast(cur.x - 1 * shadowOffset, cur.y - 1 * shadowOffset, m_fontSurface, &s_shadow, DDBLTFAST_SRCCOLORKEY | DDBLTFAST_WAIT);
            }

            RECT s{ i * cw, 0, i * cw + cw, ch };
            target->BltFast(cur.x, cur.y, m_fontSurface, &s, DDBLTFAST_SRCCOLORKEY | DDBLTFAST_WAIT);           
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
