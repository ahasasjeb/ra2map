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

// AlliesEditorDlg.cpp: Implementierungsdatei
//
// Allies editor dialog for the Houses dialog, ported from FA2sp and
// extended with a bidirectional alliance option.

#include "stdafx.h"
#include "finalsun.h"
#include "AlliesEditorDlg.h"
#include "mapdata.h"
#include "variables.h"
#include "functions.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// Dialogfeld CAlliesEditorDlg

static bool ListContains(const std::vector<CString>& list, const CString& name)
{
	for (const auto& entry : list)
		if (entry == name) return true;
	return false;
}

std::vector<CString> SplitAlliesValue(const CString& allies)
{
	std::vector<CString> result;
	CString current;
	int i;
	for (i = 0; i < allies.GetLength(); i++)
	{
		if (allies.GetAt(i) == ',')
		{
			current.TrimLeft();
			current.TrimRight();
			if (current.GetLength() > 0) result.push_back(current);
			current = "";
		}
		else
			current += allies.GetAt(i);
	}
	current.TrimLeft();
	current.TrimRight();
	if (current.GetLength() > 0) result.push_back(current);
	return result;
}

CString UpdateAlliesValue(const CString& allies, const CString& houseName, BOOL bAllied)
{
	std::vector<CString> parts = SplitAlliesValue(allies);
	CString result;
	BOOL bFound = FALSE;
	for (const auto& part : parts)
	{
		if (part == houseName)
		{
			bFound = TRUE;
			if (!bAllied) continue;
		}
		if (result.GetLength() > 0) result += ",";
		result += part;
	}
	if (bAllied && !bFound)
	{
		if (result.GetLength() > 0) result += ",";
		result += houseName;
	}
	return result;
}

CAlliesEditorDlg::CAlliesEditorDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CAlliesEditorDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CAlliesEditorDlg)
	//}}AFX_DATA_INIT

	m_bSelfAllied = FALSE;
}


void CAlliesEditorDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAlliesEditorDlg)
	DDX_Control(pDX, IDC_ALLYED_ENEMIES, m_Enemies);
	DDX_Control(pDX, IDC_ALLYED_ALLIES, m_Allies);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CAlliesEditorDlg, CDialog)
	//{{AFX_MSG_MAP(CAlliesEditorDlg)
	ON_BN_CLICKED(IDC_ALLYED_TO_ALLIES, OnToAllies)
	ON_BN_CLICKED(IDC_ALLYED_TO_ENEMIES, OnToEnemies)
	ON_LBN_DBLCLK(IDC_ALLYED_ENEMIES, OnDblClkEnemies)
	ON_LBN_DBLCLK(IDC_ALLYED_ALLIES, OnDblClkAllies)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// Behandlungsroutinen für Nachrichten CAlliesEditorDlg

static void LocalizeDlgItem(CWnd* wnd, int nID, const char* lpKey)
{
	CString s = GetLanguageStringACP(lpKey);
	if (s.GetLength() > 0) wnd->SetDlgItemText(nID, s);
}

BOOL CAlliesEditorDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	ApplyEditorUIFont(this);

	LocalizeDlgItem(this, IDC_ALLYED_LENEMIES, "AlliesEditorEnemies");
	LocalizeDlgItem(this, IDC_ALLYED_LALLIES, "AlliesEditorAllies");
	LocalizeDlgItem(this, IDC_ALLYED_LHOUSE, "AlliesEditorHouse");
	LocalizeDlgItem(this, IDC_ALLYED_BIDIRECTIONAL, "AlliesEditorBidirectional");
	LocalizeDlgItem(this, IDOK, "AlliesEditorOK");
	LocalizeDlgItem(this, IDCANCEL, "AlliesEditorCancel");

	CString caption = GetLanguageStringACP("AlliesEditorTitle");
	if (caption.GetLength() > 0) SetWindowText(caption);

	SetDlgItemText(IDC_ALLYED_CURHOUSE, m_CurrentHouse);
	CheckDlgButton(IDC_ALLYED_BIDIRECTIONAL, BST_CHECKED);

	CString currentHouse = TranslateHouse(m_CurrentHouse);

	CIniFile& ini = Map->GetIniFile();

	std::vector<CString> allies;
	const CIniFileSection* currentSec = ini.GetSection(currentHouse);
	if (currentSec != NULL)
		// Normalize stored allies to internal house names so the comparisons against
		// the [Houses] list (which uses internal names) stay correct even if the
		// value was previously saved as UI names by the Allies edit box.
		allies = SplitAlliesValue(TranslateHouse(currentSec->GetValueByName("Allies")));

	BOOL bSelfAllied = ListContains(allies, currentHouse);
	m_bSelfAllied = bSelfAllied;

	std::vector<CString> mapHouses;
	const CIniFileSection* houseList = ini.GetSection(MAPHOUSES);
	if (houseList != NULL)
	{
		for (const auto& entry : houseList->values)
		{
			const CString& house = entry.second;
#ifdef RA2_MODE
			CString lower = house;
			lower.MakeLower();
			if (lower == "nod" || lower == "gdi") continue;
#endif
			if (house == currentHouse) continue;

			mapHouses.push_back(house);
			if (ListContains(allies, house))
				m_Allies.AddString(TranslateHouse(house, TRUE));
			else
				m_Enemies.AddString(TranslateHouse(house, TRUE));
		}
	}

	// keep allies that are no map houses (like Neutral) so they are not lost
	for (const auto& ally : allies)
	{
		if (ally == currentHouse) continue;
		if (ListContains(mapHouses, ally)) continue;
		m_Allies.AddString(TranslateHouse(ally, TRUE));
	}

	return TRUE;  // return TRUE unless you set the focus to a control
}

void CAlliesEditorDlg::MoveSelection(CListBox& from, CListBox& to)
{
	int sel = from.GetCurSel();
	if (sel == LB_ERR) return;

	CString text;
	from.GetText(sel, text);
	from.DeleteString(sel);
	to.AddString(text);

	if (from.GetCount() > 0)
	{
		if (sel >= from.GetCount()) sel = from.GetCount() - 1;
		from.SetCurSel(sel);
	}
}

void CAlliesEditorDlg::OnToAllies()
{
	MoveSelection(m_Enemies, m_Allies);
}

void CAlliesEditorDlg::OnToEnemies()
{
	MoveSelection(m_Allies, m_Enemies);
}

void CAlliesEditorDlg::OnDblClkEnemies()
{
	MoveSelection(m_Enemies, m_Allies);
}

void CAlliesEditorDlg::OnDblClkAllies()
{
	MoveSelection(m_Allies, m_Enemies);
}

void CAlliesEditorDlg::SyncBidirectionalAlliances(const std::vector<CString>& newAllies)
{
	CIniFile& ini = Map->GetIniFile();

	CString currentHouse = TranslateHouse(m_CurrentHouse);

	const CIniFileSection* houseList = ini.GetSection(MAPHOUSES);
	if (houseList == NULL) return;

	for (const auto& entry : houseList->values)
	{
		const CString& house = entry.second;
		if (house == currentHouse) continue;

		CIniFileSection* sec = ini.GetSection(house);
		if (sec == NULL) continue;

		BOOL bAllied = ListContains(newAllies, house);
		// Normalize the other house's existing allies to internal names before
		// adding/removing the current house, so the exact-match comparison in
		// UpdateAlliesValue works regardless of whether the stored value uses
		// UI or internal house names. Only write back when the logical content
		// changed, to avoid touching data the user did not edit.
		CString existing = TranslateHouse(sec->GetValueByName("Allies"));
		CString updated = UpdateAlliesValue(existing, currentHouse, bAllied);
		if (updated != existing)
			sec->values["Allies"] = updated;
	}
}

void CAlliesEditorDlg::OnOK()
{
	CString currentHouse = TranslateHouse(m_CurrentHouse);

	std::vector<CString> newAllies;
	CString result;
	int i;
	for (i = 0; i < m_Allies.GetCount(); i++)
	{
		CString text;
		m_Allies.GetText(i, text);

		CString house = TranslateHouse(text);
		if (house == currentHouse) continue;
		if (ListContains(newAllies, house)) continue;

		newAllies.push_back(house);
		if (result.GetLength() > 0) result += ",";
		result += text;
	}

	m_AlliesResult = result;
	if (m_bSelfAllied)
	{
		if (m_AlliesResult.GetLength() > 0)
			m_AlliesResult = m_CurrentHouse + "," + m_AlliesResult;
		else
			m_AlliesResult = m_CurrentHouse;
	}

	if (IsDlgButtonChecked(IDC_ALLYED_BIDIRECTIONAL))
		SyncBidirectionalAlliances(newAllies);

	CDialog::OnOK();
}
