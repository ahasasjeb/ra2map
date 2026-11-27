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

#if !defined(AFX_ISOVIEW_H__7FB6D6A0_7B52_11D3_99E1_DA6DFD21E706__INCLUDED_)
#define AFX_ISOVIEW_H__7FB6D6A0_7B52_11D3_99E1_DA6DFD21E706__INCLUDED_


#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// IsoView.h : header file
//



/////////////////////////////////////////////////////////////////////////////
// View CIsoView 

#include <vector>
#include <memory>
#include <map>
#include <string>
#include "MissionEditorPackLib.h"
#include "Structs.h"
#include "PanScroll.h"
#include "VulkanScene.h"
#include <chrono>

struct rs_vulkan_renderer;



struct TextToRender
{
	std::string text;
	int drawx;
	int drawy;
	int color;
	bool fixedScreenPos = false;	
	bool useFont9 = false;
	bool centered = false;
};

struct FIELDDATA;

struct WaypointToRender
{
	int drawx;
	int drawy;
};

class TextDrawer;
class CTube;


class CIsoView : public CView
{
protected:
	DECLARE_DYNCREATE(CIsoView)

// attributes
public:
	INT m_mapx;
	INT m_mapy;
	int m_FlattenHeight; // for flatten ground
	int m_FlattenLastX;
	int m_FlattenLastY;
	int m_TileChangeCount;
private:
	ProjectedVec m_viewOffset;
	Vec2<CSProjected, float> m_viewScale;  // this is used for display and may e.g. be locked to zoom steps
	float m_viewScaleControl;  // you control these with your mouse
	CPoint m_MButtonDown;
	CPoint m_MButtonMoveZooming;

	int rclick_x; // scroll pixel coordinates
	int rclick_y;
	int cur_x_mouse;
	int cur_y_mouse;
	BOOL rscroll;
	PanScrollMotion m_panMotion;
	std::chrono::steady_clock::time_point m_lastPanTick;
	BOOL m_bAltCliff;
	bool m_zooming;

	std::vector<TextToRender> m_texts_to_render;
	std::vector<WaypointToRender> m_waypoints_to_render;
	std::vector<ProjectedCoords> m_celltags_to_render;
	std::map<CString, COLORREF> m_colorCache;  // per-frame house color cache, cleared at DrawMap start
	std::map<std::string, CString> m_picFileCache;  // (unit type + direction) -> picture filename

	bool m_resizePreview = false;  // present the cached frame while live-resizing
	RECT m_lastPresentSourceRect = { 0, 0, 0, 0 };

	

// operations
public:
	CIsoView();           
	virtual ~CIsoView();

// overwriteables
	//{{AFX_VIRTUAL(CIsoView)
	public:
	virtual BOOL Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID, CCreateContext* pContext = NULL);
	protected:
	virtual void OnDraw(CDC* pDC);      // Überschrieben zum Zeichnen dieser Ansicht
	virtual void OnInitialUpdate();     // Zum ersten Mal nach der Konstruktion
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
	//}}AFX_VIRTUAL

// implementation
protected:
	
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif


	// generated message maps
	//{{AFX_MSG(CIsoView)
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);	
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMove(int x, int y);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnDestroy();
	afx_msg void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnDeadChar(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
public:
	void FocusWaypoint(int index);
	void FocusMapCoordinate(int x, int y);
	RECT m_funcRect;
	Vec2<CSProjected, float> GetViewScale() const
	{
		return m_viewScale;
	}
	ProjectedVec GetViewOffset() const
	{
		return m_viewOffset;
	}
	RECT GetScaledDisplayRect() const;
	void SetScroll(int xscroll, int yscroll);
	void DrawMap();
	struct DrawMapCellContext
	{
		VulkanScene& scene;
		RECT r;
		DWORD MM_heightstart;
		BOOL bMarbleHeight;
	};
	void DrawMapTerrainCell(const FIELDDATA& m, const ProjectedCoords& drawCoords, const DrawMapCellContext& ctx);
	void DrawMapTerrainAnim(const FIELDDATA& m, const ProjectedCoords& drawCoords, const DrawMapCellContext& ctx, DWORD dwOrigGround);
	void DrawMapObjectsCell(const MapCoords& mapCoords, const FIELDDATA& m, const ProjectedCoords& drawCoords, const DrawMapCellContext& ctx);
	void AutoLevel();
	void FillArea(DWORD dwX, DWORD dwY, DWORD dwID, BYTE bSubTile);
	BOOL m_NoMove;
	void PlaceCurrentObjectAt(int x, int y);
	void DrawObjectPreviewAt(int x, int y);
	void SavePreviewRegion(const RECT& rect);
	void RestorePreviewRegion();
	void PlaceTile(const int x, const int y, const UINT nMouseFlags);
	void ShowAllTileSets();
	void HideTileSet(DWORD dwTileSet);
	void FlipHighResBuffer(const RECT* sourceRectOverride = nullptr);
	void RenderUIOverlay();
	bool InitializeVulkanRenderer();
	void ReleaseVulkanRenderer();
	int m_BrushSize_x;
	int m_BrushSize_y;
	BOOL ReachableFrom(DWORD dwStart, DWORD dwEnd);
	void __fastcall ChangeTileHeight(DWORD dwPos, DWORD dwNewHeight, BOOL bNonMorphableMove, BOOL bOnlyThisTile=FALSE, BOOL bNoSlopes=FALSE);
	void UpdateOverlayPictures(int id=-1);
	void UpdateStatusBar(int x, int y);
	void DrawCell(int x, int y, int w, int h, COLORREF col, BOOL dotted=FALSE);
	void DrawCell(VulkanScene& scene, int x, int y, int w, int h, int col, bool dotted=false, bool touchNeighbours=false, int colNeighbour=CLR_INVALID) const;
	void DrawCellCursor(const MapCoords& mapCoords);
	void DrawTube(const CTube& tube, const COLORREF* color=nullptr) const;

	/// <summary>
	/// Converts from (world) pixel coordinates to logical map coordinates
	/// </summary>
	/// <param name="projCoords">World pixel coordinates</param>
	/// <returns>Logical map coordinates</returns>
	MapCoords GetMapCoordinates(const ProjectedCoords& projCoords, bool bAllowAccessBehindCliffs=false, bool ignoreHideFlags = false) const;

	/// <summary>
	/// Converts from Win32 window client coordinates (0/0 at the top left corner of the view window) to logical map coordinates
	/// </summary>
	/// <param name="clientPt">Client coordinates as given e.g. by window messages</param>
	/// <returns>Logical map coordinates</returns>
	MapCoords GetMapCoordinatesFromClientCoordinates(const CPoint& clientPt, bool bAllowAccessBehindCliffs = false, bool ignoreHideFlags = false) const;

	/// <summary>
	/// Converts from Win32 window client coordinates (0/0 at the top left corner of the view window) to (world) pixel coordinates
	/// </summary>
	/// <param name="clientPt">Client coordinates as given e.g. by window messages</param>
	/// <returns>World pixel coordinates</returns>
	ProjectedCoords GetProjectedCoordinatesFromClientCoordinates(const CPoint& clientPt) const;

	/// <summary>
	/// Converts from logical map coordinates to (world) pixel coordinates
	/// </summary>
	/// <param name="mapCoords">Logical map coordinates</param>
	/// <returns>World pixel coordinates</returns>
	ProjectedCoords GetProjectedCoordinates(const MapCoords& mapCoords) const;

	/// <summary>
	/// Converts from logical map coordinates to (world) pixel coordinates
	/// </summary>
	/// <param name="mapCoords">Logical map coordinates</param>
	/// <param name="mapZ">Fixed logical height</param>
	/// <returns>World pixel coordinates</returns>
	ProjectedCoords GetProjectedCoordinates(const MapCoords& mapCoords, int mapZ) const;

	/// <summary>
	/// Converts from logical map coordinates to view / render target pixel coordinates (screen backbuffer, e.g. currently not yet scaled by view zoom)
	/// </summary>
	/// <param name="mapCoords">Logical map coordinates</param>
	/// <returns>Texel coordinates of the screen backbuffer</returns>
	ProjectedCoords GetRenderTargetCoordinates(const MapCoords& mapCoords) const;

	/// <summary>
	/// Converts from logical map coordinates to view / render target pixel coordinates (screen backbuffer, e.g. currently not yet scaled by view zoom)
	/// </summary>
	/// <param name="mapCoords">Logical map coordinates</param>
	/// <param name="mapZ">Fixed logical height</param>
	/// <returns>Texel coordinates of the screen backbuffer</returns>
	ProjectedCoords GetRenderTargetCoordinates(const MapCoords& mapCoords, int mapZ) const;

	/// <summary>
	/// Applies the scaling as it is being done when blitting from back to frontbuffer, required e.g. for drawing calls directly to the frontbuffer
	/// If you use mouse-move coordinates you need to
	/// </summary>
	/// <param name="projCoords">Texel coordinates of the backbuffer render target</param>
	/// <returns>Texel coordinates of the frontbuffer render target</returns>
	ProjectedCoords ScaleBackToFrontBuffer(const ProjectedCoords& backBufferCoords) const;

	void HandleTrail(int x, int y);
	int GetOverlayDirection(int x, int y);
	void SetError(const char* text);
	CWnd* owner;
	COLORREF GetColor(const char* house, const char* color=NULL);
	COLORREF GetColorImpl(const char* house, const char* color);
	CString GetCachedUnitPictureFilename(LPCTSTR type, int dir);
	void updateFontScaled();

	BOOL m_previewHasSaved = FALSE;
	std::unique_ptr<FSunPackLib::ColorConverter> m_color_converter;
	// Opaque Rust-owned Vulkan renderer and GPU scene recorder.
	rs_vulkan_renderer* m_vulkanRenderer;
	std::unique_ptr<VulkanScene> m_gpuScene;
	bool m_lastPresentSucceeded = false;
	int m_vulkanPresentFailures;
	MainCanvasPresentState m_presentState;
	void HandleProperties(int n, int type);
	void UpdateDialog(BOOL bRepos=TRUE);
	CMenu m_menu;
	BOOL b_IsLoading;
	int m_fontDefaultHeight;
	int m_Font9Height;
	std::unique_ptr<TextDrawer> m_textDefault;	
	std::unique_ptr<TextDrawer> m_textScaled;
	std::unique_ptr<TextDrawer> m_text9;
	std::unique_ptr<TextDrawer> m_text9Scaled;
	std::unique_ptr<TextDrawer> m_textBlue;
	std::unique_ptr<TextDrawer> m_textBlue9;
	std::unique_ptr<TextDrawer> m_textBlueScaled;
	std::unique_ptr<TextDrawer> m_textBlue9Scaled;

private:
	void UpdateScrollRanges();

private:
	RECT m_myRect;
	
	
	// mapdata* _map;
	RECT line;
	int m_type;
	int m_id;
	BOOL m_drag;
	BOOL m_moved;
	MapCoords m_cellCursor;
	MapCoords m_lastPlacementCell = MapCoords(-1, -1); // suppress duplicate placement messages within one map cell
	CPoint m_lastPlacementPoint = CPoint(-1, -1); // infantry may fill the three sub-cells while the pointer moves
public:
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	void Zoom(CPoint& pt, float f);
	afx_msg void OnMButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMButtonUp(UINT nFlags, CPoint point);
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}

#endif // AFX_ISOVIEW_H__7FB6D6A0_7B52_11D3_99E1_DA6DFD21E706__INCLUDED_
