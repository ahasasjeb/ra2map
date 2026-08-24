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

#if !defined(AFX_ALLIESEDITORDLG_H__6C1A5B40_7F2D_4E11_9C36_D08341E57A21__INCLUDED_)
#define AFX_ALLIESEDITORDLG_H__6C1A5B40_7F2D_4E11_9C36_D08341E57A21__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// AlliesEditorDlg.h : Header-Datei
//

#include <vector>

/////////////////////////////////////////////////////////////////////////////
// Dialogfeld CAlliesEditorDlg

// Splits a comma separated Allies value into its trimmed, non-empty entries.
std::vector<CString> SplitAlliesValue(const CString& allies);

// Adds or removes a single house in a comma separated Allies value,
// leaving all other entries (and their order) untouched.
CString UpdateAlliesValue(const CString& allies, const CString& houseName, BOOL bAllied);

class CAlliesEditorDlg : public CDialog
{
// Konstruktion
public:
	CAlliesEditorDlg(CWnd* pParent = NULL);   // Standardkonstruktor

	// house (UI name) whose allies are being edited, set before DoModal
	CString m_CurrentHouse;
	// comma separated allies (UI names) to hand back to the Houses dialog
	CString m_AlliesResult;

// Dialogfelddaten
	//{{AFX_DATA(CAlliesEditorDlg)
	enum { IDD = IDD_ALLIES_EDITOR };
	CListBox	m_Enemies;
	CListBox	m_Allies;
	//}}AFX_DATA


// Überschreibungen
	// Vom Klassen-Assistenten generierte virtuelle Funktionsüberschreibungen
	//{{AFX_VIRTUAL(CAlliesEditorDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV-Unterstützung
	//}}AFX_VIRTUAL

// Implementierung
protected:
	void MoveSelection(CListBox& from, CListBox& to);
	void SyncBidirectionalAlliances(const std::vector<CString>& newAllies);

	// the editor lists a house as its own ally, so keep that entry intact
	BOOL m_bSelfAllied;

	// Generierte Nachrichtenzuordnungsfunktionen
	//{{AFX_MSG(CAlliesEditorDlg)
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	afx_msg void OnToAllies();
	afx_msg void OnToEnemies();
	afx_msg void OnDblClkEnemies();
	afx_msg void OnDblClkAllies();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ fügt unmittelbar vor der vorhergehenden Zeile zusätzliche Deklarationen ein.

#endif // !defined(AFX_ALLIESEDITORDLG_H__6C1A5B40_7F2D_4E11_9C36_D08341E57A21__INCLUDED_)
