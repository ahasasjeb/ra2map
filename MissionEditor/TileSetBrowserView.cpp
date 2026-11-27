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
	m_activeRetryTimerId = 0;
	m_activeTransientTimerId = 0;
	ReleaseVulkanRenderer();
}


BEGIN_MESSAGE_MAP(CTileSetBrowserView, CScrollView)
	//{{AFX_MSG_MAP(CTileSetBrowserView)
	ON_WM_LBUTTONDOWN()
	ON_WM_VSCROLL()
	ON_WM_MOUSEWHEEL()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_TIMER()
	ON_WM_SHOWWINDOW()
	ON_WM_SETCURSOR()
	ON_WM_DESTROY()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

namespace
{
	std::string ReadLastVulkanError()
	{
		char error[1024]{};
		rs_vulkan_last_error(error, sizeof(error));
		return std::string(error);
	}

	CString GetLocalizedTextOrFallback(const char* key, const char* fallback)
	{
		CString text = GetLanguageStringACP(key);
		if (text.IsEmpty())
			return CString(fallback);
		return text;
	}

#ifdef _DEBUG
	int ParseDebugFaultCountEnv(const char* name)
	{
		char buffer[64]{};
		const DWORD length = ::GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(sizeof(buffer)));
		if (length == 0 || length >= sizeof(buffer))
			return 0;
		if (_stricmp(buffer, "always") == 0 || _stricmp(buffer, "inf") == 0 || strcmp(buffer, "-1") == 0)
			return -1;
		const int count = atoi(buffer);
		return count > 0 ? count : 0;
	}

	bool ConsumeDebugFault(int& remaining)
	{
		if (remaining < 0)
			return true;
		if (remaining > 0)
		{
			--remaining;
			return true;
		}
		return false;
	}
#endif
}

/////////////////////////////////////////////////////////////////////////////
// Zeichnung CTileSetBrowserView 

void CTileSetBrowserView::OnInitialUpdate()
{
	CScrollView::OnInitialUpdate();
}

// for fast overlay drawing use IsoView´s overlay table
extern PICDATA* ovrlpics[0xFF][max_ovrl_img];

#ifdef _DEBUG
void CTileSetBrowserView::SetVulkanTestHooks(VulkanTestHooks* hooks)
{
	ReleaseVulkanRenderer();
	m_testHooks = hooks;
	m_testRendererActive = false;
}

void CTileSetBrowserView::SetRecoveryConfigForTest(
	int maxAutoRetries,
	unsigned int retryDelayMs,
	int maxTransientSwapchainRetries,
	unsigned int transientDelayMs)
{
	CancelRecoveryTimer(m_activeRetryTimerId);
	CancelRecoveryTimer(m_activeTransientTimerId);
	m_vulkanRecovery = TileSetVulkanRecovery(
		maxAutoRetries,
		retryDelayMs,
		maxTransientSwapchainRetries,
		transientDelayMs);
	m_retryButtonRect = RECT{};
}

void CTileSetBrowserView::ConfigureSyntheticTilesForTest(
	const std::vector<DWORD>& tileIds, int tileWidth, int tileHeight)
{
	m_CurrentMode = 1;
	m_currentTileIds = tileIds;
	m_currentOverlayImages.clear();
	m_tile_width = tileWidth;
	m_tile_height = tileHeight;
	m_tilecount = static_cast<int>(tileIds.size());
	UpdateScrollLayout();
}

void CTileSetBrowserView::ConfigureSyntheticOverlaysForTest(
	const std::vector<std::pair<DWORD, DWORD>>& overlays, int tileWidth, int tileHeight)
{
	m_CurrentMode = 2;
	m_currentTileIds.clear();
	m_currentOverlayImages.clear();
	m_currentOverlayImages.reserve(overlays.size());
	for (const auto& entry : overlays)
		m_currentOverlayImages.push_back({ entry.first, entry.second });
	m_tile_width = tileWidth;
	m_tile_height = tileHeight;
	m_tilecount = static_cast<int>(m_currentOverlayImages.size());
	UpdateScrollLayout();
}
#endif

int CTileSetBrowserView::GetCurrentItemCount() const
{
	if (m_CurrentMode == 1)
		return static_cast<int>(m_currentTileIds.size());
	if (m_CurrentMode == 2)
		return static_cast<int>(m_currentOverlayImages.size());
	return 0;
}

TileSetBrowserLayout CTileSetBrowserView::ComputeLayout(int clientWidth) const
{
	return TileSetBrowserLayout::Compute(clientWidth, GetCurrentItemCount(), m_tile_width, m_tile_height);
}

void CTileSetBrowserView::UpdateScrollLayout()
{
	if (m_updatingScrollLayout)
		return;

	struct ScopedReset
	{
		bool& flag;
		~ScopedReset() { flag = false; }
	} reset{ m_updatingScrollLayout };
	m_updatingScrollLayout = true;

	if (m_hWnd == nullptr)
	{
		const TileSetBrowserLayout layout = ComputeLayout(0);
		m_bottom_needed = max(1, layout.totalHeight);
		return;
	}

	// Two-pass convergence: showing or hiding WS_VSCROLL alters client width by
	// SM_CXVSCROLL, which can change column count and total required height.
	for (int pass = 0; pass < 2; ++pass)
	{
		RECT r{};
		GetClientRect(&r);
		const int clientW = r.right - r.left;
		const int clientH = r.bottom - r.top;
		if (clientW <= 0 || clientH <= 0)
			return;

		const TileSetBrowserLayout layout = ComputeLayout(clientW);
		m_bottom_needed = max(1, layout.totalHeight);
		SetScrollSizes(MM_TEXT, CSize(max(1, clientW), m_bottom_needed));

		RECT after{};
		GetClientRect(&after);
		if ((after.right - after.left) == clientW && (after.bottom - after.top) == clientH)
			break;
	}

	RECT finalRect{};
	GetClientRect(&finalRect);
	const int finalW = finalRect.right - finalRect.left;
	const int finalH = finalRect.bottom - finalRect.top;
	if (finalW > 0 && finalH > 0)
	{
		const int curX = GetScrollPos(SB_HORZ);
		const int curY = GetScrollPos(SB_VERT);
		const int clampedX = TileSetBrowserLayout::ClampScrollPos(curX, max(1, finalW), finalW);
		const int clampedY = TileSetBrowserLayout::ClampScrollPos(curY, m_bottom_needed, finalH);
		if (clampedX != curX || clampedY != curY)
		{
			ScrollToPosition(CPoint(clampedX, clampedY));
			Invalidate(FALSE);
		}
	}
}

TileSetVulkanInitResult CTileSetBrowserView::InitializeVulkanRenderer(int width, int height, std::string& errorOut)
{
#ifdef _DEBUG
	if (m_testHooks != nullptr)
	{
		if (m_testRendererActive)
			return TileSetVulkanInitResult::Succeeded;
		if (!m_testHooks->initialize)
		{
			errorOut = "test hook missing initialize callback";
			return TileSetVulkanInitResult::Failed;
		}
		const TileSetVulkanInitResult result =
			m_testHooks->initialize(width, height, theApp.m_Options.bVSync != FALSE, errorOut);
		if (result != TileSetVulkanInitResult::Failed)
		{
			m_testRendererActive = true;
			if (m_testHooks->recordSceneCommands && m_gpuScene == nullptr)
				m_gpuScene = std::make_unique<VulkanScene>(nullptr);
		}
		return result;
	}

	if (!m_debugFaultInjection.loaded)
	{
		m_debugFaultInjection.loaded = true;
		m_debugFaultInjection.failInitRemaining = ParseDebugFaultCountEnv("FA2_DEBUG_TILESET_VULKAN_FAIL_INIT");
		m_debugFaultInjection.failPresentRemaining = ParseDebugFaultCountEnv("FA2_DEBUG_TILESET_VULKAN_FAIL_PRESENT");
	}
#endif

	if (m_vulkanRenderer != nullptr && m_gpuScene != nullptr)
		return TileSetVulkanInitResult::Succeeded;
	if (m_hWnd == nullptr)
	{
		errorOut = "TileSetBrowserView has no window handle";
		return TileSetVulkanInitResult::Failed;
	}

#ifdef _DEBUG
	if (ConsumeDebugFault(m_debugFaultInjection.failInitRemaining))
	{
		errorOut = "debug fault injection: simulated Vulkan initialization failure";
		return TileSetVulkanInitResult::Failed;
	}
#endif

	if (rs_vulkan_create(m_hWnd, &m_vulkanRenderer) == RS_OK)
	{
		const int prepareStatus = (width <= 0 || height <= 0)
			? RS_VULKAN_MINIMIZED
			: rs_vulkan_prepare(m_vulkanRenderer, width, height, theApp.m_Options.bVSync ? 1 : 0);
		if (prepareStatus == RS_OK)
		{
			m_gpuScene = std::make_unique<VulkanScene>(m_vulkanRenderer);
			return TileSetVulkanInitResult::Succeeded;
		}
		if (prepareStatus == RS_VULKAN_MINIMIZED)
		{
			m_gpuScene = std::make_unique<VulkanScene>(m_vulkanRenderer);
			return TileSetVulkanInitResult::Minimized;
		}
		if (prepareStatus == RS_VULKAN_SWAPCHAIN_OUT_OF_DATE)
		{
			m_gpuScene = std::make_unique<VulkanScene>(m_vulkanRenderer);
			return TileSetVulkanInitResult::SwapchainOutOfDate;
		}
	}

	errorOut = ReadLastVulkanError();
	ReleaseVulkanRenderer();
	return TileSetVulkanInitResult::Failed;
}

VulkanPresentResult CTileSetBrowserView::PresentVulkanScene(
	const RECT& source, int width, int height, bool vsync, std::string& errorOut)
{
#ifdef _DEBUG
	if (m_testHooks != nullptr)
	{
		if (!m_testHooks->present)
		{
			errorOut = "test hook missing present callback";
			return VulkanPresentResult::Failed;
		}
		return m_testHooks->present(source, width, height, vsync, errorOut);
	}

	if (ConsumeDebugFault(m_debugFaultInjection.failPresentRemaining))
	{
		errorOut = "debug fault injection: simulated Vulkan present failure";
		return VulkanPresentResult::Failed;
	}
#endif

	if (m_gpuScene == nullptr)
	{
		errorOut = "Vulkan scene renderer is not initialized";
		return VulkanPresentResult::Failed;
	}

	const VulkanPresentResult result = m_gpuScene->PresentWithResult(source, width, height, vsync);
	if (result == VulkanPresentResult::Failed)
		errorOut = ReadLastVulkanError();
	return result;
}

void CTileSetBrowserView::ReleaseVulkanRenderer()
{
#ifdef _DEBUG
	if (m_testHooks != nullptr)
	{
		m_gpuScene.reset();
		if (m_testRendererActive)
		{
			m_testRendererActive = false;
			if (m_testHooks->release)
				m_testHooks->release();
		}
		return;
	}
#endif

	m_gpuScene.reset();
	if (m_vulkanRenderer != nullptr)
	{
		rs_vulkan_destroy(m_vulkanRenderer);
		m_vulkanRenderer = nullptr;
	}
}

bool CTileSetBrowserView::IsViewVisible() const
{
	return m_hWnd != nullptr && ::IsWindowVisible(m_hWnd) != FALSE;
}

bool CTileSetBrowserView::IsViewMinimizedOrZeroSized(const RECT& clientRect) const
{
	if (clientRect.right <= clientRect.left || clientRect.bottom <= clientRect.top)
		return true;
	if (m_hWnd != nullptr)
	{
		if (::IsIconic(m_hWnd))
			return true;
		HWND root = ::GetAncestor(m_hWnd, GA_ROOT);
		if (root != nullptr && ::IsIconic(root))
			return true;
	}
	return false;
}

bool CTileSetBrowserView::StartRecoveryTimer(UINT_PTR timerId, unsigned int delayMs, UINT_PTR& activeTimerId)
{
	if (m_hWnd == nullptr || timerId == 0)
		return false;
	if (activeTimerId != 0)
	{
		::KillTimer(m_hWnd, activeTimerId);
		activeTimerId = 0;
	}
	const unsigned int effectiveDelay = delayMs > 0 ? delayMs : 1u;
#ifdef _DEBUG
	if (m_testHooks != nullptr && m_testHooks->setTimer && !m_testHooks->setTimer(timerId, effectiveDelay))
		return false;
#endif
	if (::SetTimer(m_hWnd, timerId, effectiveDelay, nullptr) == 0)
		return false;
	activeTimerId = timerId;
	return true;
}

void CTileSetBrowserView::CancelRecoveryTimer(UINT_PTR& activeTimerId)
{
	if (activeTimerId != 0)
	{
		if (m_hWnd != nullptr)
			::KillTimer(m_hWnd, activeTimerId);
		activeTimerId = 0;
	}
}

void CTileSetBrowserView::LogRecoveryMessage(const std::string& message)
{
	if (message.empty())
		return;
#ifdef _DEBUG
	if (m_testHooks != nullptr && m_testHooks->log)
	{
		m_testHooks->log(message);
		return;
	}
#endif
	errstream << message << endl;
	errstream.flush();
}

void CTileSetBrowserView::ApplyRecoveryAction(const TileSetVulkanRecoveryAction& action)
{
	if (action.shouldReleaseRenderer)
		ReleaseVulkanRenderer();

	if (action.cancelRetryTimer)
		CancelRecoveryTimer(m_activeRetryTimerId);

	if (action.cancelTransientTimer)
		CancelRecoveryTimer(m_activeTransientTimerId);

	if (!action.logMessage.empty())
		LogRecoveryMessage(action.logMessage);

	if (action.startRetryTimer)
	{
		if (!StartRecoveryTimer(
			static_cast<UINT_PTR>(action.retryTimerToken),
			action.retryDelayMs,
			m_activeRetryTimerId))
		{
			const TileSetVulkanRecoveryAction fallback = m_vulkanRecovery.OnRetryTimerScheduleFailed();
			if (fallback.shouldReleaseRenderer)
				ReleaseVulkanRenderer();
			if (fallback.cancelRetryTimer)
				CancelRecoveryTimer(m_activeRetryTimerId);
			if (fallback.cancelTransientTimer)
				CancelRecoveryTimer(m_activeTransientTimerId);
			if (!fallback.logMessage.empty())
				LogRecoveryMessage(fallback.logMessage);
			if (fallback.invalidate && m_hWnd != nullptr)
				Invalidate(FALSE);
		}
	}

	if (action.startTransientTimer)
	{
		if (!StartRecoveryTimer(
			static_cast<UINT_PTR>(action.transientTimerToken),
			action.transientDelayMs,
			m_activeTransientTimerId))
		{
			const TileSetVulkanRecoveryAction fallback = m_vulkanRecovery.OnTransientTimerScheduleFailed();
			ApplyRecoveryAction(fallback);
		}
	}

	if (action.invalidate && m_hWnd != nullptr)
		Invalidate(FALSE);
}

void CTileSetBrowserView::RetryVulkanRecovery()
{
	RECT r{};
	if (m_hWnd != nullptr)
		GetClientRect(&r);
	const int width = r.right - r.left;
	const int height = r.bottom - r.top;
	const bool visible = IsViewVisible() && !IsViewMinimizedOrZeroSized(r);
	const TileSetVulkanRecoveryAction action = m_vulkanRecovery.RequestManualRetry(visible, width, height);
	ApplyRecoveryAction(action);
}

void CTileSetBrowserView::NotifyVisibilityChanged(bool visible)
{
	RECT r{};
	if (m_hWnd != nullptr)
		GetClientRect(&r);
	const int width = r.right - r.left;
	const int height = r.bottom - r.top;
	const bool effectivelyVisible = visible && !IsViewMinimizedOrZeroSized(r);
	const TileSetVulkanRecoveryAction action = m_vulkanRecovery.OnShowWindow(effectivelyVisible, width, height);
	ApplyRecoveryAction(action);
}

void CTileSetBrowserView::DrawRecoveryFallback(CDC* pDC, const RECT& clientRect)
{
	if (pDC == nullptr)
		return;

	const int width = clientRect.right - clientRect.left;
	const int height = clientRect.bottom - clientRect.top;
	if (width <= 0 || height <= 0)
	{
		m_retryButtonRect = RECT{};
		return;
	}

	const int savedDC = pDC->SaveDC();
	pDC->SetViewportOrg(0, 0);
	pDC->FillSolidRect(&clientRect, ::GetSysColor(COLOR_BTNFACE));

	CFont* oldFont = pDC->SelectObject(CFont::FromHandle(static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT))));
	pDC->SetBkMode(TRANSPARENT);
	pDC->SetTextColor(::GetSysColor(COLOR_WINDOWTEXT));

	const bool stopped = m_vulkanRecovery.IsStopped();
	const int padX = min(16, max(4, width / 12));
	const int padY = min(16, max(4, height / 12));

	CString bodyText;
	if (stopped)
	{
		bodyText = GetLocalizedTextOrFallback("TileBrowserVulkanFailed", "Tile preview Vulkan rendering failed.");
		bodyText += "\r\n";
		bodyText += GetLocalizedTextOrFallback(
			"TileBrowserVulkanRetryHint",
			"Auto-recovery stopped after repeated failures. Click Retry to try again.");

		const std::string& detail = !m_vulkanRecovery.GetFirstError().empty()
			? m_vulkanRecovery.GetFirstError()
			: m_vulkanRecovery.GetLastError();
		if (!detail.empty())
		{
			std::string brief = detail;
			constexpr size_t kMaxDetailLen = 160;
			if (brief.size() > kMaxDetailLen)
				brief = brief.substr(0, kMaxDetailLen - 3) + "...";
			bodyText += "\r\n(";
			bodyText += brief.c_str();
			bodyText += ")";
		}
	}
	else
	{
		bodyText = GetLocalizedTextOrFallback(
			"TileBrowserVulkanRecovering",
			"Recovering tile preview Vulkan renderer...");
	}

	const int buttonWidth = min(120, max(56, width - padX * 2));
	const int buttonHeight = min(28, max(20, height - 8));
	const int buttonGap = 10;

	RECT measureRect{ padX, 0, max(padX + 1, width - padX), 0 };
	pDC->DrawText(bodyText, &measureRect, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
	const int textHeight = measureRect.bottom - measureRect.top;

	if (stopped)
	{
		const int totalHeight = textHeight + buttonGap + buttonHeight;
		const int topY = max(padY, (height - totalHeight) / 2);
		const int maxButtonTop = max(4, height - buttonHeight - 6);
		const int buttonTop = min(maxButtonTop, topY + textHeight + buttonGap);
		const int buttonLeft = max(4, (width - buttonWidth) / 2);
		m_retryButtonRect = RECT{
			buttonLeft,
			buttonTop,
			min(width - 4, buttonLeft + buttonWidth),
			min(height - 4, buttonTop + buttonHeight)
		};

		const int retryTop = static_cast<int>(m_retryButtonRect.top);
		RECT textRect{
			padX,
			max(4, min(topY, retryTop - buttonGap)),
			max(padX + 1, width - padX),
			max(4, retryTop - 4)
		};
		if (textRect.bottom > textRect.top)
			pDC->DrawText(bodyText, &textRect, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);

		pDC->DrawFrameControl(&m_retryButtonRect, DFC_BUTTON, DFCS_BUTTONPUSH);
		const CString retryCaption = GetLocalizedTextOrFallback("TileBrowserVulkanRetryButton", "Retry");
		RECT captionRect = m_retryButtonRect;
		pDC->DrawText(retryCaption, &captionRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}
	else
	{
		m_retryButtonRect = RECT{};
		const int topY = max(padY, (height - textHeight) / 2);
		RECT textRect{ padX, topY, max(padX + 1, width - padX), max(topY + 1, height - padY) };
		pDC->DrawText(bodyText, &textRect, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
	}

	if (oldFont != nullptr)
		pDC->SelectObject(oldFont);
	pDC->RestoreDC(savedDC);
}

void CTileSetBrowserView::OnDraw(CDC* pDC)
{
	if (theApp.m_pMainWnd != nullptr)
	{
		auto* mainDlg = static_cast<CFinalSunDlg*>(theApp.m_pMainWnd);
		if (mainDlg->m_view.m_isoview != nullptr && mainDlg->m_view.m_isoview->b_IsLoading)
			return;
	}

	RECT r{};
	GetClientRect(&r);
	const int width = r.right - r.left;
	const int height = r.bottom - r.top;
	const bool visible = IsViewVisible();
	const bool minimized = IsViewMinimizedOrZeroSized(r);

	TileSetVulkanRecoveryAction gateAction{};
	if (!m_vulkanRecovery.CanAttemptRender(visible && !minimized, width, height, gateAction))
	{
		ApplyRecoveryAction(gateAction);
		if (visible && !minimized && pDC != nullptr && m_vulkanRecovery.ShouldDrawFallback())
			DrawRecoveryFallback(pDC, r);
		return;
	}

	std::string errorMessage;
	const TileSetVulkanInitResult initResult = InitializeVulkanRenderer(width, height, errorMessage);
	if (initResult != TileSetVulkanInitResult::Succeeded)
	{
		const TileSetVulkanRecoveryAction initAction = m_vulkanRecovery.OnInitializeResult(initResult, errorMessage);
		ApplyRecoveryAction(initAction);
		if (pDC != nullptr && m_vulkanRecovery.ShouldDrawFallback())
			DrawRecoveryFallback(pDC, r);
		return;
	}

	const int scrollX = GetScrollPos(SB_HORZ);
	const int scrollY = GetScrollPos(SB_VERT);
	const RECT listViewport{ scrollX, scrollY, scrollX + width, scrollY + height };
	const RECT viewport{ 0, 0, width, height };

	if (m_gpuScene != nullptr)
	{
		m_gpuScene->Begin(viewport);

		const TileSetBrowserLayout layout = ComputeLayout(width);
		if (layout.rows > 0 && (m_CurrentMode != 1 || (tiledata != NULL && (*tiledata) != NULL)))
		{
			const int max_r = layout.cols;

			int cur_y = 0;
			int cur_x = 0;

			if (m_CurrentMode == 1)
			{
				for (int i = 0; i < m_tilecount; i++)
				{
					const DWORD dwID = m_currentTileIds[i];
					if (cur_y + m_tile_height >= listViewport.top && cur_y <= listViewport.bottom)
					{
						DWORD renderID = dwID;
						if (theApp.m_Options.bMarbleMadness && (*tiledata)[dwID].wMarbleGround != 0xFFFF)
							renderID = (*tiledata)[dwID].wMarbleGround;

						const TILEDATA& td = (*tiledata)[renderID];
						const int added_height = GetAddedHeight(renderID);
						const int curwidth = td.rect.right - td.rect.left;
						const int curheight = added_height + td.rect.bottom - td.rect.top;
						const int local_x = cur_x - scrollX;
						const int local_y = cur_y - scrollY;
						const int base_x = local_x + (m_tile_width - curwidth) / 2;
						const int base_y = local_y + (m_tile_height - curheight) / 2;
						const RECT tileClip{
							max(viewport.left, static_cast<LONG>(base_x)),
							max(viewport.top, static_cast<LONG>(base_y)),
							min(viewport.right, static_cast<LONG>(base_x + curwidth)),
							min(viewport.bottom, static_cast<LONG>(base_y + curheight))
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
							drawer.Rectangle(local_x + 2, local_y + 2, local_x + m_tile_width - 2, local_y + m_tile_height - 2, 0x00ff0000);
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
						if (cur_y + m_tile_height >= listViewport.top && cur_y <= listViewport.bottom)
						{
							const int curwidth = p->wMaxWidth;
							const int curheight = p->wMaxHeight;
							const int local_x = cur_x - scrollX;
							const int local_y = cur_y - scrollY;
							const int drawx = local_x + (m_tile_width - curwidth) / 2;
							const int drawy = local_y + (m_tile_height - curheight) / 2;

							if (p->bType == PICDATA_TYPE_BMP && p->bitmap)
							{
								m_gpuScene->Bitmap(*p->bitmap, drawx, drawy);
							}
							else
							{
								m_gpuScene->Indexed(static_cast<const BYTE*>(p->pic), curwidth, curheight,
									reinterpret_cast<const short*>(p->vborder),
									p->lighting && !p->lighting->empty() ? p->lighting->data() : nullptr,
									p->pal ? p->pal : iPalIso, nullptr, false, drawx, drawy, viewport);
							}

							if (AD.mode == ACTIONMODE_PLACE && AD.data2 == image.overlayType && AD.data3 == image.imageIndex && AD.data == 33 && AD.type == 6)
							{
								LineDrawer drawer(*m_gpuScene);
								drawer.Rectangle(local_x + 2, local_y + 2, local_x + m_tile_width - 2, local_y + m_tile_height - 2, 0x00ff0000);
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
	}

	errorMessage.clear();
	const VulkanPresentResult presentResult =
		PresentVulkanScene(viewport, width, height, theApp.m_Options.bVSync != FALSE, errorMessage);
	const TileSetVulkanRecoveryAction presentAction = m_vulkanRecovery.OnPresentResult(presentResult, errorMessage);
	ApplyRecoveryAction(presentAction);
	if (presentResult == VulkanPresentResult::Succeeded)
	{
		m_retryButtonRect = RECT{};
	}
	else if (pDC != nullptr && m_vulkanRecovery.ShouldDrawFallback())
	{
		DrawRecoveryFallback(pDC, r);
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
		m_tilecount = 0;
		UpdateScrollLayout();
		if (CFrameWnd* frame = GetParentFrame())
			frame->RecalcLayout(TRUE);
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

	UpdateScrollLayout();
	if (CFrameWnd* frame = GetParentFrame())
		frame->RecalcLayout(TRUE);

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
	if (m_vulkanRecovery.IsStopped())
	{
		if (::PtInRect(&m_retryButtonRect, point))
			RetryVulkanRecovery();
		return;
	}
	if (m_vulkanRecovery.IsRecovering())
		return;

	RECT r{};
	GetClientRect(&r);
	const TileSetBrowserLayout layout = ComputeLayout(r.right - r.left);
	if (layout.rows <= 0)
		return;

	SCROLLINFO scrinfo{};
	scrinfo.cbSize = sizeof(SCROLLINFO);
	scrinfo.fMask = SIF_ALL;
	if (GetScrollInfo(SB_HORZ, &scrinfo))
		point.x += scrinfo.nPos;
	if (GetScrollInfo(SB_VERT, &scrinfo))
		point.y += scrinfo.nPos;

	const int hitIndex = layout.HitTest(point.x, point.y);

	if (m_CurrentMode == 1)
	{
		if (hitIndex >= 0 && hitIndex < static_cast<int>(m_currentTileIds.size()))
		{
			const DWORD dwID = m_currentTileIds[hitIndex];
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
			if ((oldmode != ACTIONMODE_SETTILE || (*tiledata)[oldid].wTileSet != selectedTileSet)
				&& theApp.m_pMainWnd != nullptr)
			{
				((CFinalSunDlg*)theApp.m_pMainWnd)->m_settingsbar.m_BrushSize = 0;
				((CFinalSunDlg*)theApp.m_pMainWnd)->m_settingsbar.UpdateData(FALSE);
				((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->m_BrushSize_x = 1;
				((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->m_BrushSize_y = 1;

				for (int i = 0; i < g_data.sections["StdBrushSize"].values.size(); i++)
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
	}
	else if (m_CurrentMode == 2)
	{
		if (hitIndex >= 0 && hitIndex < static_cast<int>(m_currentOverlayImages.size()))
		{
			const OverlayImageRef& image = m_currentOverlayImages[hitIndex];
			PICDATA* p = ovrlpics[image.overlayType][image.imageIndex];
			if (p != NULL && p->pic != NULL)
			{
				AD.mode = ACTIONMODE_PLACE;
				AD.type = 6;
				AD.data = 33;
				AD.data2 = image.overlayType;
				AD.data3 = image.imageIndex;
				RedrawWindow();
				return;
			}
		}
	}


	if (theApp.m_pMainWnd != nullptr)
	{
		auto* mainDlg = static_cast<CFinalSunDlg*>(theApp.m_pMainWnd);
		if (mainDlg->m_view.m_isoview != nullptr)
		{
			mainDlg->m_view.m_isoview->SetForegroundWindow();
			mainDlg->m_view.m_isoview->SetFocus();
		}
	}

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
		m_tilecount = 0;
		UpdateScrollLayout();
		if (CFrameWnd* frame = GetParentFrame())
			frame->RecalcLayout(TRUE);
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

	UpdateScrollLayout();
	if (CFrameWnd* frame = GetParentFrame())
		frame->RecalcLayout(TRUE);
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
	if (m_vulkanRenderer != nullptr || m_vulkanRecovery.ShouldDrawFallback())
		return TRUE;
	return CScrollView::OnEraseBkgnd(pDC);
}

void CTileSetBrowserView::OnSize(UINT nType, int cx, int cy)
{
	CScrollView::OnSize(nType, cx, cy);
	const bool minimized = (nType == SIZE_MINIMIZED) || (cx <= 0) || (cy <= 0);
	if (!minimized)
		UpdateScrollLayout();
	const TileSetVulkanRecoveryAction action = m_vulkanRecovery.OnSize(minimized, cx, cy, IsViewVisible());
	ApplyRecoveryAction(action);
}

void CTileSetBrowserView::OnTimer(UINT_PTR nIDEvent)
{
	const uint64_t token = static_cast<uint64_t>(nIDEvent);
	const bool isRetryToken = TileSetVulkanRecovery::IsRetryTimerToken(token);
	const bool isTransientToken = TileSetVulkanRecovery::IsTransientTimerToken(token);
	if (isRetryToken || isTransientToken)
	{
		if (m_hWnd != nullptr)
			::KillTimer(m_hWnd, nIDEvent);

		if (isRetryToken && nIDEvent == m_activeRetryTimerId)
			m_activeRetryTimerId = 0;
		else if (isTransientToken && nIDEvent == m_activeTransientTimerId)
			m_activeTransientTimerId = 0;

		RECT r{};
		if (m_hWnd != nullptr)
			GetClientRect(&r);
		const int width = r.right - r.left;
		const int height = r.bottom - r.top;
		const bool visible = IsViewVisible() && !IsViewMinimizedOrZeroSized(r);
		const TileSetVulkanRecoveryAction action = isRetryToken
			? m_vulkanRecovery.OnRetryTimer(token, visible, width, height)
			: m_vulkanRecovery.OnTransientSwapchainTimer(token, visible, width, height);
		ApplyRecoveryAction(action);
		return;
	}
	CScrollView::OnTimer(nIDEvent);
}

void CTileSetBrowserView::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CScrollView::OnShowWindow(bShow, nStatus);
	NotifyVisibilityChanged(bShow != FALSE);
}

BOOL CTileSetBrowserView::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (nHitTest == HTCLIENT && m_vulkanRecovery.IsStopped() && !::IsRectEmpty(&m_retryButtonRect))
	{
		CPoint pt;
		if (::GetCursorPos(&pt))
		{
			ScreenToClient(&pt);
			if (::PtInRect(&m_retryButtonRect, pt))
			{
				::SetCursor(::LoadCursor(NULL, IDC_HAND));
				return TRUE;
			}
		}
	}
	return CScrollView::OnSetCursor(pWnd, nHitTest, message);
}

void CTileSetBrowserView::OnDestroy()
{
	const TileSetVulkanRecoveryAction action = m_vulkanRecovery.OnDestroy();
	ApplyRecoveryAction(action);
	CScrollView::OnDestroy();
}
