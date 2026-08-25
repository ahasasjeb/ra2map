/*
    FinalSun/FinalAlert 2 Mission Editor

    Copyright (C) 1999-2024 Electronic Arts, Inc.
    Authored by Matthias Wagner

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <vector>

class CCsfBrowserDlg : public CDialog
{
public:
	CCsfBrowserDlg(CWnd* pParent=NULL);

	enum { IDD=IDD_CSF_BROWSER };

protected:
	struct Row
	{
		const CString* id;
		const CString* value;
		CString searchText;
	};

	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnCancel();

	void BuildRows();
	void ApplySearch();
	void UpdateResultCount();
	void UpdatePreview(int visibleIndex);

	CListCtrl m_List;
	std::vector<Row> m_Rows;
	std::vector<size_t> m_VisibleRows;

	afx_msg void OnChangeSearch();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnGetdispinfoList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnItemchangedList(NMHDR* pNMHDR, LRESULT* pResult);

	DECLARE_MESSAGE_MAP()
};
