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

// Building.cpp: Implementierungsdatei
//

#include "stdafx.h"
#include "FinalSun.h"
#include "Building.h"
#include "mapdata.h"
#include "variables.h"
#include "functions.h"
#include "inlines.h"
#include "PropertyBrushTool.h"
#include "resource.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// Dialogfeld CBuilding 


CBuilding::CBuilding(CWnd* pParent /*=NULL*/)
	: CDialog(CBuilding::IDD, pParent)
{
	//{{AFX_DATA_INIT(CBuilding)
	m_direction = _T("");
	m_house = _T("");
	m_flag1 = _T("");
	m_flag2 = _T("");
	m_energy = _T("");
	m_upgradecount = _T("");
	m_spotlight = _T("");
	m_upgrade1 = _T("");
	m_upgrade2 = _T("");
	m_upgrade3 = _T("");
	m_flag3 = _T("");
	m_flag4 = _T("");
	m_tag = _T("");
	//}}AFX_DATA_INIT
	Init();
}


void CBuilding::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CBuilding)
	DDX_Control(pDX, IDC_STRENGTH, m_strength_ctrl);
	DDX_CBString(pDX, IDC_DIRECTION, m_direction);
	DDX_CBString(pDX, IDC_HOUSE, m_house);
	DDX_Text(pDX, IDC_P1, m_flag1);
	DDX_Text(pDX, IDC_P2, m_flag2);
	DDX_Text(pDX, IDC_P3, m_energy);
	DDX_Text(pDX, IDC_P4, m_upgradecount);
	DDX_Text(pDX, IDC_P5, m_spotlight);
	DDX_CBString(pDX, IDC_P6, m_upgrade1);
	DDX_CBString(pDX, IDC_P7, m_upgrade2);
	DDX_CBString(pDX, IDC_P8, m_upgrade3);
	DDX_Text(pDX, IDC_P9, m_flag3);
	DDX_Text(pDX, IDC_P10, m_flag4);
	DDX_CBString(pDX, IDC_TAG, m_tag);
	//}}AFX_DATA_MAP

	
}


BEGIN_MESSAGE_MAP(CBuilding, CDialog)
	//{{AFX_MSG_MAP(CBuilding)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// Behandlungsroutinen für Nachrichten CBuilding 

CString GetName(CString id)
{
	CIniFile& ini=Map->GetIniFile();
	if(ini.sections.find(id)!=ini.sections.end())
	{
		if(ini.sections[id].values.find("Name")!=ini.sections[id].values.end())
			return ini.sections[id].values["Name"];
		
	}
	return rules.sections[id].values["Name"];
}

namespace
{
	constexpr int PropertyBrushCheckIds[] = {
		IDC_PROPERTYBRUSH_CHECK1, IDC_PROPERTYBRUSH_CHECK2, IDC_PROPERTYBRUSH_CHECK3,
		IDC_PROPERTYBRUSH_CHECK4, IDC_PROPERTYBRUSH_CHECK5, IDC_PROPERTYBRUSH_CHECK6,
		IDC_PROPERTYBRUSH_CHECK7, IDC_PROPERTYBRUSH_CHECK8, IDC_PROPERTYBRUSH_CHECK9,
		IDC_PROPERTYBRUSH_CHECK10, IDC_PROPERTYBRUSH_CHECK11, IDC_PROPERTYBRUSH_CHECK12,
		IDC_PROPERTYBRUSH_CHECK13, IDC_PROPERTYBRUSH_CHECK14
	};

	void SelectComboItem(CDialog& dialog, int controlId, const CString& value)
	{
		auto* combo = static_cast<CComboBox*>(dialog.GetDlgItem(controlId));
		if (combo == nullptr)
			return;

		if (combo->SelectString(-1, value) == CB_ERR)
			combo->SetWindowText(value);
	}
}

void CBuilding::EnablePropertyBrush()
{
	m_propertyBrush = true;
	m_propertyBrushFields.fill(FALSE);
}

PropertyBrushSettings CBuilding::GetPropertyBrushSettings() const
{
	PropertyBrushSettings settings;
	settings.objectType = PropertyBrushObjectType::Structure;
	settings.values = { m_house, m_strength, m_direction, m_flag1, m_flag2, m_energy,
		m_upgradecount, m_spotlight, m_upgrade1, m_upgrade2, m_upgrade3, m_flag3, m_flag4, m_tag };
	for (size_t i = 0; i < m_propertyBrushFields.size(); ++i)
		settings.selected[i] = m_propertyBrushFields[i] != FALSE;
	settings.randomDirection = settings.selected[2] &&
		m_direction == GetLanguageStringACP("PropertyBrushRandomDirection");
	return settings;
}

bool CBuilding::HasSelectedPropertyBrushFields() const
{
	for (BOOL selected : m_propertyBrushFields)
		if (selected != FALSE)
			return true;
	return false;
}

void CBuilding::UpdatePropertyBrushControls()
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
		if (auto* direction = static_cast<CComboBox*>(GetDlgItem(IDC_DIRECTION)))
			direction->AddString(GetLanguageStringACP("PropertyBrushRandomDirection"));
	}
}

void CBuilding::OnOK() 
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

	char d[50];
	TruncSpace(m_spotlight);
	itoa(atoi(m_spotlight), d, 10);
	m_spotlight=d;
		
	m_house=TranslateHouse(m_house);
	TruncSpace(m_upgrade1);
	TruncSpace(m_upgrade2);
	TruncSpace(m_upgrade3);
	TruncSpace(m_tag);
	TruncSpace(m_flag1);
	TruncSpace(m_flag2);
	TruncSpace(m_flag3);
	TruncSpace(m_flag4);
	
	// 
}

BOOL CBuilding::OnInitDialog() 
{
	CDialog::OnInitDialog();
	
	CIniFile& ini=Map->GetIniFile();

	int i;
	CComboBox* chouse, *ctag;
	chouse=(CComboBox*)GetDlgItem(IDC_HOUSE);
	ctag=(CComboBox*)GetDlgItem(IDC_TAG);
	
	ListHouses(*chouse, FALSE);
	ListTags(*ctag, TRUE);
	
	
	
	m_strength_ctrl.SetRange(0,256);
	m_strength_ctrl.SetPos(atoi(m_strength));

	UpdateData(FALSE);

	int upgradecount=0;
	if(strcmp(m_type,"GACTWR")==NULL)
		upgradecount=1;


	if(ini.sections.find(m_type)!=ini.sections.end())
	{
		if(ini.sections[m_type].values.find("Upgrades")!=ini.sections[m_type].values.end())
		{
			// ok we have our upgrade
			upgradecount=atoi(ini.sections[m_type].values["Upgrades"]);
		}
		else
		{
			if(rules.sections[m_type].values.find("Upgrades")!=rules.sections[m_type].values.end())
				upgradecount=atoi(rules.sections[m_type].values["Upgrades"]);
		}
	}
	else
	{
		if(rules.sections[m_type].values.find("Upgrades")!=rules.sections[m_type].values.end())
				upgradecount=atoi(rules.sections[m_type].values["Upgrades"]);
	}

	
	if(upgradecount>0)
	{
		for(i=0;i<rules.sections["BuildingTypes"].values.size();i++)
		{
			const char* unitname=*rules.sections["BuildingTypes"].GetValue(i);

			// okay, first all the old units
			if(ini.sections.find(unitname)!=ini.sections.end())
			{
				// new thing specified
				if(ini.sections[unitname].values.find("PowersUpBuilding")!=ini.sections[unitname].values.end())
				{
					
					// ini file specified new PowersUpBuilding
					if(_stricmp(ini.sections[unitname].values["PowersUpBuilding"], m_type)==NULL)
					{
						((CComboBox*)GetDlgItem(IDC_P6))->AddString(((CString)unitname+" ("+GetName(unitname)+")"));
						((CComboBox*)GetDlgItem(IDC_P7))->AddString(((CString)unitname+" ("+GetName(unitname)+")"));
						((CComboBox*)GetDlgItem(IDC_P8))->AddString(((CString)unitname+" ("+GetName(unitname)+")"));
					}
				}
			}
			else
			{
				// ini did not specify thing specified
				if(rules.sections[unitname].values.find("PowersUpBuilding")!=rules.sections[unitname].values.end())
				{
					// rules file specified new PowersUpBuilding
					if(_stricmp(rules.sections[unitname].values["PowersUpBuilding"], m_type)==NULL)
					{
						((CComboBox*)GetDlgItem(IDC_P6))->AddString(((CString)unitname+" ("+GetName(unitname)+")"));
						((CComboBox*)GetDlgItem(IDC_P7))->AddString(((CString)unitname+" ("+GetName(unitname)+")"));
						((CComboBox*)GetDlgItem(IDC_P8))->AddString(((CString)unitname+" ("+GetName(unitname)+")"));
					}
				}
			}
		}
		if(ini.sections.find("BuildingTypes")!=ini.sections.end())
		{
			for(i=0;i<ini.sections["BuildingTypes"].values.size();i++)
			{
				const char* unitname=*ini.sections["BuildingTypes"].GetValue(i);

				// okay, first all the old units
				if(ini.sections.find(unitname)!=ini.sections.end())
				{
					// new thing specified
					if(ini.sections[unitname].values.find("PowersUpBuilding")!=ini.sections[unitname].values.end())
					{
						// ini file specified new PowersUpBuilding
						if(_stricmp(ini.sections[unitname].values["PowersUpBuilding"], m_type)==NULL)
						{
							((CComboBox*)GetDlgItem(IDC_P6))->AddString(((CString)unitname+" ("+GetName(unitname)+")"));
							((CComboBox*)GetDlgItem(IDC_P7))->AddString(((CString)unitname+" ("+GetName(unitname)+")"));
							((CComboBox*)GetDlgItem(IDC_P8))->AddString(((CString)unitname+" ("+GetName(unitname)+")"));
						}
					}
				}
				
			}
		}
	}

	GetDlgItem(IDC_P8)->EnableWindow(TRUE);
	GetDlgItem(IDC_P7)->EnableWindow(TRUE);
	GetDlgItem(IDC_P6)->EnableWindow(TRUE);
	GetDlgItem(IDC_P4)->EnableWindow(TRUE);

	if(upgradecount<3)
		GetDlgItem(IDC_P8)->EnableWindow(FALSE);
	if(upgradecount<2)
		GetDlgItem(IDC_P7)->EnableWindow(FALSE);
	if(upgradecount<1)
	{
		GetDlgItem(IDC_P6)->EnableWindow(FALSE);
		GetDlgItem(IDC_P4)->EnableWindow(FALSE);
	}
	

	UpdateStrings();

	// UpdateData(FALSE) writes directly into editable combo boxes. That leaves the
	// embedded edit controls with their text selected, making every value appear
	// blue until it receives focus. Select the corresponding list entries instead.
	SelectComboItem(*this, IDC_HOUSE, m_house);
	SelectComboItem(*this, IDC_DIRECTION, m_direction);
	SelectComboItem(*this, IDC_TAG, m_tag);
	SelectComboItem(*this, IDC_P2, m_flag2);
	SelectComboItem(*this, IDC_P3, m_energy);
	SelectComboItem(*this, IDC_P4, m_upgradecount);
	SelectComboItem(*this, IDC_P5, m_spotlight);
	SelectComboItem(*this, IDC_P6, m_upgrade1);
	SelectComboItem(*this, IDC_P7, m_upgrade2);
	SelectComboItem(*this, IDC_P8, m_upgrade3);
	SelectComboItem(*this, IDC_P9, m_flag3);
	SelectComboItem(*this, IDC_P10, m_flag4);
	UpdatePropertyBrushControls();
	
	return TRUE;  
}

void CBuilding::Init(CString house, CString strength, CString direction, CString tag, CString flag1, CString flag2, CString energy, CString upgradecount, CString spotlight, CString upgrade1, CString upgrade2, CString upgrade3, CString flag3, CString flag4)
{
	CIniFile& ini=Map->GetIniFile();

	if(house=="") 
	{
		house=TranslateHouse(Map->GetHouseID(0), TRUE);
	}
	else
		m_house=TranslateHouse(house, TRUE);
				

	

	m_house=TranslateHouse(house, TRUE);
	m_flag1=flag1;
	m_flag2=flag2;
	m_flag3=flag3;
	m_flag4=flag4;
	m_spotlight=spotlight;	
	m_energy=energy;
	m_upgrade1=upgrade1;
	m_upgrade2=upgrade2;
	m_upgrade3=upgrade3;
	m_upgradecount=upgradecount;
	m_strength=strength;
	m_tag=tag;
	m_direction=direction;

}

void CBuilding::UpdateStrings()
{
	ApplyEditorUIFont(this);

	SetWindowText(GetLanguageStringACP("StructCap"));
	GetDlgItem(IDC_LHOUSE)->SetWindowText(GetLanguageStringACP("StructHouse"));
	GetDlgItem(IDC_LDESC)->SetWindowText(GetLanguageStringACP("StructDesc"));
	GetDlgItem(IDC_LSTRENGTH)->SetWindowText(GetLanguageStringACP("StructStrength"));
	GetDlgItem(IDC_LDIRECTION)->SetWindowText(GetLanguageStringACP("StructDirection"));
	GetDlgItem(IDC_LTAG)->SetWindowText(GetLanguageStringACP("StructTag"));
	GetDlgItem(IDC_LAIREPAIRS)->SetWindowText(GetLanguageStringACP("StructAIRepairs"));
	GetDlgItem(IDC_LENERGY)->SetWindowText(GetLanguageStringACP("StructEnergy"));
	GetDlgItem(IDC_LUPGRADECOUNT)->SetWindowText(GetLanguageStringACP("StructUpgradeCount"));
	GetDlgItem(IDC_LSPOTLIGHT)->SetWindowText(GetLanguageStringACP("StructSpotlight"));
	GetDlgItem(IDC_LUPGRADE1)->SetWindowText(GetLanguageStringACP("StructUpgrade1"));
	GetDlgItem(IDC_LUPGRADE2)->SetWindowText(GetLanguageStringACP("StructUpgrade2"));
	GetDlgItem(IDC_LUPGRADE3)->SetWindowText(GetLanguageStringACP("StructUpgrade3"));
	GetDlgItem(IDC_LP1)->SetWindowText(GetLanguageStringACP("StructP1"));
	GetDlgItem(IDC_LP2)->SetWindowText(GetLanguageStringACP("StructP2"));
	GetDlgItem(IDC_LP3)->SetWindowText(GetLanguageStringACP("StructP3"));

	SetDlgItemText(IDOK, GetLanguageStringACP("OK"));
	SetDlgItemText(IDCANCEL, GetLanguageStringACP("Cancel"));
}
