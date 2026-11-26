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

// TileSetBrowserView.cpp: Implementierungsdatei
//

#include "stdafx.h"
#include "finalsun.h"
#include "TileSetBrowserView.h"
#include "FinalSunDlg.h"
#include "LineDrawer.h"
#include "mapdata.h"
#include "variables.h"
#include "functions.h"

extern ACTIONDATA AD;

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CTileSetBrowserView

IMPLEMENT_DYNCREATE(CTileSetBrowserView, CScrollView)

CTileSetBrowserView::CTileSetBrowserView()
{
	m_bottom_needed = 1000;
	m_CurrentMode = 0;
}

CTileSetBrowserView::~CTileSetBrowserView()
{
	ReleaseVulkanRenderer();
}


BEGIN_MESSAGE_MAP(CTileSetBrowserView, CScrollView)
	//{{AFX_MSG_MAP(CTileSetBrowserView)
	ON_WM_LBUTTONDOWN()
	ON_WM_VSCROLL()
	ON_WM_MOUSEWHEEL()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_DESTROY()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// Zeichnung CTileSetBrowserView 

void CTileSetBrowserView::OnInitialUpdate()
{
	CScrollView::OnInitialUpdate();
}

// for fast overlay drawing use IsoView´s overlay table
extern PICDATA* ovrlpics[0xFF][max_ovrl_img];

bool CTileSetBrowserView::InitializeVulkanRenderer()
{
	if (m_vulkanRenderer != nullptr && m_gpuScene != nullptr)
		return true;
	if (m_hWnd == nullptr)
		return false;

	if (rs_vulkan_create(m_hWnd, &m_vulkanRenderer) == RS_OK)
	{
		RECT clientRect{};
		GetClientRect(&clientRect);
		const int width = clientRect.right - clientRect.left;
		const int height = clientRect.bottom - clientRect.top;
		if (width <= 0 || height <= 0 ||
			rs_vulkan_prepare(m_vulkanRenderer, width, height, theApp.m_Options.bVSync ? 1 : 0) == RS_OK)
		{
			m_gpuScene = std::make_unique<VulkanScene>(m_vulkanRenderer);
			return true;
		}
	}

	char diagnostic[1024]{};
	rs_vulkan_last_error(diagnostic, sizeof(diagnostic));
	errstream << "TileSetBrowserView Vulkan renderer initialization failed: " << diagnostic << endl;
	errstream.flush();
	ReleaseVulkanRenderer();
	return false;
}

void CTileSetBrowserView::ReleaseVulkanRenderer()
{
	m_gpuScene.reset();
	if (m_vulkanRenderer != nullptr)
	{
		rs_vulkan_destroy(m_vulkanRenderer);
		m_vulkanRenderer = nullptr;
	}
}

void CTileSetBrowserView::OnDraw(CDC* pDC)
{
	if (((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->b_IsLoading)
		return;

	RECT r{};
	GetClientRect(&r);
	if (r.right <= 0 || r.bottom <= 0)
		return;

	if (!InitializeVulkanRenderer())
		return;

	const int scrollX = GetScrollPos(SB_HORZ);
	const int scrollY = GetScrollPos(SB_VERT);
	const RECT source{ scrollX, scrollY, scrollX + r.right, scrollY + r.bottom };
	m_gpuScene->Begin(source);

	if (tiledata != NULL && (*tiledata) != NULL && m_tilecount > 0 && m_tile_width > 0)
	{
		int max_r = r.right / m_tile_width;
		if (max_r <= 0) max_r = 1;

		int cur_y = 0;
		int cur_x = 0;

		if (m_CurrentMode == 1)
		{
			for (int i = 0; i < m_tilecount; i++)
			{
				const DWORD dwID = m_currentTileIds[i];
				if (cur_y + m_tile_height >= source.top && cur_y <= source.bottom)
				{
					DWORD renderID = dwID;
					if (theApp.m_Options.bMarbleMadness && (*tiledata)[dwID].wMarbleGround != 0xFFFF)
						renderID = (*tiledata)[dwID].wMarbleGround;

					const TILEDATA& td = (*tiledata)[renderID];
					const int added_height = GetAddedHeight(renderID);
					const int curwidth = td.rect.right - td.rect.left;
					const int curheight = added_height + td.rect.bottom - td.rect.top;
					const int base_x = cur_x + (m_tile_width - curwidth) / 2;
					const int base_y = cur_y + (m_tile_height - curheight) / 2;
					const RECT tileClip{
						max(source.left, static_cast<LONG>(base_x)),
						max(source.top, static_cast<LONG>(base_y)),
						min(source.right, static_cast<LONG>(base_x + curwidth)),
						min(source.bottom, static_cast<LONG>(base_y + curheight))
					};

					int p = 0;
					for (int si = 0; si < td.cx; si++)
					{
						for (int se = 0; se < td.cy; se++)
						{
							if (p < td.wTileCount && td.tiles[p].pic != nullptr)
							{
								const SUBTILE& st = td.tiles[p];
								const int drawx = base_x + se * f_x / 2 - si * f_x / 2 - td.rect.left + st.sX;
								const int drawy = base_y + se * f_y / 2 + si * f_y / 2 - td.rect.top + added_height + st.sY - st.bZHeight * f_y / 2;
								m_gpuScene->Indexed(st.pic, st.wWidth, st.wHeight,
									reinterpret_cast<const short*>(st.vborder), nullptr, iPalIso, nullptr,
									false, drawx, drawy, tileClip);
							}
							p++;
						}
					}

					if (AD.mode == ACTIONMODE_SETTILE && AD.type == dwID)
					{
						LineDrawer drawer(*m_gpuScene);
						drawer.Rectangle(cur_x + 2, cur_y + 2, cur_x + m_tile_width - 2, cur_y + m_tile_height - 2, 0x00ff0000);
					}
				}

				cur_x += m_tile_width;
				if (i % max_r == max_r - 1)
				{
					cur_y += m_tile_height;
					cur_x = 0;
				}
			}
		}
		else if (m_CurrentMode == 2)
		{
			for (size_t displayIndex = 0; displayIndex < m_currentOverlayImages.size(); ++displayIndex)
			{
				const OverlayImageRef& image = m_currentOverlayImages[displayIndex];
				PICDATA* p = ovrlpics[image.overlayType][image.imageIndex];
				if (p != NULL && p->pic != NULL)
				{
					if (cur_y + m_tile_height >= source.top && cur_y <= source.bottom)
					{
						const int curwidth = p->wMaxWidth;
						const int curheight = p->wMaxHeight;
						const int drawx = cur_x + (m_tile_width - curwidth) / 2;
						const int drawy = cur_y + (m_tile_height - curheight) / 2;

						if (p->bType == PICDATA_TYPE_BMP && p->bitmap)
						{
							m_gpuScene->Bitmap(*p->bitmap, drawx, drawy);
						}
						else
						{
							m_gpuScene->Indexed(static_cast<const BYTE*>(p->pic), curwidth, curheight,
								reinterpret_cast<const short*>(p->vborder),
								p->lighting && !p->lighting->empty() ? p->lighting->data() : nullptr,
								p->pal ? p->pal : iPalIso, nullptr, false, drawx, drawy, source);
						}

						if (AD.mode == ACTIONMODE_PLACE && AD.data2 == image.overlayType && AD.data3 == image.imageIndex && AD.data == 33 && AD.type == 6)
						{
							LineDrawer drawer(*m_gpuScene);
							drawer.Rectangle(cur_x + 2, cur_y + 2, cur_x + m_tile_width - 2, cur_y + m_tile_height - 2, 0x00ff0000);
						}
					}

					cur_x += m_tile_width;
					if (displayIndex % max_r == max_r - 1)
					{
						cur_y += m_tile_height;
						cur_x = 0;
					}
				}
			}
		}
	}

	if (!m_gpuScene->Present(source, r.right, r.bottom, theApp.m_Options.bVSync))
	{
		char error[1024]{};
		rs_vulkan_last_error(error, sizeof(error));
		errstream << "TileSetBrowserView Vulkan present failed: " << error << endl;
		errstream.flush();
		ReleaseVulkanRenderer();
		Invalidate(FALSE);
	}
}

/////////////////////////////////////////////////////////////////////////////
// Diagnose CTileSetBrowserView

#ifdef _DEBUG
void CTileSetBrowserView::AssertValid() const
{
	CScrollView::AssertValid();
}

void CTileSetBrowserView::Dump(CDumpContext& dc) const
{
	CScrollView::Dump(dc);
}
#endif //_DEBUG

/////////////////////////////////////////////////////////////////////////////
// Behandlungsroutinen für Nachrichten CTileSetBrowserView 

void CTileSetBrowserView::PostNcDestroy()
{
	// CScrollView::PostNcDestroy();
}

DWORD CTileSetBrowserView::GetTileID(DWORD dwTileSet, DWORD dwType)
{
	int i, e;
	DWORD tilecount = 0;
	for (i = 0;i < 10000;i++)
	{
		CString tset;
		char c[50];
		itoa(i, c, 10);
		int e;
		for (e = 0;e < 4 - strlen(c);e++)
			tset += "0";
		tset += c;
		CString sec = "TileSet";
		sec += tset;

		if (tiles->sections.find(sec) == tiles->sections.end())
			return 0xFFFFFFFF;


		for (e = 0;e < atoi(tiles->sections[sec].values["TilesInSet"]);e++)
		{
			if (i == dwTileSet && e == dwType)
				return tilecount;
			tilecount++;

		}


	}

	return tilecount;
}

void CTileSetBrowserView::SetTileSet(DWORD dwTileSet, BOOL bOnlyRedraw)
{
	if (bOnlyRedraw && !m_currentTileSets.empty())
	{
		SetTileSets(m_currentTileSets, TRUE);
		return;
	}

	SetTileSets({ dwTileSet }, bOnlyRedraw);
}

void CTileSetBrowserView::SetTileSets(const std::vector<DWORD>& tileSets, BOOL bOnlyRedraw)
{
	if (tileSets.empty())
		return;

	m_currentTileSets = tileSets;
	m_currentTileSet = tileSets.front();
	m_CurrentMode = 1;

	m_tile_width = 0;
	m_tile_height = 0;
	m_tilecount = 0;
	m_currentTileIds.clear();

	for (const DWORD tileSet : tileSets)
	{
		CString tset;
		tset.Format("%04lu", tileSet);
		const int tileCount = atoi(tiles->sections[(CString)"TileSet" + tset].values["TilesInSet"]);
		const DWORD startId = GetTileID(tileSet, 0);

		for (int tileIndex = 0; tileIndex < tileCount; ++tileIndex)
		{
#ifdef RA2_MODE
			const bool hasUnusedBridgeTiles =
				(tileSet == 80 && Map->GetTheater() == "TEMPERATE") ||
				(tileSet == 73 && Map->GetTheater() == "SNOW") ||
				(tileSet == 101 && Map->GetTheater() == "URBAN");
			if (hasUnusedBridgeTiles && (tileIndex == 10 || tileIndex == 15))
				continue;
#endif
			if (startId + tileIndex < *tiledata_count)
				m_currentTileIds.push_back(startId + tileIndex);
		}
	}

	if (m_currentTileIds.empty())
	{
		RedrawWindow();
		return;
	}

	const DWORD firstTileId = m_currentTileIds.front();
	const DWORD firstTileSet = (*tiledata)[firstTileId].wTileSet;
	if ((*tiledata)[firstTileId].wTileCount && (*tiledata)[firstTileId].tiles[0].pic)
	{
		if (!bOnlyRedraw)
		{
			AD.mode = ACTIONMODE_SETTILE;
			AD.type = firstTileId;
			AD.data = 0;
			AD.data2 = 0;
			AD.z_data = 0;

			((CFinalSunDlg*)theApp.m_pMainWnd)->m_settingsbar.m_BrushSize = 0;
			((CFinalSunDlg*)theApp.m_pMainWnd)->m_settingsbar.UpdateData(FALSE);
			((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->m_BrushSize_x = 1;
			((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->m_BrushSize_y = 1;

			for (int i = 0;i < g_data.sections["StdBrushSize"].values.size();i++)
			{
				CString n = *g_data.sections["StdBrushSize"].GetValueName(i);
				if ((*tiles).sections["General"].FindName(n) >= 0)
				{
					int tset = atoi((*tiles).sections["General"].values[n]);
					if (tset == firstTileSet)
					{
						int bs = atoi(*g_data.sections["StdBrushSize"].GetValue(i));
						((CFinalSunDlg*)theApp.m_pMainWnd)->m_settingsbar.m_BrushSize = bs - 1;
						((CFinalSunDlg*)theApp.m_pMainWnd)->m_settingsbar.UpdateData(FALSE);
						((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->m_BrushSize_x = bs;
						((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->m_BrushSize_y = bs;
					}
				}
			}
		}
	}
	for (const DWORD tileId : m_currentTileIds)
	{
		if ((*tiledata)[tileId].rect.right - (*tiledata)[tileId].rect.left > m_tile_width) m_tile_width = (*tiledata)[tileId].rect.right - (*tiledata)[tileId].rect.left;
		if (GetAddedHeight(tileId) + (*tiledata)[tileId].rect.bottom - (*tiledata)[tileId].rect.top > m_tile_height) m_tile_height = GetAddedHeight(tileId) + (*tiledata)[tileId].rect.bottom - (*tiledata)[tileId].rect.top;
	}

	m_tile_width += 6;
	m_tile_height += 6;
	m_tilecount = static_cast<int>(m_currentTileIds.size());

	RECT r;
	GetClientRect(&r);
	int max_r = r.right / m_tile_width;
	if (max_r <= 0) max_r = 1;
	m_bottom_needed = m_tile_height * (1 + m_tilecount / max_r);
	GetParentFrame()->RecalcLayout(TRUE);

	RedrawWindow();

	((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->SetForegroundWindow();
	((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->SetFocus();
}

void CTileSetBrowserView::DrawIt()
{
	CPaintDC myDC(this);
}

void CTileSetBrowserView::OnLButtonDown(UINT nFlags, CPoint point)
{
	RECT r;
	GetClientRect(&r);

	if (m_tilecount == 0)
		return;

	SCROLLINFO scrinfo;
	scrinfo.cbSize = sizeof(SCROLLINFO);
	scrinfo.fMask = SIF_ALL;
	GetScrollInfo(SB_HORZ, &scrinfo);
	point.x += scrinfo.nPos;
	GetScrollInfo(SB_VERT, &scrinfo);
	point.y += scrinfo.nPos;


	int max_r = r.right / m_tile_width;

	if (max_r == 0) max_r = 1;

	int cur_y = 0;
	int cur_x = 0;

	int tile_width = m_tile_width;
	int tile_height = m_tile_height;

	if (m_CurrentMode == 1)
	{
		int i;
		for (i = 0;i < m_tilecount;i++)
		{
			const DWORD dwID = m_currentTileIds[i];
			int curwidth = m_tile_width;
			int curheight = m_tile_height;

			int posaddedx = (m_tile_width - curwidth) / 2;
			int posaddedy = (m_tile_height - curheight) / 2;

			if (point.x > cur_x + posaddedx && point.y > cur_y + posaddedy && point.x < cur_x + tile_width - posaddedx && point.y < cur_y + tile_height - posaddedy)
			{
				char c[50];
				itoa(GetAddedHeight(dwID), c, 10);
				OutputDebugString(c);

				int oldmode = AD.mode;
				int oldid = AD.type;

				AD.mode = ACTIONMODE_SETTILE;
				AD.type = dwID;
				AD.data = 0;
				AD.data2 = 0;
				AD.data3 = 0;
				AD.z_data = 0;

				if (oldid < 0 || oldid >= *tiledata_count) oldid = 0;

				const DWORD selectedTileSet = (*tiledata)[dwID].wTileSet;
				if (oldmode != ACTIONMODE_SETTILE || (*tiledata)[oldid].wTileSet != selectedTileSet)
				{
					((CFinalSunDlg*)theApp.m_pMainWnd)->m_settingsbar.m_BrushSize = 0;
					((CFinalSunDlg*)theApp.m_pMainWnd)->m_settingsbar.UpdateData(FALSE);
					((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->m_BrushSize_x = 1;
					((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->m_BrushSize_y = 1;

					int i;
					for (i = 0;i < g_data.sections["StdBrushSize"].values.size();i++)
					{
						CString n = *g_data.sections["StdBrushSize"].GetValueName(i);
						if ((*tiles).sections["General"].FindName(n) >= 0)
						{
							int tset = atoi((*tiles).sections["General"].values[n]);
							if (tset == selectedTileSet)
							{
								int bs = atoi(*g_data.sections["StdBrushSize"].GetValue(i));
								((CFinalSunDlg*)theApp.m_pMainWnd)->m_settingsbar.m_BrushSize = bs - 1;
								((CFinalSunDlg*)theApp.m_pMainWnd)->m_settingsbar.UpdateData(FALSE);
								((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->m_BrushSize_x = bs;
								((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->m_BrushSize_y = bs;
							}
						}
					}
				}

				RedrawWindow();
				return;
			}

			cur_x += tile_width;
			if (i % max_r == max_r - 1)
			{
				cur_y += tile_height;
				cur_x = 0;
			}
		}
	}
	else if (m_CurrentMode == 2)
	{
		for (size_t displayIndex = 0; displayIndex < m_currentOverlayImages.size(); ++displayIndex)
		{
			const OverlayImageRef& image = m_currentOverlayImages[displayIndex];
			PICDATA* p = ovrlpics[image.overlayType][image.imageIndex];
			if (p != NULL && p->pic != NULL)
			{
				int curwidth = m_tile_width;
				int curheight = m_tile_height;

				int posaddedx = (m_tile_width - curwidth) / 2;
				int posaddedy = (m_tile_height - curheight) / 2;

				if (point.x > cur_x + posaddedx && point.y > cur_y + posaddedy && point.x < cur_x + tile_width - posaddedx && point.y < cur_y + tile_height - posaddedy)
				{
					AD.mode = ACTIONMODE_PLACE;
					AD.type = 6;
					AD.data = 33;
					AD.data2 = image.overlayType;
					AD.data3 = image.imageIndex;
					RedrawWindow();
					return;
				}

				cur_x += tile_width;
				if (displayIndex % max_r == max_r - 1)
				{
					cur_y += tile_height;
					cur_x = 0;
				}
			}

		}
	}


	((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->SetForegroundWindow();
	((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->SetFocus();

	CScrollView::OnLButtonDown(nFlags, point);
}

// calculates additional height added to the top of a tile if needed
int CTileSetBrowserView::GetAddedHeight(DWORD dwID)
{
	int cur_added = 0;
	int i, e, p = 0;
	for (i = 0;i < (*tiledata)[dwID].cx;i++)
	{
		for (e = 0;e < (*tiledata)[dwID].cy;e++)
		{
			int drawy = e * f_y / 2 + i * f_y / 2 - (*tiledata)[dwID].rect.top;
			drawy += (*tiledata)[dwID].tiles[p].sY - (*tiledata)[dwID].tiles[p].bZHeight * f_y / 2;
			if (drawy < cur_added) cur_added = drawy;
			p++;
		}
	}
	return -cur_added;
}

void CTileSetBrowserView::SetOverlay(DWORD dwID)
{
	SetOverlays({ dwID });
}

void CTileSetBrowserView::SetOverlays(const std::vector<DWORD>& overlayTypes)
{
	if (overlayTypes.empty())
		return;

	m_currentOverlayImages.clear();
	m_currentOverlay = overlayTypes.front();
	m_tile_width = 0;
	m_tile_height = 0;
	m_tilecount = 0;

	bool loadedGraphics = false;
	for (const DWORD overlayType : overlayTypes)
	{
		bool found = false;
		for (int imageIndex = 0; imageIndex < max_ovrl_img; ++imageIndex)
		{
			PICDATA* picture = ovrlpics[overlayType][imageIndex];
			if (picture != NULL && picture->pic != NULL)
			{
				found = true;
				break;
			}
		}

		if (!found)
		{
			theApp.m_loading->LoadOverlayGraphic(*rules.sections["OverlayTypes"].GetValue(overlayType), overlayType);
			loadedGraphics = true;
		}
	}

	if (loadedGraphics)
		((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->UpdateOverlayPictures();

	for (const DWORD overlayType : overlayTypes)
	{
		for (int imageIndex = 0; imageIndex < max_ovrl_img; ++imageIndex)
		{
			PICDATA* picture = ovrlpics[overlayType][imageIndex];
			if (picture == NULL || picture->pic == NULL)
				continue;

			m_currentOverlayImages.push_back({ overlayType, static_cast<DWORD>(imageIndex) });
			if (picture->wMaxWidth > m_tile_width)
				m_tile_width = picture->wMaxWidth;
			if (picture->wMaxHeight > m_tile_height)
				m_tile_height = picture->wMaxHeight;
		}
	}

	if (m_currentOverlayImages.empty())
	{
		RedrawWindow();
		return;
	}

	((CFinalSunDlg*)theApp.m_pMainWnd)->m_settingsbar.m_BrushSize = 0;
	((CFinalSunDlg*)theApp.m_pMainWnd)->m_settingsbar.UpdateData(FALSE);
	((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->m_BrushSize_x = 1;
	((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->m_BrushSize_y = 1;

	m_CurrentMode = 2;
	m_tilecount = static_cast<int>(m_currentOverlayImages.size());
	m_tile_width += 6;
	m_tile_height += 6;

	RECT r;
	GetClientRect(&r);
	int max_r = r.right / m_tile_width;
	if (max_r <= 0) max_r = 1;
	m_bottom_needed = m_tile_height * (1 + m_tilecount / max_r);
	GetParentFrame()->RecalcLayout(TRUE);
	RedrawWindow();

	((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->SetForegroundWindow();
	((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->SetFocus();
}

void CTileSetBrowserView::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	CScrollView::OnVScroll(nSBCode, nPos, pScrollBar);
	Invalidate(FALSE);
}

BOOL CTileSetBrowserView::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	const BOOL handled = CScrollView::OnMouseWheel(nFlags, zDelta, pt);
	Invalidate(FALSE);
	return handled;
}

BOOL CTileSetBrowserView::OnEraseBkgnd(CDC* pDC)
{
	if (m_vulkanRenderer != nullptr)
		return TRUE;
	return CScrollView::OnEraseBkgnd(pDC);
}

void CTileSetBrowserView::OnSize(UINT nType, int cx, int cy)
{
	CScrollView::OnSize(nType, cx, cy);
	Invalidate(FALSE);
}

void CTileSetBrowserView::OnDestroy()
{
	ReleaseVulkanRenderer();
	CScrollView::OnDestroy();
}
