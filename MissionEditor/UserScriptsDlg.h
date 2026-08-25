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

#if !defined(AFX_USERSCRIPTSDLG_H__6A37EE40_9653_11D5_89B3_00E07D97C331__INCLUDED_)
#define AFX_USERSCRIPTSDLG_H__6A37EE40_9653_11D5_89B3_00E07D97C331__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// UserScriptsDlg.h : Header-Datei
//

#include <vector>

/////////////////////////////////////////////////////////////////////////////
// Dialogfeld CUserScriptsDlg 

class CUserScriptsDlg : public CDialog
{
// Konstruktion
public:
	void ReportScriptError(int line);
	CUserScriptsDlg(CWnd* pParent = NULL);   // Standardkonstruktor

// Dialogfelddaten
	//{{AFX_DATA(CUserScriptsDlg)
	enum { IDD = IDD_USERSCRIPTS };
	CString	m_Script;
	CString	m_Report;
	CString	m_Source;
	//}}AFX_DATA


// Überschreibungen
	// Vom Klassen-Assistenten generierte virtuelle Funktionsüberschreibungen
	//{{AFX_VIRTUAL(CUserScriptsDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV-Unterstützung
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	//}}AFX_VIRTUAL

// Implementierung
protected:

	// Generierte Nachrichtenzuordnungsfunktionen
	//{{AFX_MSG(CUserScriptsDlg)
	virtual void OnOK();
	virtual void OnCancel();
	virtual BOOL OnInitDialog();
	afx_msg void OnSelchangeScripts();
	afx_msg void OnChangeScriptEditor();
	afx_msg void OnSaveScript();
	afx_msg void OnNewScript();
	afx_msg void OnCopyApiMarkdown();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

private:
	CString GetScriptPath(const CString& scriptName) const;
	BOOL LoadScriptSource(const CString& scriptName);
	BOOL SaveCurrentScript();
	BOOL ConfirmSaveChanges();
	BOOL SelectScript(const CString& scriptName);
	BOOL IsEditorDirty();
	void UpdateEditorState();

	CString m_loadedScript;
	CString m_originalSource;
	std::vector<int> m_sourceLines;
	BOOL m_loadingSource;
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ fügt unmittelbar vor der vorhergehenden Zeile zusätzliche Deklarationen ein.

#endif // AFX_USERSCRIPTSDLG_H__6A37EE40_9653_11D5_89B3_00E07D97C331__INCLUDED_
