/*
    FinalSun/FinalAlert 2 Mission Editor

    Copyright (C) 1999-2024 Electronic Arts, Inc.
    Authored by Matthias Wagner

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "StdAfx.h"
#include "FinalSun.h"
#include "CsfBrowserDlg.h"
#include "functions.h"
#include "variables.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	constexpr UINT_PTR SearchTimerId=1;
	constexpr UINT SearchDelayMilliseconds=150;
}

CCsfBrowserDlg::CCsfBrowserDlg(CWnd* pParent)
	: CDialog(CCsfBrowserDlg::IDD, pParent)
{
}

void CCsfBrowserDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_CSF_BROWSER_LIST, m_List);
}

BEGIN_MESSAGE_MAP(CCsfBrowserDlg, CDialog)
	ON_EN_CHANGE(IDC_CSF_BROWSER_SEARCH, OnChangeSearch)
	ON_WM_TIMER()
	ON_NOTIFY(LVN_GETDISPINFO, IDC_CSF_BROWSER_LIST, OnGetdispinfoList)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_CSF_BROWSER_LIST, OnItemchangedList)
END_MESSAGE_MAP()

BOOL CCsfBrowserDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	ApplyEditorUIFont(this);

	SetWindowText(TranslateStringACP("CSF Browser"));
	SetDlgItemText(IDC_CSF_BROWSER_SEARCH_LABEL, TranslateStringACP("Search"));
	SetDlgItemText(IDCANCEL, TranslateStringACP("Close"));

	m_List.SetExtendedStyle(m_List.GetExtendedStyle()
		| LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
	m_List.InsertColumn(0, TranslateStringACP("String ID"), LVCFMT_LEFT, 155);
	m_List.InsertColumn(1, TranslateStringACP("Text"), LVCFMT_LEFT, 315);

	BuildRows();
	ApplySearch();
	GetDlgItem(IDC_CSF_BROWSER_SEARCH)->SetFocus();
	return FALSE;
}

void CCsfBrowserDlg::OnCancel()
{
	KillTimer(SearchTimerId);
	CDialog::OnCancel();
}

void CCsfBrowserDlg::BuildRows()
{
	m_Rows.clear();
	m_Rows.reserve(AllStrings.size());

	for(const auto& entry : AllStrings)
	{
		Row row;
		row.id=&entry.first;
		row.value=&entry.second.cString;
		row.searchText=entry.first;
		row.searchText+='\n';
		row.searchText+=entry.second.cString;
		row.searchText.MakeLower();
		m_Rows.push_back(std::move(row));
	}
}

void CCsfBrowserDlg::ApplySearch()
{
	CString query;
	GetDlgItemText(IDC_CSF_BROWSER_SEARCH, query);
	query.Trim();
	query.MakeLower();

	m_VisibleRows.clear();
	m_VisibleRows.reserve(m_Rows.size());
	for(size_t i=0;i<m_Rows.size();++i)
	{
		if(query.IsEmpty() || m_Rows[i].searchText.Find(query)>=0)
			m_VisibleRows.push_back(i);
	}

	m_List.SetRedraw(FALSE);
	m_List.SetItemCountEx(static_cast<int>(m_VisibleRows.size()), LVSICF_NOSCROLL);
	m_List.SetRedraw(TRUE);
	m_List.Invalidate(FALSE);
	SetDlgItemText(IDC_CSF_BROWSER_PREVIEW, "");
	UpdateResultCount();
}

void CCsfBrowserDlg::UpdateResultCount()
{
	CString count;
	count.Format("%llu / %llu", static_cast<unsigned long long>(m_VisibleRows.size()),
		static_cast<unsigned long long>(m_Rows.size()));
	SetDlgItemText(IDC_CSF_BROWSER_COUNT, count);
}

void CCsfBrowserDlg::UpdatePreview(int visibleIndex)
{
	if(visibleIndex<0 || static_cast<size_t>(visibleIndex)>=m_VisibleRows.size())
	{
		SetDlgItemText(IDC_CSF_BROWSER_PREVIEW, "");
		return;
	}

	const Row& row=m_Rows[m_VisibleRows[visibleIndex]];
	CString preview=*row.id;
	preview+="\r\n";
	preview+=*row.value;
	SetDlgItemText(IDC_CSF_BROWSER_PREVIEW, preview);
}

void CCsfBrowserDlg::OnChangeSearch()
{
	KillTimer(SearchTimerId);
	SetTimer(SearchTimerId, SearchDelayMilliseconds, NULL);
}

void CCsfBrowserDlg::OnTimer(UINT_PTR nIDEvent)
{
	if(nIDEvent==SearchTimerId)
	{
		KillTimer(SearchTimerId);
		ApplySearch();
		return;
	}

	CDialog::OnTimer(nIDEvent);
}

void CCsfBrowserDlg::OnGetdispinfoList(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLVDISPINFO* info=reinterpret_cast<NMLVDISPINFO*>(pNMHDR);
	if((info->item.mask & LVIF_TEXT)!=0 && info->item.iItem>=0
		&& static_cast<size_t>(info->item.iItem)<m_VisibleRows.size())
	{
		const Row& row=m_Rows[m_VisibleRows[info->item.iItem]];
		const CString& text=info->item.iSubItem==0?*row.id:*row.value;
		lstrcpyn(info->item.pszText, text, info->item.cchTextMax);
	}

	*pResult=0;
}

void CCsfBrowserDlg::OnItemchangedList(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLISTVIEW* info=reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	if((info->uChanged & LVIF_STATE)!=0 && (info->uNewState & LVIS_SELECTED)!=0)
		UpdatePreview(info->iItem);
	*pResult=0;
}
