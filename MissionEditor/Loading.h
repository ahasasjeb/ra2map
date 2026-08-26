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

#if !defined(AFX_LOADING_H__5D5C3284_8962_11D3_B63B_AAA51FD322E3__INCLUDED_)
#define AFX_LOADING_H__5D5C3284_8962_11D3_B63B_AAA51FD322E3__INCLUDED_

#include "FinalSunDlg.h"	
#include "MissionEditorPackLib.h"
#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// Loading.h : header file
//

class VoxelNormalTables;

class MixHandle
{
public:
	MixHandle() = default;
	MixHandle(HMIXFILE handle) noexcept : m_handle(handle) {}
	~MixHandle()
	{
		reset();
	}

	MixHandle(const MixHandle&) = delete;
	MixHandle& operator=(const MixHandle&) = delete;

	MixHandle(MixHandle&& other) noexcept : m_handle(std::exchange(other.m_handle, 0)) {}
	MixHandle& operator=(MixHandle&& other) noexcept
	{
		if (this != &other)
			reset(std::exchange(other.m_handle, 0));
		return *this;
	}

	MixHandle& operator=(HMIXFILE handle) noexcept
	{
		reset(handle);
		return *this;
	}

	operator HMIXFILE() const noexcept
	{
		return m_handle;
	}

	void reset(HMIXFILE handle = 0) noexcept
	{
		if (m_handle != 0 && m_handle != handle)
			FSunPackLib::XCC_CloseMix(m_handle);
		m_handle = handle;
	}

private:
	HMIXFILE m_handle = 0;
};

struct EXPANDMIX
{
	MixHandle hExpand; // 0 if expansion mix does not exist
	MixHandle hECache; // 0 if no ECache
	MixHandle hConquer; // 0 if no Conquer
	MixHandle hLocal;
	MixHandle hIsoSnow; // 0 if no IsoSnow
	MixHandle hIsoTemp; // 0 if no IsoTemp
	MixHandle hIsoUrb;
	MixHandle hIsoGen;
	MixHandle hIsoLun;
	MixHandle hIsoDes;
	MixHandle hIsoUbn;
	MixHandle hIsoGenMd;
	MixHandle hIsoLunMd;
	MixHandle hIsoDesMd;
	MixHandle hIsoUbnMd;
	MixHandle hTemperat; // 0 if no Temperat
	MixHandle hSnow;
	MixHandle hUrban;
	MixHandle hLunar;
	MixHandle hUrbanN;
	MixHandle hDesert;
	MixHandle hGeneric;
	MixHandle hTem;
	MixHandle hSno;
	MixHandle hUrb;
	MixHandle hLun;
	MixHandle hDes;
	MixHandle hUbn;
	MixHandle hBuildings;
	MixHandle hMarble;

	void reset() noexcept
	{
		hMarble.reset();
		hBuildings.reset();
		hUbn.reset();
		hDes.reset();
		hLun.reset();
		hUrb.reset();
		hSno.reset();
		hTem.reset();
		hGeneric.reset();
		hDesert.reset();
		hUrbanN.reset();
		hLunar.reset();
		hUrban.reset();
		hSnow.reset();
		hTemperat.reset();
		hIsoUbnMd.reset();
		hIsoDesMd.reset();
		hIsoLunMd.reset();
		hIsoGenMd.reset();
		hIsoUbn.reset();
		hIsoDes.reset();
		hIsoLun.reset();
		hIsoGen.reset();
		hIsoUrb.reset();
		hIsoTemp.reset();
		hIsoSnow.reset();
		hLocal.reset();
		hConquer.reset();
		hECache.reset();
		hExpand.reset();
	}
};

class CFinalSunDlg;


/////////////////////////////////////////////////////////////////////////////
// dialog field CLoading 


struct FindShpResult
{
	FindShpResult(HMIXFILE mixfile_, TheaterChar mixfile_theater_, CString filename_, TheaterChar theat_, HTSPALETTE palette_): mixfile(mixfile_), mixfile_theater(mixfile_theater_), filename(filename_), theat(theat_), palette(palette_) { }
	HMIXFILE mixfile;
	TheaterChar mixfile_theater;
	CString filename;
	TheaterChar theat;
	HTSPALETTE palette;
};

class CLoading : public CDialog
{
// Construction
public:
	void CreateConvTable(RGBTRIPLE* pal, int* iPal);
	void FetchPalettes();
	void PrepareUnitGraphic(LPCSTR lpUnittype);
	void LoadStrings();
	void FreeAll();
	void FreeTileSet();
	BOOL InitDirectDraw();
	
	void InitTMPs(CProgressCtrl* prog=NULL);
	void InitPalettes();
	
	~CLoading();
	void Unload();
	BOOL InitMixFiles();
	void InitSHPs(CProgressCtrl* prog=NULL);
	void LoadTSIni(LPCTSTR lpFilename, CIniFile* lpIniFile, BOOL bIsExpansion, BOOL bCheckEditorDir = FALSE);
	void CreateINI();
	CLoading(CWnd* pParent = NULL);   // Standardconstructor
	void InitPics(CProgressCtrl* prog=NULL);
	void Load();
	BOOL LoadUnitGraphic(LPCTSTR lpUnittype);
	void LoadBuildingSubGraphic(const CString& subkey, const CIniFileSection& artSection,
		char theat, SHPHEADER& shp_h, BYTE*& shp);
	void LoadOverlayGraphic(LPCTSTR lpOvrlName, int iOvrlNum);
	void InitVoxelNormalTables();
	HTSPALETTE GetIsoPalette(char theat);
	HTSPALETTE GetUnitPalette(char theat);
	std::optional<FindShpResult> FindUnitShp(const CString& image, char preferred_theat, const CIniFileSection& artSection);
	char cur_theat;
	

// Dialog data
	//{{AFX_DATA(CLoading)
	enum { IDD = IDD_LOADING };
	CStatic	m_Version;
	CStatic	m_BuiltBy;
	CStatic	m_cap;
	CProgressCtrl	m_progress;
	//}}AFX_DATA


// Overwriteables
	// class wizard generated overwriteables
	//{{AFX_VIRTUAL(CLoading)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV-support
	virtual void PostNcDestroy();
	//}}AFX_VIRTUAL

// Implementation
protected:

	// generated message handlers
	//{{AFX_MSG(CLoading)
	virtual BOOL OnInitDialog();
	afx_msg void OnDestroy();
	afx_msg void OnPaint();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
private:
	void HackRules();
	void PrepareHouses(void);
	void CalcPicCount();
	int m_pic_count;
	int m_bmp_count;
	BOOL LoadTile(LPCSTR lpFilename, HMIXFILE hOwner, HTSPALETTE hPalette, DWORD dwID, BOOL bReplacement);
	HTSPALETTE m_hPalIsoTemp;
	HTSPALETTE m_hPalIsoSnow;
	HTSPALETTE m_hPalIsoUrb;
	
	HTSPALETTE m_hPalUnitTemp;
	HTSPALETTE m_hPalUnitSnow;
	HTSPALETTE m_hPalUnitUrb;
	HTSPALETTE m_hPalTemp;
	HTSPALETTE m_hPalSnow;
	HTSPALETTE m_hPalUrb;
	HTSPALETTE m_hPalLib;
	// YR pals:
	HTSPALETTE m_hPalLun;
	HTSPALETTE m_hPalDes;
	HTSPALETTE m_hPalUbn;
	HTSPALETTE m_hPalIsoLun;
	HTSPALETTE m_hPalIsoDes;
	HTSPALETTE m_hPalIsoUbn;
	HTSPALETTE m_hPalUnitLun;
	HTSPALETTE m_hPalUnitDes;
	HTSPALETTE m_hPalUnitUbn;

	HMIXFILE FindFileInMix(LPCTSTR lpFilename, TheaterChar* pTheaterChar=NULL);
	MixHandle m_hLocal;
	MixHandle m_hSno;
	MixHandle m_hTem;
	MixHandle m_hUrb;
	MixHandle m_hLun;
	MixHandle m_hDes;
	MixHandle m_hUbn;
	MixHandle m_hTibSun;
	MixHandle m_hBuildings;
	std::array<EXPANDMIX, 101> m_hExpand; // 1 added for ra2md.mix
	std::array<MixHandle, 100> m_hECache;
	std::vector<MixHandle> m_hExtraMixes;
	MixHandle m_hIsoSnow;
	MixHandle m_hIsoTemp;
	MixHandle m_hIsoUrb;
	MixHandle m_hIsoGen;
	MixHandle m_hIsoLun;
	MixHandle m_hIsoDes;
	MixHandle m_hIsoUbn;
	MixHandle m_hTemperat;
	MixHandle m_hSnow;
	MixHandle m_hUrban;
	MixHandle m_hUrbanN;
	MixHandle m_hLunar;
	MixHandle m_hDesert;
	MixHandle m_hCache;
	MixHandle m_hConquer;
	MixHandle m_hLanguage;
	MixHandle m_hLangMD;
	MixHandle m_hMarble;
	BOOL loaded;

	std::unique_ptr<VoxelNormalTables> m_voxelNormalTables;
	
	
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ fügt unmittelbar vor der vorhergehenden Zeile zusätzliche Deklarationen ein.

#endif // AFX_LOADING_H__5D5C3284_8962_11D3_B63B_AAA51FD322E3__INCLUDED_
