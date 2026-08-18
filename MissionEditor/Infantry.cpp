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

// Infantrie.cpp: Implementierungsdatei
//

#include "stdafx.h"
#include "FinalSun.h"
#include "Infantry.h"
#include "functions.h"
#include "mapdata.h"
#include "variables.h"
#include "PropertyBrushTool.h"
#include "resource.h"



#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	constexpr int PropertyBrushCheckIds[] = {
		IDC_PROPERTYBRUSH_CHECK1, IDC_PROPERTYBRUSH_CHECK2, IDC_PROPERTYBRUSH_CHECK3,
		IDC_PROPERTYBRUSH_CHECK4, IDC_PROPERTYBRUSH_CHECK5, IDC_PROPERTYBRUSH_CHECK6,
		IDC_PROPERTYBRUSH_CHECK7, IDC_PROPERTYBRUSH_CHECK8, IDC_PROPERTYBRUSH_CHECK9,
		IDC_PROPERTYBRUSH_CHECK10
	};
}

/////////////////////////////////////////////////////////////////////////////
// Dialogfeld CInfantrie 


CInfantrie::CInfantrie(CWnd* pParent /*=NULL*/)
	: CDialog(CInfantrie::IDD, pParent)
{
	//{{AFX_DATA_INIT(CInfantrie)
	m_direction = _T("");
	m_house = _T("");
	m_flag1 = _T("");
	m_flag2 = _T("");
	m_flag3 = _T("");
	m_flag4 = _T("");
	m_action = _T("");
	m_tag = _T("");
	m_flag5 = _T("");
	//}}AFX_DATA_INIT
	Init();
}


void CInfantrie::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CInfantrie)
	DDX_Control(pDX, IDC_STRENGTH, m_strength_ctrl);
	DDX_CBString(pDX, IDC_DIRECTION, m_direction);
	DDX_CBString(pDX, IDC_HOUSE, m_house);
	DDX_Text(pDX, IDC_P1, m_flag1);
	DDX_Text(pDX, IDC_P2, m_flag2);
	DDX_Text(pDX, IDC_P3, m_flag3);
	DDX_Text(pDX, IDC_P4, m_flag4);
	DDX_CBString(pDX, IDC_STATE, m_action);
	DDX_CBString(pDX, IDC_TAG, m_tag);
	DDX_Text(pDX, IDC_P5, m_flag5);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CInfantrie, CDialog)
	//{{AFX_MSG_MAP(CInfantrie)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

void CInfantrie::EnablePropertyBrush()
{
	m_propertyBrush = true;
	m_propertyBrushFields.fill(FALSE);
}

PropertyBrushSettings CInfantrie::GetPropertyBrushSettings() const
{
	PropertyBrushSettings settings;
	settings.objectType = PropertyBrushObjectType::Infantry;
	settings.values = { m_house, m_strength, m_action, m_direction, m_flag1, m_flag2, m_flag3, m_flag4, m_flag5, m_tag };
	for (size_t i = 0; i < m_propertyBrushFields.size(); ++i)
		settings.selected[i] = m_propertyBrushFields[i] != FALSE;
	return settings;
}

bool CInfantrie::HasSelectedPropertyBrushFields() const
{
	for (BOOL selected : m_propertyBrushFields)
		if (selected != FALSE)
			return true;
	return false;
}

void CInfantrie::UpdatePropertyBrushControls()
{
	for (int controlId : PropertyBrushCheckIds)
	{
		if (auto* control = GetDlgItem(controlId))
			control->ShowWindow(m_propertyBrush ? SW_SHOW : SW_HIDE);
	}
	if (m_propertyBrush)
	{
		SetWindowText(GetLanguageStringACP("PropertyBrushCap"));
		SetDlgItemText(IDC_LDESC, GetLanguageStringACP("PropertyBrushDesc"));
	}
}

/////////////////////////////////////////////////////////////////////////////
// Behandlungsroutinen für Nachrichten CInfantrie 

BOOL CInfantrie::OnInitDialog() 
{
	CDialog::OnInitDialog();
	
	// init the common (!) dialog things
	int i;
	CComboBox* house, *tag;
	house=(CComboBox*)GetDlgItem(IDC_HOUSE);
	tag=(CComboBox*)GetDlgItem(IDC_TAG);
	
	ListHouses(*house, FALSE);
	ListTags(*tag, TRUE);
	
	
	UpdateData(FALSE);
	m_strength_ctrl.SetRange(0,256);
	m_strength_ctrl.SetPos(atoi(m_strength));

	
	UpdateStrings();
	UpdatePropertyBrushControls();

	return TRUE;  
}

void CInfantrie::OnOK() 
{
	if (m_propertyBrush)
	{
		for (size_t i = 0; i < m_propertyBrushFields.size(); ++i)
			m_propertyBrushFields[i] = IsDlgButtonChecked(PropertyBrushCheckIds[i]);
		if (!HasSelectedPropertyBrushFields())
		{
			AfxMessageBox(GetLanguageStringACP("PropertyBrushNoFields"));
			return;
		}
	}

	CDialog::OnOK();
	m_strength=GetText(&m_strength_ctrl);

	UpdateData();	
	TruncSpace(m_tag);
	m_house=TranslateHouse(m_house);
	

	
}

void CInfantrie::Init(CString house, CString strength, CString action, CString direction, CString tag, CString flag1, CString flag2, CString flag3, CString flag4, CString flag5)
{
	CIniFile& ini=Map->GetIniFile();

	if(house=="") 
	{
		/*m_house=*rules.sections[HOUSES].GetValue(0);
		if(ini.sections.find(HOUSES)!=ini.sections.end())
			if(ini.sections[HOUSES].values.size()>0)
				m_house=*ini.sections[HOUSES].GetValue(0);*/
		m_house=TranslateHouse(Map->GetHouseID(0), TRUE);
	}
	else
		m_house=TranslateHouse(house, TRUE);

	

	m_flag1=flag1;
	m_flag2=flag2;
	m_flag3=flag3;
	m_flag4=flag4;
	m_flag5=flag5;
	// m_pos=pos;
	m_action=action;
	m_strength=strength;
	
	m_tag=tag;
	m_direction=direction;
	
}

void CInfantrie::UpdateStrings()
{
	ApplyEditorUIFont(this);

	SetWindowText(GetLanguageStringACP("InfCap"));
	GetDlgItem(IDC_LHOUSE)->SetWindowText(GetLanguageStringACP("InfHouse"));
	GetDlgItem(IDC_LDESC)->SetWindowText(GetLanguageStringACP("InfDesc"));
	GetDlgItem(IDC_LSTRENGTH)->SetWindowText(GetLanguageStringACP("InfStrength"));
	// GetDlgItem(IDC_LPOS)->SetWindowText(GetLanguageStringACP("InfPos"));
	GetDlgItem(IDC_LSTATE)->SetWindowText(GetLanguageStringACP("InfState"));
	GetDlgItem(IDC_LDIRECTION)->SetWindowText(GetLanguageStringACP("InfDirection"));
	GetDlgItem(IDC_LTAG)->SetWindowText(GetLanguageStringACP("InfTag"));
	GetDlgItem(IDC_LP1)->SetWindowText(GetLanguageStringACP("InfP1"));
	GetDlgItem(IDC_LP2)->SetWindowText(GetLanguageStringACP("InfP2"));
	GetDlgItem(IDC_LP3)->SetWindowText(GetLanguageStringACP("InfP3"));
	GetDlgItem(IDC_LP4)->SetWindowText(GetLanguageStringACP("InfP4"));
	GetDlgItem(IDC_LP5)->SetWindowText(GetLanguageStringACP("InfP5"));

	SetDlgItemText(IDOK, GetLanguageStringACP("OK"));
	SetDlgItemText(IDCANCEL, GetLanguageStringACP("Cancel"));
}
