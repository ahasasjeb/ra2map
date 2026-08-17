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

// ViewObjects.cpp: Implementierungsdatei
//

#include "stdafx.h"
#include "FinalSun.h"
#include "ViewObjects.h"
#include "FinalSunDlg.h"
#include "structs.h"
#include "mapdata.h"
#include "variables.h"
#include "functions.h"
#include "inlines.h"
#include "rtpdlg.h"
#include "TubeTool.h"
#include <set>
#include <vector>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CViewObjects

const int valadded=10000;

IMPLEMENT_DYNCREATE(CViewObjects, CTreeView)

CViewObjects::CViewObjects()
{
	m_ready=FALSE;

}

CViewObjects::~CViewObjects()
{
}


BEGIN_MESSAGE_MAP(CViewObjects, CTreeView)
	//{{AFX_MSG_MAP(CViewObjects)
	ON_NOTIFY_REFLECT(TVN_SELCHANGED, OnSelchanged)
	ON_WM_CREATE()
	ON_NOTIFY_REFLECT(TVN_KEYDOWN, OnKeydown)
	ON_WM_KEYDOWN()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()


extern int overlay_number[];
extern CString overlay_name[];
extern BOOL overlay_visible[];
extern BOOL overlay_trail[];


extern int overlay_count;


extern ACTIONDATA AD;

/////////////////////////////////////////////////////////////////////////////
// Zeichnung CViewObjects 

void CViewObjects::OnDraw(CDC* pDC)
{
	CDocument* pDoc = GetDocument();
	// ZU ERLEDIGEN: Code zum Zeichnen hier einfügen
}

/////////////////////////////////////////////////////////////////////////////
// Diagnose CViewObjects

#ifdef _DEBUG
void CViewObjects::AssertValid() const
{
	CTreeView::AssertValid();
}

void CViewObjects::Dump(CDumpContext& dc) const
{
	CTreeView::Dump(dc);
}
#endif //_DEBUG

CString GetTheaterLanguageString(LPCSTR lpString)
{
	CString s=lpString;
	CString t=lpString;

	if((tiledata)==&t_tiledata) t+="TEM";
	if((tiledata)==&s_tiledata) t+="SNO";
	if((tiledata)==&u_tiledata) t+="URB";
	if((tiledata)==&un_tiledata) t+="UBN";
	if((tiledata)==&l_tiledata) t+="LUN";
	if((tiledata)==&d_tiledata) t+="DES";

	CString res=GetLanguageStringACP(t);
	if(res.GetLength()==0) res=GetLanguageStringACP(s);

	return res;
}

/////////////////////////////////////////////////////////////////////////////
// Behandlungsroutinen für Nachrichten CViewObjects 


void CViewObjects::OnSelchanged(NMHDR* pNMHDR, LRESULT* pResult) 
{
	CIniFile& ini=Map->GetIniFile();
	
	NM_TREEVIEW* pNMTreeView = (NM_TREEVIEW*)pNMHDR;

	int val=pNMTreeView->itemNew.lParam;
	if(val<0){ // return;
		if(val==-2) { 
			AD.reset();
			((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW); 
		}
		return;
	}

	if(val<valadded)
	{
		// standard selection (maybe erasing etc)
		switch(val)
		{
			case 10: // erase field
			{
				AD.mode=ACTIONMODE_ERASEFIELD;
				break;
			}
			case 20: // waypoint stuff now
			{
				AD.mode=ACTIONMODE_WAYPOINT;
				AD.type=0;
				break;
			}
			case 21:
			{
				AD.mode=3;
				AD.type=1;
				break;
			}
			case 22:
			{
				AD.mode=3;
				AD.type=2;
				break;
			}
			case 23:
			case 24:
			case 25:
			case 26:
			case 27:
			case 28:
			case 29:
			case 30:
			{
				AD.mode=3;
				AD.type=3+val-23;
				break;
			}
			case 36: // celltag stuff
			{
				AD.mode=4;
				AD.type=0;
				break;
			}
			case 37: 
			{
				AD.mode=4;
				AD.type=1;
				break;
			}
			case 38: 
			{
				AD.mode=4;
				AD.type=2;
				break;
			}
			case 40: // node stuff
			{
				AD.mode=5;
				AD.type=0;
				break;
			}
			case 41: 
			{
				AD.mode=5;
				AD.type=1;
				break;
			}
			case 42:
			{
				AD.mode=5;
				AD.type=2;
				break;
			}
			case 50:
			{
				AD.mode=ACTIONMODE_MAPTOOL;
				AD.tool.reset(new AddTubeTool(*Map, *((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview, true));
				break;
			}
			case 51:
			{
				AD.mode = ACTIONMODE_MAPTOOL;
				AD.tool.reset(new ModifyTubeTool(*Map, *((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview, true));
				break;
			}
			case 52:
			{
				AD.mode = ACTIONMODE_MAPTOOL;
				AD.tool.reset(new AddTubeTool(*Map, *((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview, false));
				break;
			}
			case 53:
			{
				AD.mode = ACTIONMODE_MAPTOOL;
				AD.tool.reset(new ModifyTubeTool(*Map, *((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview, false));
				break;
			}
			case 54:
			{
				AD.mode=ACTIONMODE_MAPTOOL;
				AD.tool.reset(new RemoveTubeTool(*Map, *((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview));				
				break;
			}

			case 61:
				if(!tiledata_count) break;
				AD.type=0;
				AD.mode=ACTIONMODE_SETTILE;
				AD.data=0;
				AD.z_data=0;
				HandleBrushSize(0);
				break;
			
			case 62:
				int i;
				if(!tiledata_count) break;
				for(i=0;i<(*tiledata_count);i++)
					if((*tiledata)[i].wTileSet==atoi((*tiles).sections["General"].values["SandTile"])) break;
				AD.type=i;
				AD.mode=ACTIONMODE_SETTILE;
				AD.data=0;
				AD.z_data=0;
				HandleBrushSize(i);
				break;
			case 63:
				if(!tiledata_count) break;
				for(i=0;i<(*tiledata_count);i++)
					if((*tiledata)[i].wTileSet==atoi((*tiles).sections["General"].values["RoughTile"])) break;
				AD.type=i;
				AD.mode=ACTIONMODE_SETTILE;
				AD.data=0;
				AD.z_data=0;
				HandleBrushSize(i);
				break;
			case 64:
				if(!tiledata_count) break;
				for(i=0;i<(*tiledata_count);i++)
					if((*tiledata)[i].wTileSet==waterset) break;

				if(((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->m_BrushSize_x<2 ||
				((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->m_BrushSize_y<2)
				{

					((CFinalSunDlg*)theApp.m_pMainWnd)->m_settingsbar.m_BrushSize=1;
					((CFinalSunDlg*)theApp.m_pMainWnd)->m_settingsbar.UpdateData(FALSE);
					((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->m_BrushSize_x=2;
					((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->m_BrushSize_y=2;
				}

				AD.type=i;			
				AD.mode=ACTIONMODE_SETTILE;
				AD.data=1;	 // use water placement logic
				AD.z_data=0;
				break;
			case 65:
				if(!tiledata_count) break;
				for(i=0;i<(*tiledata_count);i++)
					if((*tiledata)[i].wTileSet==atoi((*tiles).sections["General"].values["GreenTile"])) break;
				AD.type=i;
				AD.mode=ACTIONMODE_SETTILE;
				AD.data=0;
				AD.z_data=0;
				HandleBrushSize(i);
				break;
			case 66:
				if(!tiledata_count) break;
				for(i=0;i<(*tiledata_count);i++)
					if((*tiledata)[i].wTileSet==atoi((*tiles).sections["General"].values["PaveTile"])) break;
				AD.type=i;
				AD.mode=ACTIONMODE_SETTILE;
				AD.data=0;
				AD.z_data=0;
				HandleBrushSize(i);
				break;
			case 67:
				if(!tiledata_count) break;
				for(i=0;i<(*tiledata_count);i++)
					if((*tiledata)[i].wTileSet==atoi(g_data.sections["NewUrbanInfo"].values["Morphable2"])) break;
				AD.type=i;
				AD.mode=ACTIONMODE_SETTILE;
				AD.data=0;
				AD.z_data=0;
				HandleBrushSize(i);
				break;
				

		}
	}
	else
	{
		int subpos=val%valadded;
		int pos=val/valadded;

		AD.mode=1;
		AD.type=pos;
		AD.data=subpos;

		if(pos==1)
		{
			CString sec="InfantryTypes";
			
			if(subpos<rules.sections[sec].values.size())
			{
				// standard unit!

				AD.data_s=*rules.sections[sec].GetValue(subpos);
			}
			else{

				AD.data_s=*ini.sections[sec].GetValue(subpos-rules.sections[sec].values.size());
			}
		}
		else if(pos==2)
		{
			CString sec="BuildingTypes";
			
			if(subpos<rules.sections[sec].values.size())
			{
				// standard unit!

				AD.data_s=*rules.sections[sec].GetValue(subpos);
			}
			else{

				AD.data_s=*ini.sections[sec].GetValue(subpos-rules.sections[sec].values.size());
			}
		}
		else if(pos==3)
		{
			CString sec="AircraftTypes";
			
			if(subpos<rules.sections[sec].values.size())
			{
				// standard unit!

				AD.data_s=*rules.sections[sec].GetValue(subpos);
			}
			else{

				AD.data_s=*ini.sections[sec].GetValue(subpos-rules.sections[sec].values.size());
			}
		}
		else if(pos==4)
		{
			CString sec="VehicleTypes";
			
			if(subpos<rules.sections[sec].values.size())
			{
				// standard unit!

				AD.data_s=*rules.sections[sec].GetValue(subpos);
			}
			else{

				AD.data_s=*ini.sections[sec].GetValue(subpos-rules.sections[sec].values.size());
			}
		}
		else if(pos==5)
		{
			
			CString sec="TerrainTypes";

			if(subpos==999)
			{
				
				CRTPDlg dlg;
				if(dlg.DoModal()==IDOK)
				{
					AD.mode=ACTIONMODE_RANDOMTERRAIN;					
				}
			}
			else
			{
				if(subpos<rules.sections[sec].values.size())
				{
					// standard unit!

					AD.data_s=*rules.sections[sec].GetValue(subpos);
				}
				else{

					AD.data_s=*ini.sections[sec].GetValue(subpos-rules.sections[sec].values.size());
				}
			}
		}
		else if(pos==6)
		{
			if(subpos<100)
			{
				// general overlay functions!
				if(subpos==1)
				{
					AD.data=31;
					AD.data2=atoi(InputBox("Please enter the value (0-255) of the overlay. Don´t exceed this range.","Set overlay manually"));

				}
				else if(subpos==2)
				{
					AD.data=32;
					AD.data2=atoi(InputBox("Please enter the value (0-255) of the overlay-data. Don´t exceed this range.","Set overlay manually"));

				}
				
			}
			else
			{
				AD.data2=subpos%100;
				AD.data=subpos/100;
				if(AD.data>=30) {AD.data=30;AD.data2=subpos%1000;}
			}
		}
		else if(pos==7)
		{
			// set owner
			//if(ini.sections.find(MAPHOUSES)!=ini.sections.end() && ini.sections[MAPHOUSES].values.size()>0)
			if(ini.sections.find(MAPHOUSES)!=ini.sections.end() && ini.sections[MAPHOUSES].values.size()>0)
			{
				AD.data_s=*ini.sections[MAPHOUSES].GetValue(subpos);
			}
			else
			{
				AD.data_s=*rules.sections[HOUSES].GetValue(subpos);
			}

			currentOwner=AD.data_s;
		}
#ifdef SMUDGE_SUPP
		else if(pos==8)
		{
			
			
			CString sec="SmudgeTypes";

			if(subpos<rules.sections[sec].values.size())
			{
				// standard unit!

				AD.data_s=*rules.sections[sec].GetValue(subpos);
			}
			else
			{
				AD.data_s=*ini.sections[sec].GetValue(subpos-rules.sections[sec].values.size());
			}

		}
#endif


	}

	
	
	*pResult = 0;
}

__inline HTREEITEM TV_InsertItemW(HWND hWnd, WCHAR* lpString, int len, HTREEITEM hInsertAfter, HTREEITEM hParent, int param)
{
	if(!lpString) return NULL;

	TVINSERTSTRUCTW tvis;
	tvis.hInsertAfter=hInsertAfter;
	tvis.hParent=hParent;
	tvis.itemex.mask=TVIF_PARAM | TVIF_TEXT;
	tvis.itemex.cchTextMax=len;
	tvis.itemex.pszText=lpString;
	tvis.itemex.lParam=param;

	// MW 07/17/2001: Updated to use Ascii if Unicode fails:
	HTREEITEM res=(HTREEITEM)::SendMessage(hWnd, TVM_INSERTITEMW, 0,((LPARAM)(&tvis)));	

	if(!res)
	{
		// failed... Probably because of missing Unicode support

		// convert text to ascii, then add it
		BYTE* lpAscii=new(BYTE[len+1]);
		BOOL bUsedDefault;
		memset(lpAscii, 0, len+1);
		WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK,
			lpString, len+1, (LPSTR)lpAscii, len+1,NULL,&bUsedDefault);

		TVINSERTSTRUCT tvis;
		tvis.hInsertAfter=hInsertAfter;
		tvis.hParent=hParent;
		tvis.itemex.mask=TVIF_PARAM | TVIF_TEXT;
		tvis.itemex.cchTextMax=len;
		tvis.itemex.lParam=param;
		tvis.itemex.pszText=(char*)lpAscii;

		res=TreeView_InsertItem(hWnd, &tvis);

		delete[] lpAscii;
	}

	return res;
}

namespace
{
	struct SideTreeNodes
	{
		std::map<int, HTREEITEM> bySideIndex;
		HTREEITEM other = NULL;
	};

	bool TryParseSideIndex(CString text, int& result)
	{
		text.Trim();
		if(text.IsEmpty())
			return false;

		char* end = NULL;
		const long parsed = strtol(text, &end, 10);
		if(end == (LPCSTR)text || *end != '\0' || parsed < 0 || parsed > 63)
			return false;

		result = static_cast<int>(parsed);
		return true;
	}

	CString NormalizeSideLabelEncoding(const CString& text)
	{
		const char* bytes = text;
		const int byteCount = text.GetLength();
		if(byteCount == 0 || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
			bytes, byteCount, NULL, 0) > 0)
			return text;

		// The Chinese Mental Omega FAData.ini shipped with FA2Ext uses CP936,
		// while this editor runs its narrow Windows APIs in UTF-8 mode.
		const int wideCount = MultiByteToWideChar(936, 0, bytes, byteCount, NULL, 0);
		if(wideCount <= 0)
			return text;

		std::wstring wide(wideCount, L'\0');
		if(MultiByteToWideChar(936, 0, bytes, byteCount, wide.data(), wideCount) == 0)
			return text;

		const int utf8Count = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideCount,
			NULL, 0, NULL, NULL);
		if(utf8Count <= 0)
			return text;

		std::string utf8(utf8Count, '\0');
		if(WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideCount,
			utf8.data(), utf8Count, NULL, NULL) == 0)
			return text;
		return CString(utf8.c_str());
	}

	SideTreeNodes CreateSideTreeNodes(CTreeCtrl& tree, HTREEITEM root)
	{
		SideTreeNodes result;
		std::map<int, CString> labels;

		for(const auto& sideEntry : sides)
		{
			const SIDEINFO& side = sideEntry.second;
			if(labels.find(side.orig_n) == labels.end())
				labels[side.orig_n] = side.name;
		}

		// FA2Ext treats FAData.ini [Sides] as display metadata. The key is the
		// side index, the first value is the tree caption and the optional
		// second value marks a Yuri/expansion-only entry. It is not an Owner
		// list and therefore must not be appended to the global `sides` table.
		if(const CIniFileSection* configuredSides = g_data.GetSection("Sides"))
		{
			for(const auto& configuredSide : configuredSides->values)
			{
				int sideIndex = -1;
				if(!TryParseSideIndex(configuredSide.first, sideIndex))
					continue;

				const bool expansionOnly = atoi(GetParam(configuredSide.second, 1)) != 0;
				if(expansionOnly && !yuri_mode)
					continue;

				CString label = NormalizeSideLabelEncoding(GetParam(configuredSide.second, 0));
				label.Trim();
				if(!label.IsEmpty())
					labels[sideIndex] = label;
			}
		}

#ifdef RA2_MODE
		if(labels.find(0) != labels.end()) labels[0] = GetLanguageStringACP("Allied");
		if(labels.find(1) != labels.end()) labels[1] = GetLanguageStringACP("Soviet");
		if(labels.find(2) != labels.end()) labels[2] = GetLanguageStringACP("Yuri");
#endif

		for(const auto& label : labels)
		{
			result.bySideIndex[label.first] = tree.InsertItem(
				TVIF_PARAM | TVIF_TEXT, label.second, 0, 0, 0, 0, -1, root, TVI_LAST);
		}
		result.other = tree.InsertItem(TVIF_PARAM | TVIF_TEXT,
			GetLanguageStringACP("Other"), 0, 0, 0, 0, -1, root, TVI_LAST);
		return result;
	}

	bool OwnerContainsSide(const CString& ownerList, const CString& sideName)
	{
		for(int i = 0; ; i++)
		{
			CString owner = GetParam(ownerList, i);
			owner.Trim();
			if(owner.IsEmpty())
				break;
			if(owner.CompareNoCase(sideName) == 0)
				return true;
		}
		return false;
	}

	std::vector<HTREEITEM> FindSideParents(const SideTreeNodes& nodes, const CString& owners, int planningSide)
	{
		std::vector<HTREEITEM> parents;
		std::set<int> addedSideIndices;

		for(const auto& sideEntry : sides)
		{
			const SIDEINFO& side = sideEntry.second;
			if(planningSide >= 0 && planningSide != side.orig_n)
				continue;

			const auto node = nodes.bySideIndex.find(side.orig_n);
			if(node == nodes.bySideIndex.end() || addedSideIndices.find(side.orig_n) != addedSideIndices.end())
				continue;

			if(OwnerContainsSide(owners, side.name) ||
				(side.orig_n == 2 && owners.CompareNoCase("YuriCountry") == 0))
			{
				parents.push_back(node->second);
				addedSideIndices.insert(side.orig_n);
			}
		}

		if(parents.empty())
			parents.push_back(nodes.other);
		return parents;
	}

	CString GetObjectOwner(const CIniFile& mapIni, const CString& type)
	{
		CString owner;
		if(const CIniFileSection* section = rules.GetSection(type))
			owner = section->GetValueByName("Owner");
		if(const CIniFileSection* section = mapIni.GetSection(type))
		{
			const CString mapOwner = section->GetValueByName("Owner");
			if(!mapOwner.IsEmpty())
				owner = mapOwner;
		}
		return owner;
	}

	int GetObjectPlanningSide(const CIniFile& mapIni, const CString& type)
	{
		CString value;
		if(const CIniFileSection* section = rules.GetSection(type))
			value = section->GetValueByName("AIBasePlanningSide");
		if(const CIniFileSection* section = g_data.GetSection(type))
		{
			const CString configuredValue = section->GetValueByName("AIBasePlanningSide");
			if(!configuredValue.IsEmpty())
				value = configuredValue;
		}
		if(const CIniFileSection* section = mapIni.GetSection(type))
		{
			const CString mapValue = section->GetValueByName("AIBasePlanningSide");
			if(!mapValue.IsEmpty())
				value = mapValue;
		}
		return value.IsEmpty() ? -1 : atoi(value);
	}

	void InsertSideObject(CTreeCtrl& tree, const SideTreeNodes& nodes, WCHAR* text,
		int param, const CString& owners, int planningSide)
	{
		if(!text)
			return;
		for(HTREEITEM parent : FindSideParents(nodes, owners, planningSide))
			TV_InsertItemW(tree.m_hWnd, text, wcslen(text), TVI_LAST, parent, param);
	}

	void InsertSideObject(CTreeCtrl& tree, const SideTreeNodes& nodes, const CString& text,
		int param, const CString& owners, int planningSide)
	{
		for(HTREEITEM parent : FindSideParents(nodes, owners, planningSide))
			tree.InsertItem(TVIF_PARAM | TVIF_TEXT, text, 0, 0, 0, 0, param, parent, TVI_LAST);
	}
}

void CViewObjects::UpdateDialog()
{
	OutputDebugString("Objectbrowser redrawn\n");

	CTreeCtrl& tree=GetTreeCtrl();
	CIniFile& ini=Map->GetIniFile();
	
	tree.Select(0,TVGN_CARET   );
	tree.DeleteAllItems();
	
	CString sTreeRoots[15];
	sTreeRoots[0]=GetLanguageStringACP("InfantryObList");
	sTreeRoots[1]=GetLanguageStringACP("VehiclesObList");
	sTreeRoots[2]=GetLanguageStringACP("AircraftObList");
	sTreeRoots[3]=GetLanguageStringACP("StructuresObList");
	sTreeRoots[4]=GetLanguageStringACP("TerrainObList");
	sTreeRoots[5]=GetLanguageStringACP("OverlayObList");
	sTreeRoots[6]=GetLanguageStringACP("WaypointsObList");
	sTreeRoots[7]=GetLanguageStringACP("CelltagsObList");
	sTreeRoots[8]=GetLanguageStringACP("BaseNodesObList");
	sTreeRoots[9]=GetLanguageStringACP("TunnelObList");
	sTreeRoots[10]=GetLanguageStringACP("DelObjObList");
	sTreeRoots[11]=GetLanguageStringACP("ChangeOwnerObList");
	sTreeRoots[12]=GetLanguageStringACP("StartpointsObList");
	sTreeRoots[13]=GetLanguageStringACP("GroundObList");
	sTreeRoots[14]=GetLanguageStringACP("SmudgesObList");

	int i=0;

	//TV_InsertItemW(tree.m_hWnd, L"HELLO", 5, TVI_LAST, TVI_ROOT, -2);
	
	HTREEITEM first=tree.InsertItem(TVIF_PARAM | TVIF_TEXT,
		TranslateStringACP(GetLanguageStringACP("NothingObList")), i, i, 0, 0, -2, TVI_ROOT, TVI_LAST);

	HTREEITEM rootitems[15];

	// we want the change owner at the top
	
	if(!Map->IsMultiplayer() || !theApp.m_Options.bEasy)
		rootitems[11]=tree.InsertItem(TVIF_PARAM | TVIF_TEXT,
			TranslateStringACP(sTreeRoots[11]), i, i, 0, 0, i, TVI_ROOT, TVI_LAST);
	

	for(i=0;i<10;i++)
	{
		BOOL bAllow=TRUE;
		if(theApp.m_Options.bEasy)
		{
			if(i>=6 && i<=9)
				bAllow=FALSE;
		}

		// no tunnels in ra2 mode
		if(editor_mode==ra2_mode && i==9 && !isTrue(g_data.sections["Debug"].values["AllowTunnels"])) bAllow=FALSE;

		if(bAllow)
		rootitems[i]=tree.InsertItem(TVIF_PARAM | TVIF_TEXT,
		sTreeRoots[i], i, i, 0, 0, i, TVI_ROOT, TVI_LAST);
	}


	rootitems[13]=tree.InsertItem(TVIF_PARAM | TVIF_TEXT, TranslateStringACP(sTreeRoots[13]), 13, 13, 0, 0, 13, TVI_ROOT, first);

	rootitems[12]=tree.InsertItem(TVIF_PARAM | TVIF_TEXT,
			TranslateStringACP(sTreeRoots[12]), 12,12, 0, 0, 12, TVI_ROOT, TVI_LAST);

	rootitems[10]=tree.InsertItem(TVIF_PARAM | TVIF_TEXT,
			TranslateStringACP(sTreeRoots[10]), 10, 10, 0, 0, 10, TVI_ROOT, TVI_LAST);

#ifdef SMUDGE_SUPP
	rootitems[14]=tree.InsertItem(TVIF_PARAM | TVIF_TEXT,
			TranslateStringACP(sTreeRoots[14]), 14, 14, 0, 0, 10, TVI_ROOT, rootitems[4]);
#endif


	const SideTreeNodes infantrySideNodes = CreateSideTreeNodes(tree, rootitems[0]);
	const SideTreeNodes vehicleSideNodes = CreateSideTreeNodes(tree, rootitems[1]);
	const SideTreeNodes aircraftSideNodes = CreateSideTreeNodes(tree, rootitems[2]);
	const SideTreeNodes buildingSideNodes = CreateSideTreeNodes(tree, rootitems[3]);


	if(!theApp.m_Options.bEasy)
	{
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("CreateWaypObList"), 0,0,0,0, 20, rootitems[6], TVI_LAST );
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("CreateSpecWaypObList"), 0,0,0,0, 22, rootitems[6], TVI_LAST );
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("DelWaypObList"), 0,0,0,0, 21, rootitems[6], TVI_LAST );
	}

		
	int e;
	int max=8;
	//if(ini.sections.find(HOUSES)!=ini.sections.end() && ini.sections.find(MAPHOUSES)!=ini.sections.end())
	if(!Map->IsMultiplayer())
		max=1;
	else
	{
		
	}
	for(e=0;e<max;e++)
	{
		CString ins=GetLanguageStringACP("StartpointsPlayerObList");
		char c[50];
		itoa(e+1,c,10);
		ins=TranslateStringVariables(1, ins, c);
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, ins, 0,0,0,0, 23+e, rootitems[12], TVI_LAST );
	}
	tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("StartpointsDelete"), 0,0,0,0, 21, rootitems[12], TVI_LAST );



	tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetTheaterLanguageString("GroundClearObList"),0,0,0,0,61,rootitems[13], TVI_LAST);
	tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetTheaterLanguageString("GroundSandObList"),0,0,0,0,62,rootitems[13], TVI_LAST);
	tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetTheaterLanguageString("GroundRoughObList"),0,0,0,0,63,rootitems[13], TVI_LAST);
	tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetTheaterLanguageString("GroundGreenObList"),0,0,0,0,65,rootitems[13], TVI_LAST);
	tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetTheaterLanguageString("GroundPaveObList"),0,0,0,0,66,rootitems[13], TVI_LAST);
	tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetTheaterLanguageString("GroundWaterObList"),0,0,0,0,64,rootitems[13], TVI_LAST);
#ifdef RA2_MODE
	if(Map->GetTheater()==THEATER3)
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetTheaterLanguageString("GroundPave2ObList"),0,0,0,0,67,rootitems[13], TVI_LAST);
#endif

	if(!theApp.m_Options.bEasy)
	{
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("CreateCelltagObList"), 0,0,0,0, 36, rootitems[7], TVI_LAST );
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("DelCelltagObList"), 0,0,0,0, 37, rootitems[7], TVI_LAST );
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("CelltagPropObList"), 0,0,0,0, 38, rootitems[7], TVI_LAST );
	}

	if(!theApp.m_Options.bEasy)
	{
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("CreateNodeNoDelObList"), 0,0,0,0, 40, rootitems[8], TVI_LAST );
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("CreateNodeDelObList"), 0,0,0,0, 41, rootitems[8], TVI_LAST );
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("DelNodeObList"), 0,0,0,0, 42, rootitems[8], TVI_LAST );
	}


	HTREEITEM deleteoverlay=tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("DelOvrlObList"), 0,0,0,0, -1, rootitems[5], TVI_LAST );
	HTREEITEM tiberium=tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("GrTibObList"), 0,0,0,0, -1, rootitems[5], TVI_LAST );
	//HTREEITEM bluetiberium=tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("BlTibObList"), 0,0,0,0, -1, rootitems[5], TVI_LAST );
#ifndef RA2_MODE
	HTREEITEM veinhole=tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("VeinholeObList"), 0,0,0,0, -1, rootitems[5], TVI_LAST );
#endif
	HTREEITEM bridges=tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("BridgesObList"), 0,0,0,0, -1, rootitems[5], TVI_LAST );
	HTREEITEM alloverlay=tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("OthObList"), 0,0,0,0, -1, rootitems[5], TVI_LAST );
	HTREEITEM everyoverlay=NULL;
		
	if(!theApp.m_Options.bEasy)
	{
		everyoverlay=tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("AllObList"), 0,0,0,0, -1, rootitems[5], TVI_LAST );
	}


	if(!theApp.m_Options.bEasy)
	{
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("OvrlManuallyObList"), 0,0,0,0, valadded*6+1, rootitems[5], TVI_LAST );
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("OvrlDataManuallyObList"), 0,0,0,0, valadded*6+2, rootitems[5], TVI_LAST );
	}

	tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("DelOvrl0ObList"), 0,0,0,0, valadded*6+100+0, deleteoverlay, TVI_LAST );
	tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("DelOvrl1ObList"), 0,0,0,0, valadded*6+100+1, deleteoverlay, TVI_LAST );
	tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("DelOvrl2ObList"), 0,0,0,0, valadded*6+100+2, deleteoverlay, TVI_LAST );
	tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("DelOvrl3ObList"), 0,0,0,0, valadded*6+100+3, deleteoverlay, TVI_LAST );
	
	//tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("DrawRanTibObList"), 0,0,0,0, valadded*6+200+0, tiberium, TVI_LAST );
	tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("DrawTibObList"), 0,0,0,0, valadded*6+200+10, tiberium, TVI_LAST );
	//tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("IncTibSizeObList"), 0,0,0,0, valadded*6+200+20, tiberium, TVI_LAST );
	//tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("DecTibSizeObList"), 0,0,0,0, valadded*6+200+21, tiberium, TVI_LAST );
	
	//tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("DrawRanTibObList"), 0,0,0,0, valadded*6+300+0, bluetiberium, TVI_LAST );
	tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("DrawTib2ObList"), 0,0,0,0, valadded*6+300+10, tiberium, TVI_LAST );
	//tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("IncTibSizeObList"), 0,0,0,0, valadded*6+300+20, bluetiberium, TVI_LAST );
	//tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("DecTibSizeObList"), 0,0,0,0, valadded*6+300+21, bluetiberium, TVI_LAST );
#ifndef RA2_MODE
	tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("VeinholeObList"), 0,0,0,0, valadded*6+400+0, veinhole, TVI_LAST );
	tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("VeinsObList"), 0,0,0,0, valadded*6+400+1, veinhole, TVI_LAST );
#endif

	if(Map->GetTheater()!=THEATER4 && Map->GetTheater()!=THEATER5)
	{
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("SmallBridgeObList"), 0,0,0,0, valadded*6+500+1, bridges, TVI_LAST );
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("BigBridgeObList"), 0,0,0,0, valadded*6+500+0, bridges, TVI_LAST );
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("BigTrackBridgeObList"), 0,0,0,0, valadded*6+500+2, bridges, TVI_LAST );
#ifdef RA2_MODE
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("SmallConcreteBridgeObList"), 0,0,0,0, valadded*6+500+3, bridges, TVI_LAST );
#endif
	}
	else
	{
		if(Map->GetTheater()==THEATER5)
		{
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("SmallBridgeObList"), 0,0,0,0, valadded*6+500+1, bridges, TVI_LAST );
#ifdef RA2_MODE
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("SmallConcreteBridgeObList"), 0,0,0,0, valadded*6+500+3, bridges, TVI_LAST );
#endif
		}

	}

#ifndef RA2_MODE
	if (!theApp.m_Options.bEasy && isTrue(g_data.sections["Debug"].values["AllowTunnels"]))
	{
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("NewTunnelObList"), 0, 0, 0, 0, 50, rootitems[9], TVI_LAST);
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("ModifyTunnelObList"), 0, 0, 0, 0, 51, rootitems[9], TVI_LAST);
		if (isTrue(g_data.sections["Debug"].values["AllowUnidirectionalTunnels"]))
		{
			tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("NewTunnelSingleObList"), 0, 0, 0, 0, 52, rootitems[9], TVI_LAST);
			tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("ModifyTunnelSingleObList"), 0, 0, 0, 0, 53, rootitems[9], TVI_LAST);
		}
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("DelTunnelObList"), 0, 0, 0, 0, 54, rootitems[9], TVI_LAST);
	}
#else
	if (!theApp.m_Options.bEasy && isTrue(g_data.sections["Debug"].values["AllowTunnels"]))
	{
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("NewTunnelObList"), 0, 0, 0, 0, 50, rootitems[9], TVI_LAST);
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("ModifyTunnelObList"), 0, 0, 0, 0, 51, rootitems[9], TVI_LAST);
		if (isTrue(g_data.sections["Debug"].values["AllowUnidirectionalTunnels"]))
		{
			tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("NewTunnelSingleObList"), 0, 0, 0, 0, 52, rootitems[9], TVI_LAST);
			tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("ModifyTunnelSingleObList"), 0, 0, 0, 0, 53, rootitems[9], TVI_LAST);
		}
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("DelTunnelObList"), 0,0,0,0, 54, rootitems[9], TVI_LAST );
	}
#endif


	int lv=1;

	if(!theApp.m_Options.bEasy || !Map->IsMultiplayer())
	{
		if(ini.sections.find(MAPHOUSES)!=ini.sections.end() && ini.sections[MAPHOUSES].values.size()>0)
		{
			for(i=0;i<ini.sections[MAPHOUSES].values.size();i++)
			{
#ifdef RA2_MODE
				CString j=*ini.sections[MAPHOUSES].GetValue(i);
				j.MakeLower();
				if(j=="nod" || j=="gdi") continue;
#endif	

				tree.InsertItem(TVIF_PARAM | TVIF_TEXT, TranslateHouse(*ini.sections[MAPHOUSES].GetValue(i), TRUE), 0,0,0,0, valadded*7+i, rootitems[11], TVI_LAST );
			}

		}
		else
		{
			for(i=0;i<rules.sections[HOUSES].values.size();i++)
			{
				if(rules.sections[HOUSES].GetValueOrigPos(i)<0) continue;
				//tree.InsertItem(TVIF_PARAM | TVIF_TEXT, CCStrings[*rules.sections[HOUSES].GetValue(i)].cString, 
				//0,0,0,0, valadded*7+i, rootitems[11], TVI_LAST );
#ifdef RA2_MODE
				CString j=*rules.sections[HOUSES].GetValue(i);
				j.MakeLower();
				if(j=="nod" || j=="gdi") continue;
#endif	
				TV_InsertItemW(tree.m_hWnd, CCStrings[*rules.sections[HOUSES].GetValue(i)].wString, CCStrings[*rules.sections[HOUSES].GetValue(i)].len, TVI_LAST, rootitems[11],valadded*7+i);
			}
		}
	}
	else
	{
		// change owner to neutral
		if(ini.sections.find(MAPHOUSES)!=ini.sections.end() && ini.sections[MAPHOUSES].values.size()>0)
		{
			if(ini.sections[MAPHOUSES].FindValue("Neutral")>=0)
				currentOwner="Neutral";
			else
				currentOwner=*ini.sections[MAPHOUSES].GetValue(0);
		}
		else
			currentOwner="Neutral";
		
	}

	
	for(i=0;i<overlay_count;i++)
	{
		if(overlay_visible[i] && (!yr_only[i] || yuri_mode))
		{
			if(!overlay_trdebug[i] || isTrue(g_data.sections["Debug"].values["EnableTrackLogic"]))
			tree.InsertItem(TVIF_PARAM | TVIF_TEXT, TranslateStringACP(overlay_name[i]), 0,0,0,0, valadded*6+3000+overlay_number[i], alloverlay, TVI_LAST );
		}
	}

	e=0;
	if(!theApp.m_Options.bEasy)
	{
		for(i=0;i<rules.sections["OverlayTypes"].values.size();i++)
		{
			// it seems there is somewhere a bug that lists empty overlay ids... though they are not in the rules.ini
			// so this here is the workaround:
			CString id=*rules.sections["OverlayTypes"].GetValue(i);
			//if(strchr(id,' ')!=NULL){ id[strchr(id,' ')-id;};		
			if(id.Find(' ')>=0) id = id.Left(id.Find(' '));
			if(id.GetLength()>0)
			{
				
				CString unitname=*rules.sections["OverlayTypes"].GetValue(i);

#ifdef RA2_MODE
				if (Map->GetTheater()==THEATER0 && g_data.sections["IgnoreTemperateRA2"].FindValue(unitname) >= 0) continue;
				if (Map->GetTheater()==THEATER1 && g_data.sections["IgnoreSnowRA2"].FindValue(unitname) >= 0) continue;
				if (Map->GetTheater()==THEATER2 && g_data.sections["IgnoreUrbanRA2"].FindValue(unitname) >= 0) continue;
				if (Map->GetTheater()==THEATER3 && g_data.sections["IgnoreNewUrbanRA2"].FindValue(unitname) >= 0) continue;
				if (Map->GetTheater()==THEATER4 && g_data.sections["IgnoreLunarRA2"].FindValue(unitname) >= 0) continue;
				if (Map->GetTheater()==THEATER5 && g_data.sections["IgnoreDesertRA2"].FindValue(unitname) >= 0) continue;
#else
				if (Map->GetTheater()==THEATER0 && g_data.sections["IgnoreTemperateTS"].FindValue(unitname) >= 0) continue;
				if (Map->GetTheater()==THEATER1 && g_data.sections["IgnoreSnowTS"].FindValue(unitname) >= 0) continue;
#endif

#ifdef RA2_MODE
				if((i>=39 && i<=60) || (i>=180 && i<=201) || i==239 || i==178 || i==167 || i==126
					|| (i>=122 && i<=125) || i==1 || (i>=0x03 && i<=0x17) || (i>=0x3d && i<=0x43)
					|| (i>=0x4a && i<=0x65) || (i>=0xcd && i<=0xec))
				{
					if(!isTrue(g_data.sections["Debug"].values["DisplayAllOverlay"]))
					{
						e++;
						continue;
					}
				}


#endif

				CString val=*rules.sections["OverlayTypes"].GetValue(i);
#ifdef RA2_MODE
				val.Replace("TIB", "ORE");
#endif

				tree.InsertItem(TVIF_PARAM | TVIF_TEXT, val , 0,0,0,0, valadded*6+3000+e, everyoverlay, TVI_LAST );
				e++;
			}
		}
	}
	
	
	for(i=0;i<rules.sections["InfantryTypes"].values.size();i++)
	{
		CString unitname=*rules.sections["InfantryTypes"].GetValue(i);

		if(unitname.GetLength()==0) continue;

#ifdef RA2_MODE
		if (Map->GetTheater()==THEATER0 && g_data.sections["IgnoreTemperateRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER1 && g_data.sections["IgnoreSnowRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER2 && g_data.sections["IgnoreUrbanRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER3 && g_data.sections["IgnoreNewUrbanRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER4 && g_data.sections["IgnoreLunarRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER5 && g_data.sections["IgnoreDesertRA2"].FindValue(unitname) >= 0) continue;
#else
		if (Map->GetTheater()==THEATER0 && g_data.sections["IgnoreTemperateTS"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER1 && g_data.sections["IgnoreSnowTS"].FindValue(unitname) >= 0) continue;
#endif

#ifdef RA2_MODE
		if(g_data.sections["IgnoreRA2"].FindValue(unitname)>=0) continue;
#else
		if(g_data.sections["IgnoreTS"].FindValue(unitname) >= 0) continue;
#endif

		WCHAR* addedString=Map->GetUnitName(unitname);
		if(!addedString) continue;

		//addedString=TranslateStringACP(addedString);

		//addedString+=" (";
		//addedString+=unitname+")";

		InsertSideObject(tree, infantrySideNodes, addedString, valadded*1+i,
			GetObjectOwner(ini, unitname), GetObjectPlanningSide(ini, unitname));
		
		//tree.InsertItem(TVIF_PARAM | TVIF_TEXT, addedString, 0,0,0,0, valadded*1+i, rootitems[0], TVI_LAST );
		lv=i;
	}
	lv+=1;
	// okay, now the user-defined types:
	for(i=0;i<ini.sections["InfantryTypes"].values.size();i++)
	{
		if(ini.sections["InfantryTypes"].GetValue(i)->GetLength()==0) continue;

		const CString unitname = *ini.sections["InfantryTypes"].GetValue(i);
		CString addedString = ini.sections[unitname].GetValueByName("Name");
		if(addedString.IsEmpty())
			addedString = unitname + " NOTDEFINED";
		InsertSideObject(tree, infantrySideNodes, addedString,
			valadded*1+rules.sections["InfantryTypes"].values.size()+i,
			GetObjectOwner(ini, unitname), GetObjectPlanningSide(ini, unitname));
	}

	CString theater=Map->GetTheater();

	
	auto needed_terrain=TheaterChar::None;
	if(tiledata==&s_tiledata) needed_terrain=TheaterChar::A;
	else if(tiledata==&t_tiledata) needed_terrain=TheaterChar::T;
    
	for(i=0;i<rules.sections["BuildingTypes"].values.size();i++)
	{
				
		CString unitname=*rules.sections["BuildingTypes"].GetValue(i);

		if(unitname.GetLength()==0) continue;
		
#ifdef RA2_MODE
		if (Map->GetTheater()==THEATER0 && g_data.sections["IgnoreTemperateRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER1 && g_data.sections["IgnoreSnowRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER2 && g_data.sections["IgnoreUrbanRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER3 && g_data.sections["IgnoreNewUrbanRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER4 && g_data.sections["IgnoreLunarRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER5 && g_data.sections["IgnoreDesertRA2"].FindValue(unitname) >= 0) continue;
#else
		if (Map->GetTheater()==THEATER0 && g_data.sections["IgnoreTemperateTS"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER1 && g_data.sections["IgnoreSnowTS"].FindValue(unitname) >= 0) continue;
#endif

#ifdef RA2_MODE
		if (g_data.sections["IgnoreRA2"].FindValue(unitname) >= 0) continue;
#else
		if (g_data.sections["IgnoreTS"].FindValue(unitname) >= 0) continue;
#endif
		if (!isTrue(g_data.sections["Debug"].GetValueByName("ShowBuildingsWithToTile", "0")) && !rules.sections[unitname].GetValueByName("ToTile").IsEmpty())
			continue;


		WCHAR* addedString=Map->GetUnitName(unitname);
		if(!addedString) continue;



		int id=Map->GetBuildingID(unitname);
		if(id<0 /*|| (buildinginfo[id].pic[0].bTerrain!=0 && buildinginfo[id].pic[0].bTerrain!=needed_terrain)*/)
			continue;

		if(theater==THEATER0 && !buildinginfo[id].bTemp) { /*MessageBox("Ignored", unitname,0);*/ continue;}
		if(theater==THEATER1 && !buildinginfo[id].bSnow)  { /*MessageBox("Ignored", unitname,0);*/ continue;}
		if(theater==THEATER2 && !buildinginfo[id].bUrban)  { /*MessageBox("Ignored", unitname,0);*/ continue;}

		//addedString=TranslateStringACP(addedString);

		//addedString+=" (";
		//addedString+=unitname+")";

		InsertSideObject(tree, buildingSideNodes, addedString, valadded*2+i,
			GetObjectOwner(ini, unitname), GetObjectPlanningSide(ini, unitname));

		lv=i;
	}
	lv+=1;
	// okay, now the user-defined types:
	for(i=0;i<ini.sections["BuildingTypes"].values.size();i++)
	{
		if(ini.sections["BuildingTypes"].GetValue(i)->GetLength()==0) continue;

		const CString unitname = *ini.sections["BuildingTypes"].GetValue(i);
		int id=Map->GetBuildingID(unitname);
		if(id<0 || (buildinginfo[id].pic[0].bTerrain!=TheaterChar::None && buildinginfo[id].pic[0].bTerrain!=needed_terrain))
			continue;

		CString addedString = ini.sections[unitname].GetValueByName("Name");
		if(addedString.IsEmpty())
			addedString = unitname + " UNDEFINED";
		InsertSideObject(tree, buildingSideNodes, addedString, valadded*2+i,
			GetObjectOwner(ini, unitname), GetObjectPlanningSide(ini, unitname));
	}



	for(i=0;i<rules.sections["AircraftTypes"].values.size();i++)
	{
		CString unitname=*rules.sections["AircraftTypes"].GetValue(i);
		if(unitname.GetLength()==0) continue;

#ifdef RA2_MODE
		if (Map->GetTheater()==THEATER0 && g_data.sections["IgnoreTemperateRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER1 && g_data.sections["IgnoreSnowRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER2 && g_data.sections["IgnoreUrbanRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER3 && g_data.sections["IgnoreNewUrbanRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER4 && g_data.sections["IgnoreLunarRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER5 && g_data.sections["IgnoreDesertRA2"].FindValue(unitname) >= 0) continue;
#else
		if (Map->GetTheater()==THEATER0 && g_data.sections["IgnoreTemperateTS"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER1 && g_data.sections["IgnoreSnowTS"].FindValue(unitname) >= 0) continue;
#endif

#ifdef RA2_MODE
		if (g_data.sections["IgnoreRA2"].FindValue(unitname) >= 0) continue;
#else
		if (g_data.sections["IgnoreTS"].FindValue(unitname) >= 0) continue;
#endif

		WCHAR* addedString=Map->GetUnitName(unitname);
		if(!addedString) continue;

		//addedString=TranslateStringACP(addedString);

		//addedString+=" (";
		//addedString+=unitname+")";

		InsertSideObject(tree, aircraftSideNodes, addedString, valadded*3+i,
			GetObjectOwner(ini, unitname), GetObjectPlanningSide(ini, unitname));
		
		//tree.InsertItem(TVIF_PARAM | TVIF_TEXT, addedString, 0,0,0,0, valadded*3+i, rootitems[2], TVI_LAST );
		lv=i;
	}
	lv+=1;
	// okay, now the user-defined types:
	for(i=0;i<ini.sections["AircraftTypes"].values.size();i++)
	{
		if(ini.sections["AircraftTypes"].GetValue(i)->GetLength()==0) continue;

		const CString unitname = *ini.sections["AircraftTypes"].GetValue(i);
		CString addedString = ini.sections[unitname].GetValueByName("Name");
		if(addedString.IsEmpty())
			addedString = unitname + " NOTDEFINED";
		InsertSideObject(tree, aircraftSideNodes, addedString,
			valadded*3+i+rules.sections["AircraftTypes"].values.size(),
			GetObjectOwner(ini, unitname), GetObjectPlanningSide(ini, unitname));
	}

	
	for(i=0;i<rules.sections["VehicleTypes"].values.size();i++)
	{
		CString unitname=*rules.sections["VehicleTypes"].GetValue(i);
		if(unitname.GetLength()==0) continue;

#ifdef RA2_MODE
		if (Map->GetTheater()==THEATER0 && g_data.sections["IgnoreTemperateRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER1 && g_data.sections["IgnoreSnowRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER2 && g_data.sections["IgnoreUrbanRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER3 && g_data.sections["IgnoreNewUrbanRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER4 && g_data.sections["IgnoreLunarRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER5 && g_data.sections["IgnoreDesertRA2"].FindValue(unitname) >= 0) continue;
#else
		if (Map->GetTheater()==THEATER0 && g_data.sections["IgnoreTemperateTS"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER1 && g_data.sections["IgnoreSnowTS"].FindValue(unitname) >= 0) continue;
#endif
		
#ifdef RA2_MODE
		if (g_data.sections["IgnoreRA2"].FindValue(unitname) >= 0) continue;
#else
		if (g_data.sections["IgnoreTS"].FindValue(unitname) >= 0) continue;
#endif

		WCHAR* addedString=Map->GetUnitName(unitname);
		if(!addedString) continue;

		//addedString=TranslateStringACP(addedString);

		//addedString+=" (";
		//addedString+=unitname+")";

		InsertSideObject(tree, vehicleSideNodes, addedString, valadded*4+i,
			GetObjectOwner(ini, unitname), GetObjectPlanningSide(ini, unitname));
		
		//tree.InsertItem(TVIF_PARAM | TVIF_TEXT, addedString, 0,0,0,0, valadded*4+i, rootitems[1], TVI_LAST );
		lv=i;
	}
	lv+=1;
	// okay, now the user-defined types:
	for(i=0;i<ini.sections["VehicleTypes"].values.size();i++)
	{
		if(ini.sections["VehicleTypes"].GetValue(i)->GetLength()==0) continue;

		const CString unitname = *ini.sections["VehicleTypes"].GetValue(i);
		CString addedString = ini.sections[unitname].GetValueByName("Name");
		if(addedString.IsEmpty())
			addedString = unitname + " NOTDEFINED";
		InsertSideObject(tree, vehicleSideNodes, addedString,
			valadded*4+i+rules.sections["VehicleTypes"].values.size(),
			GetObjectOwner(ini, unitname), GetObjectPlanningSide(ini, unitname));
	}


	#ifdef RA2_MODE
	HTREEITEM hTrees=tree.InsertItem(GetLanguageStringACP("TreesObList"), rootitems[4], TVI_LAST);
	HTREEITEM hTL=tree.InsertItem(GetLanguageStringACP("TrafficLightsObList"), rootitems[4], TVI_LAST);
	HTREEITEM hSigns=tree.InsertItem(GetLanguageStringACP("SignsObList"), rootitems[4], TVI_LAST);
	HTREEITEM hLightPosts=tree.InsertItem(GetLanguageStringACP("LightPostsObList"), rootitems[4], TVI_LAST);
	#endif
	
	// random tree placer
#ifdef RA2_MODE
	tree.InsertItem(TVIF_PARAM | TVIF_TEXT,  GetLanguageStringACP("RndTreeObList"), 0,0,0,0, valadded*5+999, hTrees, TVI_LAST);
#else
	tree.InsertItem(TVIF_PARAM | TVIF_TEXT, GetLanguageStringACP("RndTreeObList"), 0,0,0,0, valadded*5+999, rootitems[4], TVI_LAST);
#endif

	for(i=0;i<rules.sections["TerrainTypes"].values.size();i++)
	{
		CString unitname=*rules.sections["TerrainTypes"].GetValue(i);
		CString addedString=Map->GetUnitName(unitname);

#ifdef RA2_MODE
		if (Map->GetTheater()==THEATER0 && g_data.sections["IgnoreTemperateRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER1 && g_data.sections["IgnoreSnowRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER2 && g_data.sections["IgnoreUrbanRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER3 && g_data.sections["IgnoreNewUrbanRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER4 && g_data.sections["IgnoreLunarRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER5 && g_data.sections["IgnoreDesertRA2"].FindValue(unitname) >= 0) continue;
#else
		if (Map->GetTheater()==THEATER0 && g_data.sections["IgnoreTemperateTS"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER1 && g_data.sections["IgnoreSnowTS"].FindValue(unitname) >= 0) continue;
#endif

#ifdef RA2_MODE
		if (g_data.sections["IgnoreRA2"].FindValue(unitname) >= 0) continue;
#else
		if (g_data.sections["IgnoreTS"].FindValue(unitname) >= 0) continue;
#endif
#ifdef RA2_MODE
		if (g_data.sections["IgnoreTerrainRA2"].FindValue(unitname) >= 0) continue;
#else
		if (g_data.sections["IgnoreTerrainTS"].FindValue(unitname) >= 0) continue;
#endif
	
		addedString=TranslateStringACP(addedString);
			
		UINT flags=MF_STRING;
		
		HTREEITEM howner=rootitems[4];

		#ifdef RA2_MODE
		if(unitname.Find("SIGN")>=0) howner=hSigns;
		if(unitname.Find("TRFF")>=0) howner=hTL;
		if(unitname.Find("TREE")>=0) howner=hTrees;
		if(unitname.Find("LT")>=0) howner=hLightPosts;		
		#endif
		
#ifdef RA2_MODE
		if(howner==hTrees)
		{
			int TreeMin=atoi(g_data.sections[Map->GetTheater()+"Limits"].values["TreeMin"]);
			int TreeMax=atoi(g_data.sections[Map->GetTheater()+"Limits"].values["TreeMax"]);
			
			CString id=unitname;
			id.Delete(0, 4);
			int n=atoi(id);

			if(n<TreeMin || n>TreeMax) continue;
		}
#endif

		if(unitname.GetLength()>0 && unitname!="VEINTREE" && unitname.Find("ICE")<0 && unitname.Find("BOXES")<0 && unitname.Find("SPKR")<0) // out with it :-)
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT,  (addedString+ " (" + unitname +")"), 0,0,0,0, valadded*5+i, howner, TVI_LAST );

		lv=i;
	}

#ifdef SMUDGE_SUPP
	for(i=0;i<rules.sections["SmudgeTypes"].values.size();i++)
	{
		CString unitname=*rules.sections["SmudgeTypes"].GetValue(i);
		CString addedString=unitname;

#ifdef RA2_MODE
		if (Map->GetTheater()==THEATER0 && g_data.sections["IgnoreTemperateRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER1 && g_data.sections["IgnoreSnowRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER2 && g_data.sections["IgnoreUrbanRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER3 && g_data.sections["IgnoreNewUrbanRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER4 && g_data.sections["IgnoreLunarRA2"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER5 && g_data.sections["IgnoreDesertRA2"].FindValue(unitname) >= 0) continue;
#else
		if (Map->GetTheater()==THEATER0 && g_data.sections["IgnoreTemperateTS"].FindValue(unitname) >= 0) continue;
		if (Map->GetTheater()==THEATER1 && g_data.sections["IgnoreSnowTS"].FindValue(unitname) >= 0) continue;
#endif

#ifdef RA2_MODE
		if (g_data.sections["IgnoreRA2"].FindValue(unitname) >= 0) continue;
#else
		if (g_data.sections["IgnoreTS"].FindValue(unitname) >= 0) continue;
#endif
	
		addedString=TranslateStringACP(addedString);
			
		UINT flags=MF_STRING;
		
		HTREEITEM howner=rootitems[14];

		
		if(unitname.GetLength()>0) 
		tree.InsertItem(TVIF_PARAM | TVIF_TEXT,  unitname, 0,0,0,0, valadded*8+i, howner, TVI_LAST );

		lv=i;
	}
#endif

	

	OutputDebugString("Objectbrowser redraw finished\n");
	

}

int CViewObjects::OnCreate(LPCREATESTRUCT lpCreateStruct) 
{
	lpCreateStruct->style|=TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS;
	if (CTreeView::OnCreate(lpCreateStruct) == -1)
		return -1;
	
	
	return 0;
}

void CViewObjects::OnInitialUpdate() 
{
	CTreeView::OnInitialUpdate();
	
	
	m_ready=TRUE;
}

void CViewObjects::OnKeydown(NMHDR* pNMHDR, LRESULT* pResult) 
{
	TV_KEYDOWN* pTVKeyDown = (TV_KEYDOWN*)pNMHDR;
	// TODO: Code für die Behandlungsroutine der Steuerelement-Benachrichtigung hier einfügen
	
	*pResult = 0;
}

void CViewObjects::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) 
{
	// TODO: Code für die Behandlungsroutine für Nachrichten hier einfügen und/oder Standard aufrufen
	
	// CTreeView::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CViewObjects::HandleBrushSize(int iTile)
{
	if(iTile>=*tiledata_count) return;

	int i;
	for(i=0;i<g_data.sections["StdBrushSize"].values.size();i++)
	{
		CString n=*g_data.sections["StdBrushSize"].GetValueName(i);
		if((*tiles).sections["General"].FindName(n)>=0)
		{
			int tset=atoi((*tiles).sections["General"].values[n]);
			if(tset==(*tiledata)[iTile].wTileSet)
			{
				int bs=atoi(*g_data.sections["StdBrushSize"].GetValue(i));
				((CFinalSunDlg*)theApp.m_pMainWnd)->m_settingsbar.m_BrushSize=bs-1;
				((CFinalSunDlg*)theApp.m_pMainWnd)->m_settingsbar.UpdateData(FALSE);
				((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->m_BrushSize_x=bs;
				((CFinalSunDlg*)theApp.m_pMainWnd)->m_view.m_isoview->m_BrushSize_y=bs;
			}
		}
	}

}
