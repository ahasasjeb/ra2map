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

#include <climits>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#ifdef _DEBUG
#include <functional>
#endif
#include "VulkanScene.h"
#include "TileSetVulkanRecovery.h"

struct rs_vulkan_renderer;

struct TileSetBrowserLayout
{
	int cols = 1;
	int rows = 0;
	int totalHeight = 0;
	int itemCount = 0;
	int tileWidth = 0;
	int tileHeight = 0;

	static TileSetBrowserLayout Compute(int clientWidth, int itemCount, int tileWidth, int tileHeight)
	{
		TileSetBrowserLayout layout;
		layout.itemCount = itemCount > 0 ? itemCount : 0;
		layout.tileWidth = tileWidth > 0 ? tileWidth : 0;
		layout.tileHeight = tileHeight > 0 ? tileHeight : 0;
		if (layout.itemCount <= 0 || layout.tileWidth <= 0 || layout.tileHeight <= 0)
			return layout;

		const int computedCols = clientWidth / layout.tileWidth;
		layout.cols = computedCols > 0 ? computedCols : 1;
		layout.rows = (layout.itemCount + layout.cols - 1) / layout.cols;
		const int64_t height64 = static_cast<int64_t>(layout.rows) * static_cast<int64_t>(layout.tileHeight);
		layout.totalHeight = height64 > INT_MAX ? INT_MAX : static_cast<int>(height64);
		return layout;
	}

	struct VisibleRowRange
	{
		int firstRow = 0;
		int lastRow = -1;
		bool Empty() const { return firstRow > lastRow; }
	};

	VisibleRowRange ComputeVisibleRows(int scrollY, int viewportHeight, int marginRows = 1) const
	{
		if (rows <= 0 || itemCount <= 0 || tileHeight <= 0 || viewportHeight <= 0)
			return {};

		const int64_t viewTop = scrollY > 0 ? static_cast<int64_t>(scrollY) : 0;
		const int64_t viewBottomExclusive = static_cast<int64_t>(scrollY) + static_cast<int64_t>(viewportHeight);
		if (viewBottomExclusive <= 0)
			return {};

		const int64_t rawFirst = viewTop / tileHeight;
		const int64_t rawLast = (viewBottomExclusive - 1) / tileHeight;
		const int64_t margin = marginRows > 0 ? static_cast<int64_t>(marginRows) : 0;

		const int64_t clampedFirst = (rawFirst - margin) > 0 ? (rawFirst - margin) : 0;
		const int64_t maxRow = static_cast<int64_t>(rows - 1);
		const int64_t clampedLast = (rawLast + margin) < maxRow ? (rawLast + margin) : maxRow;
		if (clampedFirst > clampedLast)
			return {};

		return { static_cast<int>(clampedFirst), static_cast<int>(clampedLast) };
	}

	int HitTest(int listX, int listY) const
	{
		if (rows <= 0 || itemCount <= 0 || tileWidth <= 0 || tileHeight <= 0 || listX < 0 || listY < 0)
			return -1;
		const int col = listX / tileWidth;
		const int row = listY / tileHeight;
		if (col < 0 || col >= cols || row < 0 || row >= rows)
			return -1;
		const int index = row * cols + col;
		return (index >= 0 && index < itemCount) ? index : -1;
	}

	static int ClampScrollPos(int scrollPos, int totalSize, int viewportSize)
	{
		const int safeViewport = viewportSize > 0 ? viewportSize : 0;
		const int maxScroll = totalSize > safeViewport ? (totalSize - safeViewport) : 0;
		if (scrollPos < 0)
			return 0;
		if (scrollPos > maxScroll)
			return maxScroll;
		return scrollPos;
	}
};

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
	int m_currentOverlay = 0;
	int GetAddedHeight(DWORD dwID);
	int m_bottom_needed;
	void UpdateScrollLayout();
	TileSetBrowserLayout ComputeLayout(int clientWidth) const;
	void DrawIt();
	void SetTileSet(DWORD dwTileSet, BOOL bOnlyRedraw=FALSE);
	void SetTileSets(const std::vector<DWORD>& tileSets, BOOL bOnlyRedraw=FALSE);
	DWORD GetTileID(DWORD dwTileSet, DWORD dwType);
	int m_currentTileSet = 0;
	void RetryVulkanRecovery();
	void NotifyVisibilityChanged(bool visible);
	virtual ~CTileSetBrowserView();
#ifdef _DEBUG
	struct VulkanTestHooks
	{
		std::function<TileSetVulkanInitResult(int width, int height, bool vsync, std::string& errorOut)> initialize;
		std::function<VulkanPresentResult(const RECT& source, int width, int height, bool vsync, std::string& errorOut)> present;
		std::function<void()> release;
		std::function<void(const std::string&)> log;
		std::function<bool(UINT_PTR timerId, unsigned int delayMs)> setTimer;
		bool recordSceneCommands = false;
	};

	void SetVulkanTestHooks(VulkanTestHooks* hooks);
	void SetRecoveryConfigForTest(
		int maxAutoRetries,
		unsigned int retryDelayMs,
		int maxTransientSwapchainRetries = TileSetVulkanRecovery::kDefaultMaxTransientSwapchainRetries,
		unsigned int transientDelayMs = TileSetVulkanRecovery::kDefaultTransientSwapchainDelayMs);
	void ConfigureSyntheticTilesForTest(const std::vector<DWORD>& tileIds, int tileWidth, int tileHeight);
	void ConfigureSyntheticOverlaysForTest(const std::vector<std::pair<DWORD, DWORD>>& overlays, int tileWidth, int tileHeight);
	const VulkanScene* GetGpuSceneForTest() const { return m_gpuScene.get(); }
	const TileSetVulkanRecovery& GetVulkanRecoveryForTest() const { return m_vulkanRecovery; }
	const RECT& GetRetryButtonRectForTest() const { return m_retryButtonRect; }
	bool IsRetryTimerActiveForTest() const { return m_activeRetryTimerId != 0; }
	bool IsTransientTimerActiveForTest() const { return m_activeTransientTimerId != 0; }
	UINT_PTR GetActiveRetryTimerIdForTest() const { return m_activeRetryTimerId; }
	UINT_PTR GetActiveTransientTimerIdForTest() const { return m_activeTransientTimerId; }
	int GetLastDrawVisitedItemsForTest() const { return m_lastDrawVisitedItemsForTest; }
	void TriggerDrawForTest(CDC* pDC) { OnDraw(pDC); }

	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

	// Generierte Nachrichtenzuordnungsfunktionen
	//{{AFX_MSG(CTileSetBrowserView)
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg void OnDestroy();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
protected:
	struct OverlayImageRef
	{
		DWORD overlayType;
		DWORD imageIndex;
	};

	TileSetVulkanInitResult InitializeVulkanRenderer(int width, int height, std::string& errorOut);
	VulkanPresentResult PresentVulkanScene(const RECT& source, int width, int height, bool vsync, std::string& errorOut);
	void ReleaseVulkanRenderer();
	bool IsViewVisible() const;
	bool IsViewMinimizedOrZeroSized(const RECT& clientRect) const;
	void ApplyRecoveryAction(const TileSetVulkanRecoveryAction& action);
	bool StartRecoveryTimer(UINT_PTR timerId, unsigned int delayMs, UINT_PTR& activeTimerId);
	void CancelRecoveryTimer(UINT_PTR& activeTimerId);
	void LogRecoveryMessage(const std::string& message);
	void DrawRecoveryFallback(CDC* pDC, const RECT& clientRect);

	int GetCurrentItemCount() const;

	int m_CurrentMode;
	int m_tilecount = 0;
	int m_tile_height = 0;
	int m_tile_width = 0;
	bool m_updatingScrollLayout = false;
	std::vector<DWORD> m_currentTileSets;
	std::vector<DWORD> m_currentTileIds;
	std::vector<OverlayImageRef> m_currentOverlayImages;
	rs_vulkan_renderer* m_vulkanRenderer = nullptr;
	std::unique_ptr<VulkanScene> m_gpuScene;
	TileSetVulkanRecovery m_vulkanRecovery;
	UINT_PTR m_activeRetryTimerId = 0;
	UINT_PTR m_activeTransientTimerId = 0;
	RECT m_retryButtonRect{};
#ifdef _DEBUG
	struct DebugFaultInjection
	{
		bool loaded = false;
		int failInitRemaining = 0;
		int failPresentRemaining = 0;
	};

	VulkanTestHooks* m_testHooks = nullptr;
	bool m_testRendererActive = false;
	int m_lastDrawVisitedItemsForTest = 0;
	DebugFaultInjection m_debugFaultInjection;
#endif
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ fügt unmittelbar vor der vorhergehenden Zeile zusätzliche Deklarationen ein.

#endif // AFX_TILESETBROWSERVIEW_H__3DD92021_7D37_11D4_9C87_97337B61A44A__INCLUDED_
