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

// TSOptions.cpp: implementation
//

#include "stdafx.h"
#include "FinalSun.h"
#include "TSOptions.h"
#include "resource.h"
#include "mapdata.h"
#include "variables.h"
#include "functions.h"

extern CFinalSunApp theApp;

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// Dialogfeld CTSOptions 


CTSOptions::CTSOptions(CWnd* pParent /*=NULL*/)
	: CDialog(CTSOptions::IDD, pParent)
	, m_PreferLocalTheaterFiles(FALSE)
{
	//{{AFX_DATA_INIT(CTSOptions)
	m_LikeTS = -1;
	//}}AFX_DATA_INIT
}


void CTSOptions::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CTSOptions)
	DDX_Control(pDX, IDC_LANGUAGE, m_Language);
	DDX_Control(pDX, IDC_AUTOSAVE_LOCATION, m_AutoSaveLocationControl);
	DDX_Control(pDX, IDC_EDIT1, m_TSExe);
	DDX_Radio(pDX, IDC_RULESLIKETS, m_LikeTS);
	DDX_Check(pDX, IDC_PREFER_LOCAL_THEATER_FILES, m_PreferLocalTheaterFiles);
	DDX_Check(pDX, IDC_AUTOSAVE_ENABLED, m_AutoSaveEnabled);
	DDX_Text(pDX, IDC_AUTOSAVE_INTERVAL, m_AutoSaveIntervalMinutes);
	DDV_MinMaxInt(pDX, m_AutoSaveIntervalMinutes, 1, 1440);
	DDX_CBIndex(pDX, IDC_AUTOSAVE_LOCATION, m_AutoSaveLocation);
	DDX_Text(pDX, IDC_AUTOSAVE_PATH, m_AutoSaveDirectory);
	DDX_Check(pDX, IDC_AUTOSAVE_VERSIONED, m_AutoSaveVersioned);
	DDX_Text(pDX, IDC_AUTOSAVE_MAXCOUNT, m_AutoSaveMaxCount);
	DDV_MinMaxInt(pDX, m_AutoSaveMaxCount, 1, 1000);
	DDX_Check(pDX, IDC_FILE_WATCHER, m_FileWatcher);
	//}}AFX_DATA_MAP	
}


BEGIN_MESSAGE_MAP(CTSOptions, CDialog)
	//{{AFX_MSG_MAP(CTSOptions)
	ON_BN_CLICKED(IDC_CHOOSE, OnChoose)
	ON_BN_CLICKED(IDC_AUTOSAVE_BROWSE, OnChooseAutoSaveDirectory)
	ON_BN_CLICKED(IDC_AUTOSAVE_ENABLED, OnAutoSaveSettingChanged)
	ON_BN_CLICKED(IDC_AUTOSAVE_VERSIONED, OnAutoSaveSettingChanged)
	ON_CBN_SELCHANGE(IDC_AUTOSAVE_LOCATION, OnAutoSaveSettingChanged)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// Behandlungsroutinen für Nachrichten CTSOptions 

void CTSOptions::OnChoose() 
{
#ifndef RA2_MODE
	CFileDialog fd(TRUE, NULL, "Sun.exe", OFN_FILEMUSTEXIST, "Tiberian Sun EXE|Sun.exe|");
#else
	CFileDialog fd(TRUE, NULL, "ra2.exe", OFN_FILEMUSTEXIST, "Red Alert 2 EXE|ra2.exe|");
#endif

	if (fd.DoModal() == IDOK)
		GetDlgItem(IDC_EDIT1)->SetWindowText(fd.GetPathName());
}

void CTSOptions::OnChooseAutoSaveDirectory()
{
	CString currentPath;
	GetDlgItemText(IDC_AUTOSAVE_PATH, currentPath);
	CFolderPickerDialog dialog(currentPath, OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR, this);
	if (dialog.DoModal() == IDOK)
		SetDlgItemText(IDC_AUTOSAVE_PATH, dialog.GetPathName());
}

void CTSOptions::OnAutoSaveSettingChanged()
{
	UpdateAutoSaveControlState();
}

void CTSOptions::UpdateAutoSaveControlState()
{
	const BOOL enabled = IsDlgButtonChecked(IDC_AUTOSAVE_ENABLED) == BST_CHECKED;
	const int location = m_AutoSaveLocationControl.GetCurSel();
	const BOOL customLocation = enabled && location == 2;
	const BOOL versioned = enabled && IsDlgButtonChecked(IDC_AUTOSAVE_VERSIONED) == BST_CHECKED;

	for (int controlId : { IDC_AUTOSAVE_INTERVAL_LABEL, IDC_AUTOSAVE_INTERVAL,
		IDC_AUTOSAVE_MINUTES_LABEL, IDC_AUTOSAVE_LOCATION_LABEL, IDC_AUTOSAVE_LOCATION,
		IDC_AUTOSAVE_VERSIONED })
	{
		if (auto* control = GetDlgItem(controlId))
			control->EnableWindow(enabled);
	}
	GetDlgItem(IDC_AUTOSAVE_PATH)->EnableWindow(customLocation);
	GetDlgItem(IDC_AUTOSAVE_BROWSE)->EnableWindow(customLocation);
	GetDlgItem(IDC_AUTOSAVE_MAXCOUNT_LABEL)->EnableWindow(versioned);
	GetDlgItem(IDC_AUTOSAVE_MAXCOUNT)->EnableWindow(versioned);
}

void CTSOptions::OnOK() 
{
	if (!UpdateData(TRUE))
		return;
	if (m_AutoSaveEnabled && m_AutoSaveLocation == 2)
	{
		m_AutoSaveDirectory.Trim();
		if (m_AutoSaveDirectory.IsEmpty())
		{
			AfxMessageBox(GetLanguageStringACP("OptAutoSaveCustomRequired"), MB_ICONWARNING);
			GetDlgItem(IDC_AUTOSAVE_PATH)->SetFocus();
			return;
		}
	}
	this->GetDlgItem(IDC_EDIT1)->GetWindowText(m_TSEXE);
	int n=m_Language.GetItemData(m_Language.GetCurSel()); 
	
	m_LanguageName=*language.sections["Languages"].GetValue(n);

	CDialog::OnOK();
}

BOOL CTSOptions::OnInitDialog() 
{
	CDialog::OnInitDialog();

	if (theApp.m_Options.LanguageName.IsEmpty())
		theApp.m_Options.LanguageName = DetectPreferredLanguageName();

	ApplyEditorUIFont(this);
	SetWindowText(GetLanguageStringACP("OptCaption"));
	SetDlgItemText(IDC_OPT_EXE_LABEL, GetLanguageStringACP("OptExeLabel"));
	SetDlgItemText(IDC_OPT_LANG_LABEL, GetLanguageStringACP("OptLanguage"));
	SetDlgItemText(IDC_OPT_SUPPORT_GROUP, GetLanguageStringACP("OptSupportGroup"));
	SetDlgItemText(IDC_CHOOSE, GetLanguageStringACP("OptBrowse"));
	SetDlgItemText(IDC_RULESLIKETS, GetLanguageStringACP("OptSupportMods"));
	SetDlgItemText(IDC_ONLYORIGINAL, GetLanguageStringACP("OptOnlyOriginal"));
	SetDlgItemText(IDC_PREFER_LOCAL_THEATER_FILES, GetLanguageStringACP("OptPreferLocalTheater"));
	SetDlgItemText(IDC_AUTOSAVE_GROUP, GetLanguageStringACP("OptAutoSaveGroup"));
	SetDlgItemText(IDC_AUTOSAVE_ENABLED, GetLanguageStringACP("OptAutoSaveEnabled"));
	SetDlgItemText(IDC_AUTOSAVE_INTERVAL_LABEL, GetLanguageStringACP("OptAutoSaveInterval"));
	SetDlgItemText(IDC_AUTOSAVE_MINUTES_LABEL, GetLanguageStringACP("OptAutoSaveMinutes"));
	SetDlgItemText(IDC_AUTOSAVE_LOCATION_LABEL, GetLanguageStringACP("OptAutoSaveLocation"));
	SetDlgItemText(IDC_AUTOSAVE_BROWSE, GetLanguageStringACP("OptBrowse"));
	SetDlgItemText(IDC_AUTOSAVE_VERSIONED, GetLanguageStringACP("OptAutoSaveVersioned"));
	SetDlgItemText(IDC_AUTOSAVE_MAXCOUNT_LABEL, GetLanguageStringACP("OptAutoSaveMaxCount"));
	SetDlgItemText(IDC_FILE_WATCHER, GetLanguageStringACP("OptFileWatcher"));
	SetDlgItemText(IDOK, GetLanguageStringACP("OK"));
	SetDlgItemText(IDCANCEL, GetLanguageStringACP("Cancel"));
	
	m_TSExe.SetWindowText((LPCTSTR)theApp.m_Options.TSExe);
	
	if(theApp.m_Options.bSearchLikeTS) m_LikeTS=0;
	else m_LikeTS=1;

	m_PreferLocalTheaterFiles = theApp.m_Options.bPreferLocalTheaterFiles ? TRUE : FALSE;
	m_AutoSaveEnabled = theApp.m_Options.autoSaveEnabled ? TRUE : FALSE;
	m_AutoSaveIntervalMinutes = theApp.m_Options.autoSaveIntervalMinutes;
	m_AutoSaveLocation = theApp.m_Options.autoSaveLocation;
	m_AutoSaveDirectory = theApp.m_Options.autoSaveCustomDirectory;
	m_AutoSaveVersioned = theApp.m_Options.autoSaveVersioned ? TRUE : FALSE;
	m_AutoSaveMaxCount = theApp.m_Options.autoSaveMaxCount;
	m_FileWatcher = theApp.m_Options.bFileWatcher ? TRUE : FALSE;
	m_AutoSaveLocationControl.AddString(GetLanguageStringACP("OptAutoSaveLocationAppData"));
	m_AutoSaveLocationControl.AddString(GetLanguageStringACP("OptAutoSaveLocationMap"));
	m_AutoSaveLocationControl.AddString(GetLanguageStringACP("OptAutoSaveLocationCustom"));

	UpdateData(FALSE);
	UpdateAutoSaveControlState();

	int i;
	for(i=0;i<language.sections["Languages"].values.size();i++)
	{
		CString langId=*language.sections["Languages"].GetValue(i);
		CString langName=language.sections[langId+"Header"].values["Name"];

		const int idx = m_Language.AddString(langName);
		m_Language.SetItemData(idx,i);
	}

	int selected = 0;
	for(i=0;i<m_Language.GetCount();i++)
	{
		const int langIndex = static_cast<int>(m_Language.GetItemData(i));
		CString langId=*language.sections["Languages"].GetValue(langIndex);
		if (langId == theApp.m_Options.LanguageName)
			selected = i;
	}
	m_Language.SetCurSel(selected);

	
	return TRUE;  
}
