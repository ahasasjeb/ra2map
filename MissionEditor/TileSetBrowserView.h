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

#if !defined(AFX_TILESETBROWSERVIEW_H__3DD92021_7D37_11D4_9C87_97337B61A44A__INCLUDED_)
#define AFX_TILESETBROWSERVIEW_H__3DD92021_7D37_11D4_9C87_97337B61A44A__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// TileSetBrowserView.h : Header-Datei
//

#include <atlcomcli.h>
#include <vector>

/////////////////////////////////////////////////////////////////////////////
// Ansicht CTileSetBrowserView 

class CTileSetBrowserView : public CScrollView
{
public:
	CTileSetBrowserView();           // Dynamische Erstellung verwendet geschützten Konstruktor
	DECLARE_DYNCREATE(CTileSetBrowserView)

// Attribute
public:

// Operationen
public:

// Überschreibungen
	// Vom Klassen-Assistenten generierte virtuelle Funktionsüberschreibungen
	//{{AFX_VIRTUAL(CTileSetBrowserView)
	protected:
	virtual void OnDraw(CDC* pDC);      // Überschrieben zum Zeichnen dieser Ansicht
	virtual void OnInitialUpdate();     // Zum ersten Mal nach der Konstruktion
	virtual void PostNcDestroy();
	//}}AFX_VIRTUAL

// Implementierung
public:
	void SetOverlay(DWORD dwID);
	void SetOverlays(const std::vector<DWORD>& overlayTypes);
	int m_currentOverlay;
	int GetAddedHeight(DWORD dwID);
	int m_bottom_needed;
	void DrawIt();
	std::vector<CComPtr<IDirectDrawSurface4>> m_lpDDS;
	void SetTileSet(DWORD dwTileSet, BOOL bOnlyRedraw=FALSE);
	void SetTileSets(const std::vector<DWORD>& tileSets, BOOL bOnlyRedraw=FALSE);
	DWORD GetTileID(DWORD dwTileSet, DWORD dwType);
	int m_currentTileSet = 0;
	// Rebuilds the cached tile surfaces after the DirectDraw objects were
	// reinitialized (e.g. after a lost-surface recovery). The old surfaces
	// belong to a lost device and must never be drawn from.
	void ReInitializeTileSurfaces();
	virtual ~CTileSetBrowserView();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

	// Generierte Nachrichtenzuordnungsfunktionen
	//{{AFX_MSG(CTileSetBrowserView)
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
protected:
	struct OverlayImageRef
	{
		DWORD overlayType;
		DWORD imageIndex;
	};

	int m_CurrentMode;
	LPDIRECTDRAWSURFACE4 RenderTile(DWORD dwID);
	int m_tilecount = 0;
	int m_tile_height = 0;
	int m_tile_width = 0;
	std::vector<DWORD> m_currentTileSets;
	std::vector<DWORD> m_currentTileIds;
	std::vector<OverlayImageRef> m_currentOverlayImages;
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ fügt unmittelbar vor der vorhergehenden Zeile zusätzliche Deklarationen ein.

#endif // AFX_TILESETBROWSERVIEW_H__3DD92021_7D37_11D4_9C87_97337B61A44A__INCLUDED_
