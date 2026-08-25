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

// UserScriptsDlg.cpp: Implementierungsdatei
//

#include "stdafx.h"
#include "finalsun.h"
#include "UserScriptsDlg.h"
#include <io.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <array>
#include <vector>
#include "variables.h"
#include "functions.h"
#include "inlines.h"
#include "combouinputdlg.h"
#include "RustCore.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


using namespace std;

struct FunctionData
{
	CString name;
	std::vector<CString> params;
	int sourceLine=1;

	void AddParam()
	{
		params.emplace_back();
	}
};

struct JumpLineData
{
	CString name;
	int line;
};

class CUserScript  
{
public:
	void RaiseErr(int n, const char* str);
	char errortext[250];
	int error;
	int functioncount;
	int GetFunction(int index, CString* name, std::vector<CString>* params) const;
	int GetSourceLine(int index) const;
	int FindJumpLine(const CString& name) const;
	int LoadFile(const char* filename);
	CUserScript();
	~CUserScript() = default;

private:
	int AllocateFunction();
	std::vector<FunctionData> functiondata;
	map<CString, int> jumplinedata;


	CString filename;
};

CUserScript::CUserScript()
{
	functioncount = 0;
}

int CUserScript::FindJumpLine(const CString& name) const
{
	if(jumplinedata.find(name)==jumplinedata.end()) return -1;
	return jumplinedata.at(name);
}

int CUserScript::LoadFile(const char *setupfile)
{
	filename=setupfile;
	
	int file=_open(setupfile, _O_RDONLY);
	if(file==-1)
	{
		MessageBox(0, TranslateStringACP("Error opening script file"), TranslateStringACP("Error"), MB_ICONERROR);
		return -1;
	}

	int parsepos=0, filesize=0;
	BOOL inFunction=FALSE;
	BOOL inParam=FALSE;
	BOOL inComment=FALSE;
	BOOL inFunctionHead=FALSE;
	BOOL inNewOrder=TRUE;
	BOOL inJumpLine=FALSE;
	int sourceLine=1;

	
	
	while(!(_eof(file)))
	{
		unsigned char c;
		int res=_read(file,(void*) &c, 1);
		if(res>0) filesize++; 
	}

	_lseek(file, 0, SEEK_SET);
	std::vector<unsigned char> data(filesize + 5);
	data[filesize]=0;
	for (parsepos=0;parsepos<filesize;parsepos++)
	{
		_read(file, data.data() + parsepos, 1);
	}
	_close(file);
		
    //MessageBox(0,(char*)data,"DEBUG: SETUPSCRIPT:/",0);

	std::array<BYTE, 512> jumplinename{};
	// (the original memset had value/size swapped and was a no-op,
	// leaving the buffer uninitialized for the first strcat)
	jumplinename.fill(0);

	//// MAIN STUFF HERE ////
	for(parsepos=0;parsepos<filesize;parsepos++)
	{
		

		if(inComment==TRUE || inNewOrder==FALSE)
		{



			if(data[parsepos]=='\n')
			{
				//MessageBox(0, "NEWLINE", (char*)&data[parsepos+1], 0);
				inNewOrder=TRUE;
				inComment=FALSE;
				inJumpLine=FALSE;
				jumplinename.fill(0);
				
			}
		}
		
		else if(inFunction==FALSE && inComment==FALSE && inFunctionHead==FALSE && inJumpLine==FALSE)
		{
			// easy here, just seek for new position

			if(data[parsepos]=='/' && data[parsepos+1]=='/')
			{
				inComment=TRUE;
			}
			else if(data[parsepos]==';')
			{
				inNewOrder=TRUE;
			}
			else if(data[parsepos]==':')
			{
				inJumpLine=TRUE;
			}
			else if(IsCharAlphaNumeric(data[parsepos])!=0 && inNewOrder==TRUE && inJumpLine==FALSE)
			{
				inFunction=TRUE;
				int pos=AllocateFunction();
				functiondata[pos-1].sourceLine=sourceLine;
				//*functiondata[pos-1].name.append(data[parsepos]);
				functiondata[pos-1].name = static_cast<char>(data[parsepos]);
			}

		}
		else if(data[parsepos]=='(' && inComment==FALSE && inParam==FALSE)
		{
			//MessageBox(0, "InHead","",0);
			inFunctionHead=TRUE;
		}
		else if(inFunctionHead==TRUE && inParam==TRUE)
		{
			if(data[parsepos]=='\\')
			{
				if(data[parsepos+1]=='n' || data[parsepos+1]=='N')
				{
					data[parsepos]='\n';
					functiondata[functioncount-1].params.back() += data[parsepos];
					parsepos++;
				}
				if(data[parsepos+1]=='r' || data[parsepos+1]=='R')
				{
					data[parsepos]='\r';
					functiondata[functioncount-1].params.back() += data[parsepos];
					parsepos++;
				}
			}

			else if(data[parsepos]=='"' && data[parsepos+1]!='"')
			{
				inParam=FALSE;
				//MessageBox(0, functiondata[functioncount-1].param[functiondata[functioncount-1].paramcount-1].data(), functiondata[functioncount-1].name->data(), 0);


			}
			else
			{

				// add character to param
				functiondata[functioncount-1].params.back() += data[parsepos];
				if(data[parsepos]=='"' && data[parsepos+1]=='"') parsepos++; // ignore the next "


			}
		}
		else if(inFunction==TRUE && inParam==FALSE && inFunctionHead==FALSE)
		{
			if(IsCharAlphaNumeric(data[parsepos])!=0)
			{
				functiondata[functioncount-1].name += data[parsepos];
			}
			else
			{
				inFunction=FALSE;
				//MessageBox(0, functiondata[functioncount-1].name->data(), "FUNCTIONAME", 0);
			}
		}

		else if(inFunctionHead==TRUE && inParam==FALSE)
		{
			if(data[parsepos]=='"')
			{
				// add a param!
				inParam=TRUE;
				functiondata[functioncount-1].AddParam();
				functiondata[functioncount-1].params.back() = "";
			}
			if(data[parsepos]==')')
			{
				inFunctionHead=FALSE;
				//MessageBox(0, (char*)&data[parsepos], "", 0);
			}
		}

		else if(inJumpLine==TRUE)
		{
			if(data[parsepos]!=':')
			{
				// guard the fixed 512-byte label buffer against
				// overlong jump line labels in user scripts
				if (strlen(reinterpret_cast<char*>(jumplinename.data())) + 1 < jumplinename.size())
				{
					char d[2];
					d[0]=data[parsepos];
					d[1]=0;
					strcat(reinterpret_cast<char*>(jumplinename.data()), d);
				}
			}
			else
			{
				jumplinedata[reinterpret_cast<char*>(jumplinename.data())]=functioncount;
				//MessageBox(0,(char*)jumplinename,"",0);
				jumplinename.fill(0);
				inJumpLine=FALSE;
				inNewOrder=TRUE;
			}
		}
		

		if(data[parsepos]=='\n') sourceLine++;

	}
	/////////////////////////



		
	return 0;
}

int CUserScript::GetFunction(int index, CString* name, std::vector<CString>* params) const
{
	if(index<0 || index>=functioncount) return -1;

	*name = functiondata[index].name;
	*params = functiondata[index].params;
	return 0;
}

int CUserScript::GetSourceLine(int index) const
{
	if(index<0 || index>=functioncount) return index;
	return functiondata[index].sourceLine;
}

int CUserScript::AllocateFunction()
{
	functiondata.emplace_back();
	++functioncount;
	return functioncount;
}

void CUserScript::RaiseErr(int n, const char *str)
{

}

/////////////////////////////////////////////////////////////////////////////
// Dialogfeld CUserScriptsDlg 


CUserScriptsDlg::CUserScriptsDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CUserScriptsDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CUserScriptsDlg)
	m_Script = _T("");
	m_Report = _T("");
	m_Source = _T("");
	//}}AFX_DATA_INIT
	m_loadingSource=FALSE;
}


void CUserScriptsDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CUserScriptsDlg)
	DDX_LBString(pDX, IDC_SCRIPTS, m_Script);
	DDX_Text(pDX, IDC_REPORT, m_Report);
	DDX_Text(pDX, IDC_SCRIPT_EDITOR, m_Source);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CUserScriptsDlg, CDialog)
	//{{AFX_MSG_MAP(CUserScriptsDlg)
	ON_LBN_SELCHANGE(IDC_SCRIPTS, OnSelchangeScripts)
	ON_EN_CHANGE(IDC_SCRIPT_EDITOR, OnChangeScriptEditor)
	ON_BN_CLICKED(IDC_SCRIPT_SAVE, OnSaveScript)
	ON_BN_CLICKED(IDC_SCRIPT_NEW, OnNewScript)
	ON_BN_CLICKED(IDC_SCRIPT_COPY_API, OnCopyApiMarkdown)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// Behandlungsroutinen für Nachrichten CUserScriptsDlg 

#define ASK_CONTINUE "AskContinue"
#define ADD_TRIGGER "AddTrigger"
#define MESSAGE "Message"
#define SET_INI_KEY "SetIniKey"
#define SET_SAFE_MODE "SetSafeMode"
#define SET_VARIABLE "SetVariable"
#define SET_VARIABLE_IF "SetVariableIf"
#define ADD "Add"
#define MULTI "Multi"
#define SUBSTRACT "Substract"
#define DIVIDE "Divide"
#define SET_WAYPOINT "SetWaypoint"
#define REQUIRES_MP "RequiresMP"
#define REQUIRES_SP "RequiresSP"
#define ADD_AI_TRIGGER "AddAITrigger"
#define ADD_TAG "AddTag"
#define RESIZE "Resize"
#define IS "Is"
#define CANCEL "Cancel"
#define PRINT "Print"
#define TOLOWER "LowerCase"
#define TOUPPER "UpperCase"
#define GET_FREE_WAYPOINT "GetFreeWaypoint"
#define UINPUT_GET_INTEGER "UInputGetInteger"
#define UINPUT_GET_STRING "UInputGetString"
#define JUMP_TO_LINE "JumpTo"
#define SET_AUTO_UPDATE "SetAutoUpdate"
#define GET_RANDOM "GetRandom"
#define ADD_TERRAIN "AddTerrain"
#define GET_INI_KEY "GetIniKey"
#define MODULO "Mod"
#ifdef SMUDGE_SUPP
#define ADD_SMUDGE "AddSmudge"
#endif
#define INSERT "Insert"
#define LENGTH "Length"
#define TRIM "Trim"
#define GETCHAR "GetChar"
#define REPLACE "Replace"
#define REMOVE "Remove"
#define GET_WAYPOINT_POS "GetWaypointPos"
#define GET_PARAM "GetParam"
#define SET_PARAM "SetParam"
#define GET_PARAM_COUNT "GetParamCount"
#define ALLOW_DELETE "AllowDelete"
#define DELETE_TERRAIN "DeleteTerrain"
#define DELETE_INFANTRY "DeleteInfantry"
#define DELETE_AIRCRAFT "DeleteAircraft"
#define DELETE_STRUCTURE "DeleteStructure"
#define DELETE_VEHICLE "DeleteVehicle"
#define IS_INFANTRY_DELETED "IsInfantryDeleted"
#define IS_TERRAIN_DELETED "IsTerrainDeleted"
#define ADD_INFANTRY "AddInfantry"
#define ALLOW_ADD "AllowAdd"
#define ADD_VEHICLE "AddVehicle"
#define ADD_AIRCRAFT "AddAircraft"
#define ADD_STRUCTURE "AddStructure"
#define GET_INFANTRY "GetInfantry"
#define GET_AIRCRAFT "GetAircraft"
#define GET_STRUCTURE "GetStructure"
#define GET_VEHICLE "GetVehicle"
#define UINPUT_GET_HOUSE "UInputGetHouse"
#define UINPUT_GET_COUNTRY "UInputGetCountry"
#define UINPUT_GET_TRIGGER "UInputGetTrigger"
#define UINPUT_GET_TAG "UInputGetTag"
#define MESSAGE_YES_NO "Ask"
#define GET_HOUSE "GetHouse"
#define GET_COUNTRY "GetCountry"
#define GET_HOUSE_INDEX "GetHouseIndex"
#define OR "Or"
#define AND "And"
#define NOT "Not"
#define UINPUT_GET_MANUAL "UInputSelect"

#define ID_ASK_CONTINUE 0
#define ID_ADD_TRIGGER 1
#define ID_MESSAGE 2
#define ID_SET_INI_KEY 3
#define ID_SET_SAFE_MODE 4
#define ID_SET_VARIABLE 5
#define ID_SET_VARIABLE_IF 6
#define ID_ADD 7
#define ID_MULTI 8
#define ID_SUBSTRACT 9
#define ID_DIVIDE 10
#define ID_SET_WAYPOINT 11
#define ID_REQUIRES_MP 12
#define ID_REQUIRES_SP 13
#define ID_ADD_AI_TRIGGER 14
#define ID_ADD_TAG 15
#define ID_RESIZE 16
#define ID_IS 17
#define ID_CANCEL 18
#define ID_PRINT 19
#define ID_TOLOWER 20
#define ID_TOUPPER 21
#define ID_GET_FREE_WAYPOINT 22
#define ID_UINPUT_GET_INTEGER 23
#define ID_UINPUT_GET_STRING 24
#define ID_JUMP_TO_LINE 25
#define ID_SET_AUTO_UPDATE 26
#define ID_GET_RANDOM 27
#define ID_ADD_TERRAIN 28
#define ID_GET_INI_KEY 29
#define ID_MODULO 30
#ifdef SMUDGE_SUPP
#define ID_ADD_SMUDGE 31
#endif
#define ID_INSERT 32
#define ID_LENGTH 33
#define ID_TRIM 34
#define ID_GETCHAR 35
#define ID_REPLACE 36
#define ID_REMOVE 37
#define ID_GET_WAYPOINT_POS 38
#define ID_GET_PARAM 39
#define ID_SET_PARAM 40
#define ID_GET_PARAM_COUNT 41
#define ID_ALLOW_DELETE 42
#define ID_DELETE_TERRAIN 43
#define ID_DELETE_INFANTRY 44
#define ID_DELETE_STRUCTURE 45
#define ID_DELETE_AIRCRAFT 46
#define ID_DELETE_VEHICLE 47
#define ID_IS_INFANTRY_DELETED 48
#define ID_IS_TERRAIN_DELETED 49
#define ID_ADD_INFANTRY 50
#define ID_ALLOW_ADD 51
#define ID_ADD_VEHICLE 52
#define ID_ADD_AIRCRAFT 53
#define ID_ADD_STRUCTURE 54
#define ID_GET_INFANTRY 55
#define ID_GET_AIRCRAFT 56
#define ID_GET_STRUCTURE 57
#define ID_GET_VEHICLE 58
#define ID_UINPUT_GET_HOUSE 59
#define ID_UINPUT_GET_COUNTRY 60
#define ID_UINPUT_GET_TRIGGER 61
#define ID_UINPUT_GET_TAG 62
#define ID_MESSAGE_YES_NO 63
#define ID_GET_HOUSE 64
#define ID_GET_COUNTRY 65
#define ID_GET_HOUSE_INDEX 66
#define ID_OR 67
#define ID_AND 68
#define ID_NOT 69
#define ID_UINPUT_GET_MANUAL 70


extern CString GetFree(const char* section);

BOOL IsValSet(CString val)
{
	val.MakeLower();
	if(val=="false" || val=="no") return FALSE; // val is not set
	if(val=="true" || val=="yes") return TRUE;
	if(atoi(val)) return TRUE;
	return FALSE;
}

int get_player_count()
{
	if(Map->IsMultiplayer()==FALSE) return 1;

	int i;
	int wp_count=0;
	for(i=0;i<Map->GetWaypointCount();i++)
	{
		CString id;
		DWORD pos;
		Map->GetWaypointData(i, &id, &pos);
		int idi;
		idi=atoi(id);
		if(idi!=i) break;
		if(idi>=0 && idi<8) 
		{
			wp_count++;	
		}		
	}

	return wp_count;
}

namespace
{
	constexpr int LUA_MUTATE_SET=0;
	constexpr int LUA_MUTATE_REMOVE_KEY=1;
	constexpr int LUA_MUTATE_CLEAR_SECTION=2;
	constexpr int LUA_MUTATE_REMOVE_SECTION=3;
	constexpr int LUA_MAX_NAME_LENGTH=255;
	constexpr int LUA_MAX_VALUE_LENGTH=1024 * 1024;
	constexpr int LUA_MAX_REPORT_LENGTH=1024 * 1024;

	struct LuaScriptContext
	{
		CMapData* map=NULL;
		CIniFile ini;
		CString report;
		BOOL changed=FALSE;
		BOOL reportTruncated=FALSE;
	};

	BOOL IsValidLuaIniName(const CString& value, BOOL section)
	{
		if(value.IsEmpty() || value.GetLength()>LUA_MAX_NAME_LENGTH) return FALSE;
		if(value.FindOneOf(section ? "\r\n[]" : "\r\n=")>=0) return FALSE;
		if(section && value.CompareNoCase("$editor")==0) return FALSE;
		return TRUE;
	}

	BOOL IsProtectedLuaIniMutation(const CString& section, const CString* key)
	{
		if(section.CompareNoCase("IsoMapPack5")==0 ||
			section.CompareNoCase("OverlayPack")==0 ||
			section.CompareNoCase("OverlayDataPack")==0 ||
			section.CompareNoCase("PreviewPack")==0)
			return TRUE;

		if(section.CompareNoCase("Map")!=0) return FALSE;
		if(key==NULL) return TRUE;
		return key->CompareNoCase("Size")==0 ||
			key->CompareNoCase("LocalSize")==0 ||
			key->CompareNoCase("Theater")==0;
	}

	CString GetLuaEditorValue(LuaScriptContext* context, const CString& key)
	{
		CString value;
		if(key=="width") value.Format("%d", context->map->GetWidth());
		else if(key=="height") value.Format("%d", context->map->GetHeight());
		else if(key=="iso_size") value.Format("%d", context->map->GetIsoSize());
		else if(key=="waypoint_count") value.Format("%d", context->map->GetWaypointCount());
		else if(key=="unit_count") value.Format("%d", context->map->GetUnitCount());
		else if(key=="infantry_count") value.Format("%d", context->map->GetInfantryCount());
		else if(key=="structure_count") value.Format("%d", context->map->GetStructureCount());
		else if(key=="aircraft_count") value.Format("%d", context->map->GetAircraftCount());
		else if(key=="terrain_count") value.Format("%d", context->map->GetTerrainCount());
		else if(key=="player_count") value.Format("%d", get_player_count());
		else if(key=="house_count") value.Format("%d", context->map->GetHousesCount(FALSE));
		else if(key=="country_count") value.Format("%d", context->map->GetHousesCount(TRUE));
		else if(key=="theater") value=context->map->GetTheater();
		else if(key=="multiplayer") value=context->map->IsMultiplayer() ? "1" : "0";
		return value;
	}

	int UserLuaGet(void* opaque, const char* sectionText, const char* keyText,
		char* dst, size_t dstCap, size_t* outLength)
	{
		if(opaque==NULL || sectionText==NULL || keyText==NULL || outLength==NULL) return -1;
		LuaScriptContext* context=(LuaScriptContext*)opaque;
		const CString section=sectionText;
		const CString key=keyText;
		CString value;

		if(section.CompareNoCase("$editor")==0)
		{
			value=GetLuaEditorValue(context, key);
			if(value.IsEmpty() && key!="theater") return 0;
		}
		else
		{
			const auto sectionIt=context->ini.sections.find(section);
			if(sectionIt==context->ini.sections.end()) return 0;
			const auto valueIt=sectionIt->second.values.find(key);
			if(valueIt==sectionIt->second.values.end()) return 0;
			value=valueIt->second;
		}

		*outLength=static_cast<size_t>(value.GetLength());
		if(dst==NULL) return 1;
		if(dstCap<*outLength) return -1;
		if(*outLength>0) memcpy(dst, (LPCSTR)value, *outLength);
		return 1;
	}

	size_t UserLuaList(void* opaque, const char* sectionText, char* dst, size_t dstCap)
	{
		if(opaque==NULL) return 0;
		LuaScriptContext* context=(LuaScriptContext*)opaque;
		size_t required=0;

		if(sectionText==NULL)
		{
			for(const auto& section : context->ini.sections)
				required += static_cast<size_t>(section.first.GetLength()) + 1;
		}
		else
		{
			const auto sectionIt=context->ini.sections.find(CString(sectionText));
			if(sectionIt==context->ini.sections.end()) return 0;
			for(const auto& value : sectionIt->second.values)
				required += static_cast<size_t>(value.first.GetLength()) + 1;
		}

		if(dst==NULL) return required;
		if(dstCap<required) return 0;

		size_t offset=0;
		auto copyName=[&](const CString& name)
		{
			const size_t length=static_cast<size_t>(name.GetLength());
			if(length>0) memcpy(dst + offset, (LPCSTR)name, length);
			offset += length;
			dst[offset++]=0;
		};

		if(sectionText==NULL)
		{
			for(const auto& section : context->ini.sections) copyName(section.first);
		}
		else
		{
			const auto sectionIt=context->ini.sections.find(CString(sectionText));
			for(const auto& value : sectionIt->second.values) copyName(value.first);
		}
		return required;
	}

	int UserLuaMutate(void* opaque, int operation, const char* sectionText,
		const char* keyText, const char* valueText)
	{
		if(opaque==NULL || sectionText==NULL) return 0;
		LuaScriptContext* context=(LuaScriptContext*)opaque;
		const CString section=sectionText;
		if(!IsValidLuaIniName(section, TRUE)) return 0;

		if(operation==LUA_MUTATE_SET)
		{
			if(keyText==NULL || valueText==NULL) return 0;
			const CString key=keyText;
			const CString value=valueText;
			if(!IsValidLuaIniName(key, FALSE) || value.GetLength()>LUA_MAX_VALUE_LENGTH || value.FindOneOf("\r\n")>=0)
				return 0;
			if(IsProtectedLuaIniMutation(section, &key)) return 0;
			auto& values=context->ini.sections[section].values;
			const auto existing=values.find(key);
			if(existing==values.end() || existing->second!=value)
			{
				values[key]=value;
				context->changed=TRUE;
			}
			return 1;
		}

		if(operation==LUA_MUTATE_REMOVE_KEY)
		{
			if(keyText==NULL) return 0;
			const CString key=keyText;
			if(!IsValidLuaIniName(key, FALSE)) return 0;
			if(IsProtectedLuaIniMutation(section, &key)) return 0;
			const auto sectionIt=context->ini.sections.find(section);
			if(sectionIt!=context->ini.sections.end() && sectionIt->second.values.erase(key)>0)
				context->changed=TRUE;
			return 1;
		}

		if(operation==LUA_MUTATE_CLEAR_SECTION)
		{
			if(IsProtectedLuaIniMutation(section, NULL)) return 0;
			const auto sectionIt=context->ini.sections.find(section);
			if(sectionIt!=context->ini.sections.end() && !sectionIt->second.values.empty())
			{
				sectionIt->second.values.clear();
				sectionIt->second.value_orig_pos.clear();
				context->changed=TRUE;
			}
			return 1;
		}

		if(operation==LUA_MUTATE_REMOVE_SECTION)
		{
			if(IsProtectedLuaIniMutation(section, NULL)) return 0;
			if(context->ini.sections.erase(section)>0) context->changed=TRUE;
			return 1;
		}

		return 0;
	}

	void UserLuaPrint(void* opaque, const char* text)
	{
		if(opaque==NULL || text==NULL) return;
		LuaScriptContext* context=(LuaScriptContext*)opaque;
		if(context->reportTruncated) return;

		const CString line=text;
		if(context->report.GetLength() + line.GetLength() + 2>LUA_MAX_REPORT_LENGTH)
		{
			context->report += TranslateStringACP("Lua report truncated after 1 MiB.");
			context->report += "\r\n";
			context->reportTruncated=TRUE;
			return;
		}
		context->report += line;
		context->report += "\r\n";
	}
}

static const char kUserScriptApiMarkdown[] = R"fscriptmd(# Map Script API

## Lua 5.5 scripts (`.lua`)

New scripts use embedded Lua 5.5. Map edits are transactional: the editor runs the script against a temporary INI copy and asks before applying successful changes. A syntax or runtime error discards the copy.

```lua
print("Map:", map.get("Basic", "Name", "Unnamed"))
print("Size:", map.info.width, map.info.height)

for _, key in ipairs(map.keys("Waypoints")) do
    print("Waypoint", key, map.get("Waypoints", key))
end

map.set("Basic", "Author", "Lua script")
```

### Lua API

| API | Description |
| --- | --- |
| `print(...)` | Append tab-separated values to the report. |
| `map.get(section, key[, default])` | Return an INI value, the optional default, or `nil`. |
| `map.has(section, key)` | Return whether an INI key exists. |
| `map.set(section, key, value)` | Set a string, number, or Boolean INI value in the transaction. |
| `map.remove(section, key)` | Remove a key and return whether it existed. |
| `map.sections()` | Return a sorted array of section names. |
| `map.keys(section)` | Return a sorted array of keys in a section. |
| `map.section(section)` | Return a table containing all key/value pairs in a section. |
| `map.replace_section(section, values)` | Replace a section with a Lua table of key/value pairs. |
| `map.clear_section(section)` | Remove every key while keeping the section. |
| `map.remove_section(section)` | Remove a complete section. |
| `map.info` | Read-only-at-runtime metadata table described below. |
| `map.api_version` | Lua map API version; currently `1`. |

`map.info` contains `width`, `height`, `iso_size`, `theater`, `multiplayer`, `waypoint_count`, `unit_count`, `infantry_count`, `structure_count`, `aircraft_count`, `terrain_count`, `player_count`, `house_count`, and `country_count`.

The runtime loads Lua's base, table, string, math, and UTF-8 facilities. Filesystem, process, native module, package, dynamic-code, coroutine, and debug access are unavailable (`os`, `io`, `package`, `require`, `dofile`, `loadfile`, `load`, `coroutine`, and `debug`). Each run is limited to 64 MiB of Lua memory and 20 million VM instructions. INI section/key names and values must be single-line text; values are limited to 1 MiB. Packed map sections and the `[Map]` geometry/theater keys are read-only because changing them requires specialized resizing or theater-reload operations.

## Legacy scripts (`.fscript`)

Map scripts run against the currently open map. Save the map before running a script: script changes cannot be undone.

### Syntax

```text
// A comment
SetVariable("%Counter%", "10");
Print("Counter: %Counter%");

:Loop:
Substract("%Counter%", "1");
JumpTo("Loop", "%Counter%");
```

- Commands and variable names are case-sensitive.
- Parameters are quoted strings. Use `\n` and `\r` for line breaks, and `""` for a literal quote.
- Most commands accept an optional final `condition`. The command runs when the value is `true`, `yes`, or a non-zero number.
- Output parameters are variable names such as `%Result%`. Other parameters expand `%Variable%` before the command runs.
- Labels use `:Name:` and are targets for `JumpTo`.

### Built-in variables

`%Width%`, `%Height%`, `%IsoSize%`, `%WaypointCount%`, `%UnitCount%`, `%InfantryCount%`, `%StructureCount%`, `%AircraftCount%`, `%TerrainCount%`, `%Theater%`, `%PlayerCount%`, `%HousesCount%`, `%CountriesCount%`, `%DeleteAllowed%`, `%AddAllowed%`, `%SafeMode%`.

### Flow, messages, and variables

| API | Description |
| --- | --- |
| `Print(text[, condition])` | Append a line to the report. |
| `Message(text, title[, condition])` | Show an information message. |
| `Ask(%result%, text, title[, condition])` | Show a Yes/No message and store `1` or `0`. |
| `AskContinue(message[, condition])` | Ask whether the script should continue. |
| `Cancel([condition])` | Stop the script. |
| `RequiresMP([condition])` | Stop unless the current map is multiplayer. |
| `RequiresSP([condition])` | Stop unless the current map is single-player. |
| `JumpTo(label[, condition])` | Jump to a label. A safety prompt appears after every 300 loops. |
| `Is(left, operator, right, %result%[, condition])` | Compare using `<`, `<=`, `=`, `>=`, `>`, or `!=`; store `1` or `0`. |
| `And(%result%, value, ...)` | Store whether every value is set. |
| `Or(%result%, value, ...)` | Store whether any value is set. |
| `Not(%variable%[, condition])` | Toggle a Boolean variable. |
| `SetVariable(%variable%, value[, condition])` | Assign a string value. |
| `Add(%variable%, number[, condition])` | Add an integer. |
| `Substract(%variable%, number[, condition])` | Subtract an integer. The historical API spelling is `Substract`. |
| `Multi(%variable%, number[, condition])` | Multiply an integer. |
| `Divide(%variable%, number[, condition])` | Divide an integer. |
| `Mod(%variable%, number[, condition])` | Store the integer remainder. |
| `GetRandom(%result%[, condition])` | Store a random integer from 0 through 32767. |
| `LowerCase(%variable%[, condition])` | Convert a variable to lower case. |
| `UpperCase(%variable%[, condition])` | Convert a variable to upper case. |
| `Length(%result%, text[, condition])` | Store the text length. |
| `Trim(%variable%[, condition])` | Remove leading and trailing whitespace. |
| `Insert(%variable%, text, index[, condition])` | Insert text; a negative index appends. |
| `Replace(%variable%, old, replacement[, condition])` | Replace every matching substring. |
| `Remove(%variable%, index, length[, condition])` | Remove a substring. |
| `GetChar(%result%, text, index[, condition])` | Store one character. |
| `GetParam(%result%, csv, index[, condition])` | Read a zero-based comma-separated field. |
| `SetParam(%variable%, index, value[, condition])` | Replace a zero-based comma-separated field. |
| `GetParamCount(%result%, csv[, condition])` | Count comma-separated fields. |
| `SetAutoUpdate(enabled[, condition])` | Enable or disable live report refresh while running. |

### User input

| API | Description |
| --- | --- |
| `UInputGetInteger(%result%, prompt, minimum, maximum[, condition])` | Repeatedly ask for an integer in the optional bounds. Use an empty bound for none. |
| `UInputGetString(%result%, prompt[, condition])` | Repeatedly ask for a non-empty string. |
| `UInputGetHouse(%result%, prompt[, condition])` | Select a house. |
| `UInputGetCountry(%result%, prompt[, condition])` | Select a country. |
| `UInputGetTrigger(%result%, prompt[, condition])` | Select a trigger ID. |
| `UInputGetTag(%result%, prompt[, condition])` | Select a tag ID. |

### Map and INI

| API | Description |
| --- | --- |
| `GetIniKey(%result%, section, key[, condition])` | Read a map INI value, or an empty string if it does not exist. |
| `SetIniKey(section, key, value[, condition])` | Write a map INI value. Requires INI protection to be disabled. |
| `SetSafeMode(enabled, reason[, condition])` | Toggle INI protection; disabling it requires confirmation. |
| `SetWaypoint(id, x, y[, condition])` | Add or move a waypoint. Use a negative ID to allocate one automatically. |
| `GetFreeWaypoint(%result%[, condition])` | Store an unused waypoint ID. |
| `GetWaypointPos(id, %x%, %y%[, condition])` | Read waypoint coordinates. Missing waypoints return `0,0`. |
| `Resize(left, top, width, height[, condition])` | Resize the map after confirmation; width and height must not exceed 200. |
| `GetHouse(%result%, index[, condition])` | Read a house ID by zero-based index. |
| `GetCountry(%result%, index[, condition])` | Read a country ID by zero-based index. |

### Adding map content

Call `AllowAdd(reason)` first. The user must confirm before add commands take effect.

| API | Description |
| --- | --- |
| `AllowAdd(reason[, condition])` | Request permission to add objects or triggers. |
| `AddTrigger(%id%, triggerData, eventData, actionData, createTag[, condition])` | Add a trigger and optionally a tag; store the new ID when `%id%` is non-empty. |
| `AddAITrigger(%id%, data[, condition])` | Add an AI trigger and optionally store its ID. |
| `AddTag(%id%, data[, condition])` | Add a tag and optionally store its ID. |
| `AddTerrain(type, x, y[, condition])` | Add terrain at map coordinates. |
| `AddSmudge(type, x, y[, condition])` | Add a smudge when supported by the editor variant. |
| `AddInfantry(data[, condition])` | Add 14-field infantry INI data. |
| `AddVehicle(data[, condition])` | Add 14-field vehicle INI data. |
| `AddAircraft(data[, condition])` | Add 12-field aircraft INI data. |
| `AddStructure(data[, condition])` | Add 17-field structure INI data. |

Object data fields use the game's comma-separated map INI format. `GetInfantry`, `GetVehicle`, `GetAircraft`, and `GetStructure` are the safest way to obtain a compatible record before changing fields with `SetParam`.

### Reading and deleting map content

Call `AllowDelete(reason)` first. The user must confirm before delete commands take effect.

| API | Description |
| --- | --- |
| `GetInfantry(%result%, index[, condition])` | Read infantry INI data by zero-based index. |
| `GetVehicle(%result%, index[, condition])` | Read vehicle INI data by zero-based index. |
| `GetAircraft(%result%, index[, condition])` | Read aircraft INI data by zero-based index. |
| `GetStructure(%result%, index[, condition])` | Read structure INI data by zero-based index. |
| `AllowDelete(reason[, condition])` | Request permission to delete objects or triggers. |
| `DeleteTerrain(index[, condition])` | Delete terrain by index. |
| `DeleteInfantry(index[, condition])` | Delete infantry by index. |
| `DeleteVehicle(index[, condition])` | Delete a vehicle by index. |
| `DeleteAircraft(index[, condition])` | Delete aircraft by index. |
| `DeleteStructure(index[, condition])` | Delete a structure by index. |
| `IsInfantryDeleted(%result%, index[, condition])` | Store whether an infantry record is deleted. |
| `IsTerrainDeleted(%result%, index[, condition])` | Store whether a terrain record is deleted. |
)fscriptmd";

static CString BuildUserScriptApiMarkdown()
{
	return CString(kUserScriptApiMarkdown);
}

struct FUNC_INFO
{
	int type;
	CString name;
	std::vector<CString> params;
	int paramcount;
};

CString CUserScriptsDlg::GetScriptPath(const CString& scriptName) const
{
	return (CString)AppPath + "\\Scripts\\" + scriptName;
}

static CString NormalizeScriptTextForComparison(CString text)
{
	text.Replace("\r\n", "\n");
	text.Replace("\r", "\n");
	return text;
}

BOOL CUserScriptsDlg::IsEditorDirty()
{
	if(m_loadingSource || m_loadedScript.IsEmpty()) return FALSE;

	CString source;
	GetDlgItemText(IDC_SCRIPT_EDITOR, source);
	return NormalizeScriptTextForComparison(source) != NormalizeScriptTextForComparison(m_originalSource);
}

void CUserScriptsDlg::UpdateEditorState()
{
	const BOOL hasScript=!m_loadedScript.IsEmpty();
	GetDlgItem(IDC_SCRIPT_EDITOR)->EnableWindow(hasScript);
	GetDlgItem(IDC_SCRIPT_SAVE)->EnableWindow(hasScript && IsEditorDirty());
	GetDlgItem(IDOK)->EnableWindow(hasScript);
}

BOOL CUserScriptsDlg::LoadScriptSource(const CString& scriptName)
{
	CFile file;
	const CString path=GetScriptPath(scriptName);
	if(!file.Open(path, CFile::modeRead | CFile::shareDenyNone))
	{
		CString message=TranslateStringVariables(1, TranslateStringACP("Could not open script %1."), scriptName);
		MessageBox(message, TranslateStringACP("Error"), MB_ICONERROR);
		return FALSE;
	}

	const ULONGLONG fileLength=file.GetLength();
	if(fileLength>8 * 1024 * 1024)
	{
		file.Close();
		MessageBox(TranslateStringACP("The script is too large to edit."), TranslateStringACP("Error"), MB_ICONERROR);
		return FALSE;
	}

	CString source;
	LPSTR buffer=source.GetBuffer(static_cast<int>(fileLength) + 1);
	const UINT bytesRead=file.Read(buffer, static_cast<UINT>(fileLength));
	buffer[bytesRead]=0;
	source.ReleaseBuffer(bytesRead);
	file.Close();

	m_loadingSource=TRUE;
	m_loadedScript=scriptName;
	m_Script=scriptName;
	m_Source=source;
	SetDlgItemText(IDC_SCRIPT_EDITOR, source);
	// A multiline Windows edit control can normalize newline sequences while
	// accepting text. Use the text it actually stores as the clean baseline so
	// opening and closing an untouched script never produces a save prompt.
	GetDlgItemText(IDC_SCRIPT_EDITOR, m_originalSource);
	m_Source=m_originalSource;
	m_loadingSource=FALSE;
	UpdateEditorState();
	return TRUE;
}

BOOL CUserScriptsDlg::SelectScript(const CString& scriptName)
{
	CListBox* scripts=(CListBox*)GetDlgItem(IDC_SCRIPTS);
	const int index=scripts->FindStringExact(-1, scriptName);
	if(index==LB_ERR) return FALSE;
	scripts->SetCurSel(index);
	return TRUE;
}

BOOL CUserScriptsDlg::SaveCurrentScript()
{
	if(m_loadedScript.IsEmpty()) return FALSE;

	CString source;
	GetDlgItemText(IDC_SCRIPT_EDITOR, source);

	CFile file;
	if(!file.Open(GetScriptPath(m_loadedScript), CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive))
	{
		CString message=TranslateStringVariables(1, TranslateStringACP("Could not save script %1."), m_loadedScript);
		MessageBox(message, TranslateStringACP("Error"), MB_ICONERROR);
		return FALSE;
	}

	if(!source.IsEmpty()) file.Write((LPCSTR)source, source.GetLength());
	file.Close();
	m_Source=source;
	m_originalSource=source;

	CListBox* scripts=(CListBox*)GetDlgItem(IDC_SCRIPTS);
	if(scripts->FindStringExact(-1, m_loadedScript)==LB_ERR) scripts->AddString(m_loadedScript);
	SelectScript(m_loadedScript);
	UpdateEditorState();
	return TRUE;
}

BOOL CUserScriptsDlg::ConfirmSaveChanges()
{
	if(!IsEditorDirty()) return TRUE;

	CString message=TranslateStringVariables(1, TranslateStringACP("Save changes to %1?"), m_loadedScript);
	const int result=MessageBox(message, TranslateStringACP("Map Scripts"), MB_YESNOCANCEL | MB_ICONQUESTION);
	if(result==IDCANCEL) return FALSE;
	if(result==IDYES) return SaveCurrentScript();
	return TRUE;
}

void CUserScriptsDlg::OnSelchangeScripts()
{
	CListBox* scripts=(CListBox*)GetDlgItem(IDC_SCRIPTS);
	const int selected=scripts->GetCurSel();
	if(selected==LB_ERR) return;

	CString scriptName;
	scripts->GetText(selected, scriptName);
	if(scriptName==m_loadedScript) return;

	if(!ConfirmSaveChanges())
	{
		SelectScript(m_loadedScript);
		return;
	}

	if(!LoadScriptSource(scriptName)) SelectScript(m_loadedScript);
}

void CUserScriptsDlg::OnChangeScriptEditor()
{
	if(!m_loadingSource) UpdateEditorState();
}

void CUserScriptsDlg::OnSaveScript()
{
	SaveCurrentScript();
}

void CUserScriptsDlg::OnNewScript()
{
	if(!ConfirmSaveChanges()) return;

	CString scriptName=InputBox(TranslateStringACP("Enter a name for the new map script. The .lua extension is used by default."), TranslateStringACP("New Map Script"));
	scriptName.TrimLeft();
	scriptName.TrimRight();
	if(scriptName.IsEmpty()) return;

	if(scriptName=="." || scriptName==".." || scriptName.FindOneOf("\\/:*?\"<>|")>=0)
	{
		MessageBox(TranslateStringACP("The script name contains invalid characters."), TranslateStringACP("Error"), MB_ICONERROR);
		return;
	}

	CString lowerName=scriptName;
	lowerName.MakeLower();
	const BOOL isLegacyScript=lowerName.GetLength()>=8 && lowerName.Right(8)==".fscript";
	const BOOL isLuaScript=lowerName.GetLength()>=4 && lowerName.Right(4)==".lua";
	if(!isLegacyScript && !isLuaScript)
	{
		scriptName += ".lua";
		lowerName += ".lua";
	}
	CreateDirectory((CString)AppPath+"\\Scripts", NULL);

	if(GetFileAttributes(GetScriptPath(scriptName))!=INVALID_FILE_ATTRIBUTES)
	{
		MessageBox(TranslateStringACP("A script with that name already exists."), TranslateStringACP("Error"), MB_ICONERROR);
		SelectScript(scriptName);
		LoadScriptSource(scriptName);
		return;
	}

	m_loadedScript=scriptName;
	m_Script=scriptName;
	if(lowerName.Right(4)==".lua")
	{
		m_Source="-- Lua 5.5 map script\r\n-- Press F5 or Ctrl+Enter to save and run.\r\n-- Map changes are applied only after the script succeeds.\r\n\r\nprint(\"Map:\", map.get(\"Basic\", \"Name\", \"Unnamed\"))\r\nprint(\"Size:\", map.info.width, map.info.height)\r\n";
	}
	else
	{
		m_Source="// Legacy map script (.fscript)\r\n// Press F5 or Ctrl+Enter to save and run.\r\n\r\nPrint(\"Script started.\");\r\n";
	}
	m_originalSource.Empty();
	m_loadingSource=TRUE;
	SetDlgItemText(IDC_SCRIPT_EDITOR, m_Source);
	m_loadingSource=FALSE;
	if(SaveCurrentScript())
	{
		GetDlgItem(IDC_SCRIPT_EDITOR)->SetFocus();
		((CEdit*)GetDlgItem(IDC_SCRIPT_EDITOR))->SetSel(m_Source.GetLength(), m_Source.GetLength());
	}
}

void CUserScriptsDlg::OnCopyApiMarkdown()
{
	const CString markdown=BuildUserScriptApiMarkdown();
	BOOL copied=FALSE;
	if(OpenClipboard())
	{
		if(EmptyClipboard())
		{
			HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE, markdown.GetLength() + 1);
			if(memory!=NULL)
			{
				LPVOID data=GlobalLock(memory);
				if(data!=NULL)
				{
					memcpy(data, (LPCSTR)markdown, markdown.GetLength() + 1);
					GlobalUnlock(memory);
					if(SetClipboardData(CF_TEXT, memory)!=NULL)
					{
						copied=TRUE;
						memory=NULL;
					}
				}
				if(memory!=NULL) GlobalFree(memory);
			}
		}
		CloseClipboard();
	}

	if(copied)
	{
		m_Report=TranslateStringACP("API reference copied as Markdown.");
		SetDlgItemText(IDC_REPORT, m_Report);
	}
	else
	{
		MessageBox(TranslateStringACP("Could not copy the API reference to the clipboard."), TranslateStringACP("Error"), MB_ICONERROR);
	}
}

BOOL CUserScriptsDlg::PreTranslateMessage(MSG* pMsg)
{
	if(pMsg->message==WM_KEYDOWN)
	{
		const BOOL controlDown=(GetKeyState(VK_CONTROL) & 0x8000)!=0;
		if(pMsg->wParam==VK_F5 || (pMsg->wParam==VK_RETURN && controlDown))
		{
			OnOK();
			return TRUE;
		}
		if((pMsg->wParam=='S' || pMsg->wParam=='s') && controlDown)
		{
			OnSaveScript();
			return TRUE;
		}
	}
	return CDialog::PreTranslateMessage(pMsg);
}

void CUserScriptsDlg::OnCancel()
{
	if(ConfirmSaveChanges()) CDialog::OnCancel();
}

BOOL CUserScriptsDlg::RunLuaScript()
{
	LuaScriptContext context;
	context.map=Map;
	context.ini=Map->GetIniFile();
	context.report=m_Script + " Report:\r\n\r\n";

	rs_lua_callbacks callbacks{};
	callbacks.get=UserLuaGet;
	callbacks.list=UserLuaList;
	callbacks.mutate=UserLuaMutate;
	callbacks.print=UserLuaPrint;

	std::array<char, 8192> error{};
	const int result=rs_lua_run(
		(const unsigned char*)(LPCSTR)m_Source,
		static_cast<size_t>(m_Source.GetLength()),
		(LPCSTR)m_Script,
		&callbacks,
		&context,
		error.data(),
		error.size());

	if(result!=RS_OK)
	{
		context.report += TranslateStringACP("Lua script failed:");
		context.report += "\r\n";
		context.report += error.data();
		m_Report=context.report;
		SetDlgItemText(IDC_REPORT, m_Report);
		MessageBox(error.data(), TranslateStringACP("Lua Script Error"), MB_ICONERROR);
		return FALSE;
	}

	if(context.changed)
	{
		const int apply=MessageBox(
			TranslateStringACP("The Lua script completed successfully and wants to apply its map changes. Apply them now?"),
			TranslateStringACP("Apply Lua Script Changes"),
			MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
		if(apply==IDYES)
		{
			Map->GetIniFile()=context.ini;
			Map->UpdateIniFile(MAPDATA_UPDATE_FROM_INI);
			((CFinalSunDlg*)theApp.GetMainWnd())->UpdateDialogs(FALSE, FALSE);
			context.report += TranslateStringACP("Lua map changes applied.");
		}
		else
		{
			context.report += TranslateStringACP("Lua map changes discarded.");
		}
		context.report += "\r\n";
	}
	else
	{
		context.report += TranslateStringACP("Lua script completed successfully. No map changes were requested.");
		context.report += "\r\n";
	}

	m_Report=context.report;
	SetDlgItemText(IDC_REPORT, m_Report);
	return TRUE;
}

void CUserScriptsDlg::OnOK() 
{
	if(m_loadedScript.IsEmpty() || !SaveCurrentScript()) return;
	UpdateData(TRUE);
	m_Script=m_loadedScript;
	CString lowerScriptName=m_Script;
	lowerScriptName.MakeLower();
	if(lowerScriptName.GetLength()>=4 && lowerScriptName.Right(4)==".lua")
	{
		RunLuaScript();
		return;
	}

	//srand((unsigned)time(NULL));

	if(m_Script.GetLength()==0) return;

	CUserScript s;
	if(s.LoadFile(GetScriptPath(m_Script))<0) return;
	m_sourceLines.clear();
	for(int sourceIndex=0;sourceIndex<s.functioncount;sourceIndex++)
		m_sourceLines.push_back(s.GetSourceLine(sourceIndex));

	CIniFile& ini=Map->GetIniFile();

	BOOL bUpdate=FALSE;
	BOOL bUpdateOnlyMission=TRUE;
	BOOL bNoRepos=TRUE;
	BOOL bSafeMode=TRUE;

	map<CString, CString> variables;
	char c[50];
	

	CString report=m_Script+" Report:\r\n\r\n";

	BOOL bAutoUpdate=TRUE;
	BOOL bAllowLoop=TRUE; // for now we enable this, as we´ve introduced a loop counter
	
	// stuff for faster CMapData processing:
	BOOL bUpdateWaypoints=FALSE;
	BOOL bOldUpdate=Map->m_noAutoObjectUpdate;

	BOOL bDeleteAllowed=FALSE; // shall the script be able to delete stuff?
	BOOL bAddAllowed=FALSE; // " delete stuff
	
	
	


	// for delete/add stuff:
	int lastInfantryDeleted=-1;
	CString lastStructureDeleted="";
	int lastTerrainDeleted=-1;
	CString lastUnitDeleted="";
	CString lastAircraftDeleted="";


	BOOL bIgnoreLoopCounts=FALSE;
	int loop_count=0;


	map<int, FUNC_INFO> functions;

	// get function ids
	int i;
	for(i=0;i<s.functioncount;i++)
	{
		FUNC_INFO info;
		CString& name=info.name;

		s.GetFunction(i, &info.name, &info.params);
		info.paramcount = static_cast<int>(info.params.size());

		
		
		if(name==ASK_CONTINUE)
		{
			info.type=ID_ASK_CONTINUE;
		}
		else if(name==MESSAGE)
		{
			info.type=ID_MESSAGE;
		}
		else if(name==ADD_TRIGGER)
		{
			info.type=ID_ADD_TRIGGER;
		}
		else if(name==SET_INI_KEY)
		{
			info.type=ID_SET_INI_KEY;
		}
		else if(name==SET_SAFE_MODE)
		{
			info.type=ID_SET_SAFE_MODE;
		}
		else if(name==SET_VARIABLE)
		{
			info.type=ID_SET_VARIABLE;
		}
		else if(name==ADD)
		{
			info.type=ID_ADD;
		}
		else if(name==SUBSTRACT)
		{
			info.type=ID_SUBSTRACT;
		}
		else if(name==MULTI)
		{
			info.type=ID_MULTI;
		}
		else if(name==DIVIDE)
		{
			info.type=ID_DIVIDE;
		}
		else if(name==SET_WAYPOINT)
		{
			info.type=ID_SET_WAYPOINT;
		}
		else if(name==REQUIRES_MP)
		{
			info.type=ID_REQUIRES_MP;
		}
		else if(name==REQUIRES_SP)
		{
			info.type=ID_REQUIRES_SP;
		}
		else if(name==ADD_AI_TRIGGER)
		{
			info.type=ID_ADD_AI_TRIGGER;
		}
		else if(name==ADD_TAG)
		{
			info.type=ID_ADD_TAG;
		}
		else if(name==RESIZE)
		{
			info.type=ID_RESIZE;
		}
		else if(name==IS)
		{
			info.type=ID_IS;						
		}
		else if(name==CANCEL)
		{			
			info.type=ID_CANCEL;
		}
		else if(name==PRINT)
		{			
			info.type=ID_PRINT;			
		}
		else if(name==TOLOWER)
		{	
			info.type=ID_TOLOWER;			
		}
		else if(name==TOUPPER)
		{			
			info.type=ID_TOUPPER;
		}
		else if(name==GET_FREE_WAYPOINT)
		{
			info.type=ID_GET_FREE_WAYPOINT;
		}
		else if(name==UINPUT_GET_INTEGER)
		{
			info.type=ID_UINPUT_GET_INTEGER;
		}
		else if(name== UINPUT_GET_STRING )
		{
			info.type=ID_UINPUT_GET_STRING;
		}
		else if(name==JUMP_TO_LINE)
		{
			info.type=ID_JUMP_TO_LINE;
		}
		else if(name==SET_AUTO_UPDATE)
		{
			info.type=ID_SET_AUTO_UPDATE;
		}
		else if(name==GET_RANDOM)
		{
			info.type=ID_GET_RANDOM;
		}
		else if(name==ADD_TERRAIN)
		{
			info.type=ID_ADD_TERRAIN;
		}
		else if(name==GET_INI_KEY)
		{
			info.type=ID_GET_INI_KEY;
		}
		else if(name==MODULO)
		{
			info.type=ID_MODULO;
		}
#ifdef SMUDGE_SUPP
		else if(name==ADD_SMUDGE)
		{
			info.type=ID_ADD_SMUDGE;
		}
#endif
		else if(name==INSERT)
		{
			info.type=ID_INSERT;
		}
		else if(name==LENGTH)
		{
			info.type=ID_LENGTH;
		}
		else if(name==TRIM)
		{
			info.type=ID_TRIM;
		}
		else if(name==GETCHAR)
		{
			info.type=ID_GETCHAR;
		}
		else if(name==REPLACE)
		{
			info.type=ID_REPLACE;
		}
		else if(name==REMOVE)
		{
			info.type=ID_REMOVE;
		}
		else if(name==GET_WAYPOINT_POS)
		{
			info.type=ID_GET_WAYPOINT_POS;
		}
		else if(name==GET_PARAM)
		{
			info.type=ID_GET_PARAM;
		}
		else if(name==SET_PARAM)
		{
			info.type=ID_SET_PARAM;
		}
		else if(name==GET_PARAM_COUNT)
		{
			info.type=ID_GET_PARAM_COUNT;
		}
		else if(name==ALLOW_DELETE)
		{
			info.type=ID_ALLOW_DELETE;
		}
		else if(name==DELETE_TERRAIN)
		{
			info.type=ID_DELETE_TERRAIN;
		}
		else if(name==DELETE_INFANTRY)
		{
			info.type=ID_DELETE_INFANTRY;
		}
		else if(name==DELETE_STRUCTURE)
		{
			info.type=ID_DELETE_STRUCTURE;
		}
		else if(name==DELETE_AIRCRAFT)
		{
			info.type=ID_DELETE_AIRCRAFT;
		}
		else if(name==DELETE_VEHICLE)
		{
			info.type=ID_DELETE_VEHICLE;
		}
		else if(name==IS_INFANTRY_DELETED)
		{
			info.type=ID_IS_INFANTRY_DELETED;
		}
		else if(name==IS_TERRAIN_DELETED)
		{
			info.type=ID_IS_TERRAIN_DELETED;
		}
		else if(name==ADD_INFANTRY)
		{
			info.type=ID_ADD_INFANTRY;
		}
		else if(name==ALLOW_ADD)
		{
			info.type=ID_ALLOW_ADD;
		}
		else if(name==ADD_VEHICLE) info.type=ID_ADD_VEHICLE;
		else if(name==ADD_AIRCRAFT) info.type=ID_ADD_AIRCRAFT;
		else if(name==ADD_STRUCTURE) info.type=ID_ADD_STRUCTURE;
		else if(name==GET_INFANTRY) info.type=ID_GET_INFANTRY;
		else if(name==GET_AIRCRAFT) info.type=ID_GET_AIRCRAFT;
		else if(name==GET_STRUCTURE) info.type=ID_GET_STRUCTURE;
		else if(name==GET_VEHICLE) info.type=ID_GET_VEHICLE;
		else if(name==UINPUT_GET_HOUSE) info.type=ID_UINPUT_GET_HOUSE;
		else if(name==UINPUT_GET_COUNTRY) info.type=ID_UINPUT_GET_COUNTRY;
		else if(name==UINPUT_GET_TRIGGER) info.type=ID_UINPUT_GET_TRIGGER;
		else if(name==UINPUT_GET_TAG) info.type=ID_UINPUT_GET_TAG;
		else if(name==MESSAGE_YES_NO) info.type=ID_MESSAGE_YES_NO;
		else if(name==GET_HOUSE) info.type=ID_GET_HOUSE;
		else if(name==GET_COUNTRY) info.type=ID_GET_COUNTRY;
		else if(name==GET_HOUSE_INDEX) info.type=ID_GET_HOUSE_INDEX;
		else if(name==OR) info.type=ID_OR;
		else if(name==AND) info.type=ID_AND;
		else if(name==NOT) info.type=ID_NOT;
		else if(name==UINPUT_GET_MANUAL) info.type=ID_UINPUT_GET_MANUAL;

		else info.type=-1;
		
			
		

		functions[i]=info;
	}


	for(i=0;i<s.functioncount;i++)
	{
		// initialize global variables here so they can´t be overwritten!
		itoa(Map->GetWidth(), c, 10);
		variables["%Width%"]=c;
		itoa(Map->GetHeight(), c, 10);
		variables["%Height%"]=c;
		itoa(Map->GetIsoSize(), c, 10);
		variables["%IsoSize%"]=c;
		itoa(Map->GetWaypointCount(), c, 10);
		variables["%WaypointCount%"]=c;
		itoa(Map->GetUnitCount(), c, 10);
		variables["%UnitCount%"]=c;
		itoa(Map->GetInfantryCount(), c, 10);
		variables["%InfantryCount%"]=c;
		itoa(Map->GetStructureCount(), c, 10);
		variables["%StructureCount%"]=c;
		itoa(Map->GetAircraftCount(), c, 10);
		variables["%AircraftCount%"]=c;
		itoa(Map->GetTerrainCount(), c, 10);
		variables["%TerrainCount%"]=c;
		variables["%Theater%"]=Map->GetTheater();
		itoa(get_player_count(), c, 10);
		variables["%PlayerCount%"]=c;
		itoa(Map->GetHousesCount(FALSE), c, 10);
		variables["%HousesCount%"]=c;
		itoa(Map->GetHousesCount(TRUE), c, 10);
		variables["%CountriesCount%"]=c;

		if(bDeleteAllowed)
		{
			variables["%DeleteAllowed%"]="1";
		}
		else
			variables["%DeleteAllowed%"]="0";

		if(bAddAllowed)
		{
			variables["%AddAllowed%"]="1";
		}
		else
			variables["%AddAllowed%"]="0";

		if(bSafeMode)
			variables["%SafeMode%"]="1";
		else
			variables["%SafeMode%"]="0";
		
		//CString name;
		int name=functions[i].type;
		
		int paramcount=functions[i].paramcount;
		
		CString* params=NULL;

		if(paramcount)
		{
			params=new(CString[paramcount]);
			int e;
			for(e=0;e<paramcount;e++)
				params[e]=functions[i].params[e];
		}
		
		//s.GetFunction(i, &name, &params, &paramcount);


		BOOL * replaceVariables=new(BOOL[paramcount+4]); // make sure at least room for 4 variables

		int h;
		for(h=0;h<paramcount;h++) replaceVariables[h]=TRUE;

		if(name==ID_SET_VARIABLE || name==ID_ADD || name==ID_SUBSTRACT || name==ID_MULTI || name==ID_DIVIDE 
			|| name==ID_TOLOWER || name==ID_TOUPPER || name==ID_GET_FREE_WAYPOINT || name==ID_JUMP_TO_LINE
			|| name==ID_UINPUT_GET_INTEGER || name==ID_UINPUT_GET_STRING || name==ID_GET_RANDOM || name==ID_ADD_TRIGGER
			|| name==ID_ADD_AI_TRIGGER || name==ID_GET_INI_KEY || name==ID_MODULO || name==ID_INSERT
			|| name==ID_LENGTH || name==ID_TRIM || name==ID_GETCHAR || name==ID_REPLACE || name==ID_REMOVE
			|| name==ID_GET_PARAM || name==ID_SET_PARAM || name==ID_GET_PARAM_COUNT || name==ID_IS_INFANTRY_DELETED
			|| name==ID_IS_TERRAIN_DELETED || name==ID_GET_INFANTRY || name==ID_GET_AIRCRAFT || name==ID_GET_STRUCTURE
			|| name==ID_GET_VEHICLE || name==ID_UINPUT_GET_TRIGGER || name==ID_UINPUT_GET_TAG
			|| name==ID_UINPUT_GET_HOUSE || name==ID_UINPUT_GET_COUNTRY || name==ID_MESSAGE_YES_NO
			|| name==ID_OR || name==ID_AND || name==ID_NOT || name==ID_UINPUT_GET_MANUAL)
		{
			replaceVariables[0]=FALSE;			
		}

		if(name==ID_GET_WAYPOINT_POS )
		{
			replaceVariables[1]=FALSE;
			replaceVariables[2]=FALSE;
		}

		if(name==ID_IS)
		{
			replaceVariables[3]=FALSE;
		}

		

		map<CString, CString>::iterator e;
		
		
		for(e=variables.begin();e!=variables.end();e++)
		{
			for(h=0;h<paramcount;h++)
			{
				if(replaceVariables[h])
				{
					params[h].Replace(e->first, e->second);
				}
			}
		}


		delete[] replaceVariables;


		if(name==ID_ASK_CONTINUE)
		{
			if(paramcount<1) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>1)
			{
				if(params[1].GetLength()>0)
				{
					if(!IsValSet(params[1])) goto nextline;
				}
			}

			int res=MessageBox(params[0], TranslateStringACP("Continue?"), MB_YESNO);
			if(res==IDNO) break;
		}
		else if(name==ID_MESSAGE)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			MessageBox(params[0], params[1]);
		}
		else if(name==ID_MESSAGE_YES_NO)
		{
			if(paramcount<3) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>3)
			{
				if(params[3].GetLength()>0)
				{
					if(!IsValSet(params[3])) goto nextline;
				}
			}

			int res=MessageBox(params[1], params[2], MB_YESNO);
			if(res==IDYES)
			{
				variables[params[0]]="1";
			}
			else
				variables[params[0]]="0";
		}
		else if(name==ID_ADD_TRIGGER)
		{
			if(paramcount<5) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>5)
			{
				if(params[5].GetLength()>0)
				{
					if(!IsValSet(params[5])) goto nextline;
				}
			}

			if(!bAddAllowed) goto nextline;

			
			CString ID_T=GetFreeID();

			if(params[0].GetLength()>0)
			{
				variables[params[0]]=ID_T;
			}			

			ini.sections["Triggers"].values[ID_T]=params[1];
			ini.sections["Events"].values[ID_T]=params[2];
			ini.sections["Actions"].values[ID_T]=params[3];

			BOOL tag=TRUE;
			params[4].MakeLower();
			if(params[4]=="false" || params[4]=="no") tag=FALSE;

			if(tag)
			{
				CString ID_TAG=GetFreeID();
				ini.sections["Tags"].values[ID_TAG]="0,";
				ini.sections["Tags"].values[ID_TAG]+=GetParam(params[1],2);
				ini.sections["Tags"].values[ID_TAG]+=",";
				ini.sections["Tags"].values[ID_TAG]+=ID_T;
			}

			report+="Trigger " + GetParam(params[1],2) + " added\r\n";
			
			bUpdate=TRUE;
		}
		else if(name==ID_SET_INI_KEY)
		{
			if(paramcount<3) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>3)
			{
				if(params[3].GetLength()>0)
				{
					if(!IsValSet(params[3])) goto nextline;
				}
			}
			
			//if(ini.sections[params[0]].FindName(params[1])>=0)
			{
				if(bSafeMode) goto nextline;				
			}

			ini.sections[params[0]].values[params[1]]=params[2];

			report +=params[0]+(CString)"->"+params[1]+(CString) " set to \"" + params[2] + "\"\r\n"; 
			
			bUpdate=TRUE;
			bUpdateOnlyMission=FALSE;
		}
		else if(name==ID_GET_INI_KEY)
		{
			if(paramcount<3) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>3)
			{
				if(params[3].GetLength()>0)
				{
					if(!IsValSet(params[3])) goto nextline;
				}
			}

			if(ini.sections.find(params[1])==ini.sections.end() || ini.sections[params[1]].FindName(params[2])<0)
			{
				variables[params[0]]="";				
			}
			else
			variables[params[0]]=ini.sections[params[1]].values[params[2]];
		}
		else if(name==ID_SET_SAFE_MODE)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			BOOL enabled=TRUE;
			params[0].MakeLower();
			if(params[0]=="false" || params[0]=="no") enabled=FALSE;

			if(!enabled)
			{
				CString s=TranslateStringVariables(1, TranslateStringACP("This script wants to disable INI protection. For some scripts this may be necessary, but it can seriously damage your map. Reason why script wants to disable INI protection: %1. Disable INI protection?"), params[1]);

				int res=MessageBox(s, TranslateStringACP("Disable INI protection?"), MB_YESNO | MB_DEFBUTTON2);
				if(res==IDNO) goto nextline;
			}

			if(!enabled) report+="INI Protection disabled\r\n";
			if(enabled) report+="INI Protection enabled\r\n";

			bSafeMode=enabled;
		}
		else if(name==ID_SET_VARIABLE)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			variables[params[0]]=params[1];
		}
		else if(name==ID_ADD)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			int n;
			n=atoi(variables[params[0]]);
			int n2=atoi(params[1]);
			n+=n2;
			char c[50];
			itoa(n, c, 10);
			variables[params[0]]=c;
		}
		else if(name==ID_INSERT)
		{
			if(paramcount<3) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>3)
			{
				if(params[3].GetLength()>0)
				{
					if(!IsValSet(params[3])) goto nextline;
				}
			}

			int pos=atoi(params[2]);

			if(pos<0)
			{
				pos=variables[params[0]].GetLength();
			}

			variables[params[0]].Insert(pos, params[1]);
		}
		else if(name==ID_REPLACE)
		{
			if(paramcount<3) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>3)
			{
				if(params[3].GetLength()>0)
				{
					if(!IsValSet(params[3])) goto nextline;
				}
			}			

			variables[params[0]].Replace(params[1], params[2]);
		}
		else if(name==ID_TRIM)
		{
			if(paramcount<1) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>1)
			{
				if(params[1].GetLength()>0)
				{
					if(!IsValSet(params[1])) goto nextline;
				}
			}

			variables[params[0]].TrimLeft();
			variables[params[0]].TrimRight();
		}
		else if(name==ID_NOT)
		{
			if(paramcount<1) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>1)
			{
				if(params[1].GetLength()>0)
				{
					if(!IsValSet(params[1])) goto nextline;
				}
			}

			if(IsValSet(variables[params[0]]))
				variables[params[0]]="0";
			else
				variables[params[0]]="1";
		}
		else if(name==ID_AND)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}
			
			BOOL bSet=TRUE;

			int k;
			for(k=1;k<paramcount;k++)
			{
				if(!IsValSet(params[k])) { bSet=FALSE; break; }
			}

			CString s="0";
			if(bSet) s="1";
			
			variables[params[0]]=s;			
		}
		else if(name==ID_OR)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}
			
			BOOL bSet=FALSE;

			int k;
			for(k=1;k<paramcount;k++)
			{
				if(IsValSet(params[k])) { bSet=TRUE; break; }
			}

			CString s="0";
			if(bSet) s="1";
			
			variables[params[0]]=s;			
		}
		else if(name==ID_LENGTH)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			int len=params[1].GetLength();
			char c[50];
			itoa(len, c, 10);
			variables[params[0]]=c;
		}
		else if(name==ID_REMOVE)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			if(atoi(params[1])>=variables[params[0]].GetLength() || atoi(params[1])<0 || atoi(params[2])<0)
			{
				MessageBox(TranslateStringACP("Invalid index or length for remove command, script cancelled."), TranslateStringACP("Error"));
				delete[] params;
				break;
			}

			variables[params[0]].Delete(atoi(params[1]),atoi(params[2]));
		}
		else if(name==ID_GETCHAR)
		{
			if(paramcount<3) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>3)
			{
				if(params[3].GetLength()>0)
				{
					if(!IsValSet(params[3])) goto nextline;
				}
			}

			if(atoi(params[2])>=params[1].GetLength() || atoi(params[2])<0)
			{
				MessageBox(TranslateStringACP("Invalid index for GetChar command, script cancelled."), TranslateStringACP("Error"));
				delete[] params;
				break;
			}

			variables[params[0]]=params[1].GetAt(atoi(params[2]));
		}
		else if(name==ID_SUBSTRACT)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			int n;
			n=atoi(variables[params[0]]);
			int n2=atoi(params[1]);
			n-=n2;
			char c[50];
			itoa(n, c, 10);
			variables[params[0]]=c;
		}
		else if(name==ID_MULTI)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			int n;
			n=atoi(variables[params[0]]);
			int n2=atoi(params[1]);
			n=n*n2;
			char c[50];
			itoa(n, c, 10);
			variables[params[0]]=c;
		}
		else if(name==ID_DIVIDE)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			int n;
			n=atoi(variables[params[0]]);
			int n2=atoi(params[1]);

			if(n2==0)
			{
				MessageBox(TranslateStringACP("Division through 0, script cancelled"), TranslateStringACP("Division through 0"));
				delete[] params;
				break;
			}

			n=n/n2;
			char c[50];
			itoa(n, c, 10);
			variables[params[0]]=c;
		}
		else if(name==ID_MODULO)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			int n;
			n=atoi(variables[params[0]]);
			int n2=atoi(params[1]);

			if(n2==0)
			{
				MessageBox(TranslateStringACP("Division through 0, script cancelled"), TranslateStringACP("Division through 0"));
				delete[] params;
				break;
			}

			n=n%n2;
			char c[50];
			itoa(n, c, 10);
			variables[params[0]]=c;
		}
		else if(name==ID_SET_WAYPOINT)
		{
			if(paramcount<3) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>3)
			{
				if(params[3].GetLength()>0)
				{
					if(!IsValSet(params[3])) goto nextline;
				}
			}

			int n=atoi(params[0]);
			CString id=params[0];
			if(n<0)
			{
				id="";
			}

			if(bSafeMode && n>=0)
			{
				if(ini.sections["Waypoints"].FindName(id)>=0)
				{
					goto nextline;
				}
			}


			DWORD pos=atoi(params[1])+atoi(params[2])*Map->GetIsoSize();
			
			if(pos<Map->GetIsoSize()*Map->GetIsoSize())
			{
				//Map->m_noAutoObjectUpdate=TRUE;				
				Map->AddWaypoint(id, pos);
				bUpdateWaypoints=TRUE;
				//Map->m_noAutoObjectUpdate=bOldUpdate;
				report+="Waypoint " + id + " set.\r\n";
			}
			else
			{
				report+="Waypoint " + id + " moving failed!\r\n";
			}

			bUpdate=TRUE;
			bUpdateOnlyMission=FALSE;
		}
		else if(name==ID_REQUIRES_MP)
		{
			// check bool
			if(paramcount>0)
			{
				if(params[0].GetLength()>0)
				{
					if(!IsValSet(params[0])) goto nextline;
				}
			}

			if(Map->IsMultiplayer()==FALSE)
			{
				MessageBox(TranslateStringACP("This script requires a multiplayer map and cannot be used with singleplayer maps"), TranslateStringACP("Error"));
				break;
			}
		}
		else if(name==ID_REQUIRES_SP)
		{
			// check bool
			if(paramcount>0)
			{
				if(params[0].GetLength()>0)
				{
					if(!IsValSet(params[0])) goto nextline;
				}
			}

			if(Map->IsMultiplayer()==TRUE)
			{
				MessageBox(TranslateStringACP("This script requires a singleplayer map and cannot be used with multiplayer maps"), TranslateStringACP("Error"));
				break;
			}
		}
		else if(name==ID_ADD_AI_TRIGGER)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			if(!bAddAllowed) goto nextline;

			CString ID_T=GetFreeID();

			if(params[0].GetLength()>0)
			{
				variables[params[0]]=ID_T;
			}	

			ini.sections["AITriggerTypes"].values[ID_T]=params[1];

			report+="AI Trigger " + GetParam(params[1],0) + " added\r\n";
			
			bUpdate=TRUE;
		}
		else if(name==ID_ADD_TAG)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			if(!bAddAllowed) goto nextline;
			
			CString ID_T=GetFreeID();

			if(params[0].GetLength()>0)
			{
				variables[params[0]]=ID_T;
			}		

			CString ID_TAG=ID_T; //GetFreeID();
			ini.sections["Tags"].values[ID_TAG]=params[1]; 

			report+="Tag " + GetParam(params[1],1) + " added\r\n";
			
			bUpdate=TRUE;
		}
		else if(name==ID_RESIZE)
		{
			if(paramcount<4) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>4)
			{
				if(params[4].GetLength()>0)
				{
					if(!IsValSet(params[4])) goto nextline;
				}
			}

			int res=MessageBox(TranslateStringACP("This script wants to resize the map. Resize map?"), TranslateStringACP("Resize map?"), MB_YESNO);
			if(res==IDNO) goto nextline;


			if(atoi(params[2])>200 || atoi(params[3])>200)
			{
				MessageBox(TranslateStringACP("Resizing map failed. Script cancelled."), TranslateStringACP("Error"));
				break;
			}

			Map->ResizeMap(atoi(params[0]), atoi(params[1]), atoi(params[2]), atoi(params[3]));

			bUpdate=TRUE;
			bUpdateOnlyMission=FALSE;
			bNoRepos=FALSE;
		}
		else if(name==ID_IS)
		{
			if(paramcount<4) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>4)
			{
				if(params[4].GetLength()>0)
				{
					if(!IsValSet(params[4])) goto nextline;
				}
			}

			BOOL bIs=FALSE;

			if(params[1]=="<")
			{
				if(atoi(params[0])<atoi(params[2])) bIs=TRUE;
			}
			if(params[1]=="<=")
			{
				if(atoi(params[0])<=atoi(params[2])) bIs=TRUE;
			}
			if(params[1]=="=")
			{
				if(atoi(params[0])==atoi(params[2])) 
				{					
					bIs=TRUE;
					
				}
				if(params[0]==params[2]) {bIs=TRUE;}
			}
			if(params[1]==">=")
			{
				if(atoi(params[0])>=atoi(params[2])) bIs=TRUE;
			}
			if(params[1]==">")
			{
				if(atoi(params[0])>atoi(params[2])) bIs=TRUE;
			}
			if(params[1]=="!=")
			{
				if(atoi(params[0])!=atoi(params[2])) bIs=TRUE;
				if(params[0]!=params[2]) bIs=TRUE;
			}



			CString s="0";
			if(bIs) s="1";

			
						
			variables[params[3]]=s;
						
		}
		else if(name==ID_CANCEL)
		{			
			// check bool
			if(paramcount>0)
			{
				if(params[0].GetLength()>0)
				{
					if(!IsValSet(params[0])) goto nextline;
				}
			}

			break;
		}
		else if(name==ID_PRINT)
		{	
			
			if(paramcount<1) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>1)
			{
				if(params[1].GetLength()>0)
				{
					if(!IsValSet(params[1])) goto nextline;
				}
			}

			report+=params[0];
			report+="\r\n";

			
		}
		else if(name==ID_TOLOWER)
		{	
			
			if(paramcount<1) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>1)
			{
				if(params[1].GetLength()>0)
				{
					if(!IsValSet(params[1])) goto nextline;
				}
			}

			variables[params[0]].MakeLower();

			
		}
		else if(name==ID_TOUPPER)
		{	
			
			if(paramcount<1) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>1)
			{
				if(params[1].GetLength()>0)
				{
					if(!IsValSet(params[1])) goto nextline;
				}
			}

			variables[params[0]].MakeUpper();

		}
		else if(name==ID_GET_FREE_WAYPOINT)
		{
			if(paramcount<1) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>1)
			{
				if(params[1].GetLength()>0)
				{
					if(!IsValSet(params[1])) goto nextline;
				}
			}

			variables[params[0]]=GetFree("Waypoints");
		}
		else if(name==ID_UINPUT_GET_INTEGER)
		{
			
			if(paramcount<4) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>4)
			{
				if(params[4].GetLength()>0)
				{
					if(!IsValSet(params[4])) goto nextline;
				}
			}

			BOOL ok=FALSE;
			int n=0;
			while(!ok)
			{
				CString s=InputBox(params[1], TranslateStringACP("Enter Integer"));	
				ok=TRUE;

				if(s.GetLength()==0) ok=FALSE;

				n=atoi(s);				
				if(params[2].GetLength()>0)
				{
					if(n<atoi(params[2])) ok=FALSE;
				}

				if(params[3].GetLength()>0)
				{
					if(n>atoi(params[3])) ok=FALSE;
				}
			}

			char c[50];
			itoa(n, c, 10);
			variables[params[0]]=c;
		}
		else if(name==ID_UINPUT_GET_STRING)
		{
			
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			BOOL ok=FALSE;
			CString s;
			while(!ok)
			{
				s=InputBox(params[1], TranslateStringACP("Enter String"));	
				ok=TRUE;

				if (s.GetLength()==0) ok=FALSE;
			}
			variables[params[0]]=s;
		}
		else if(name==ID_UINPUT_GET_HOUSE)
		{			
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			CComboUInputDlg dlg;
			dlg.m_type=COMBOUINPUT_HOUSES;
			dlg.m_Caption=params[1];

			dlg.DoModal();
			
			variables[params[0]]=dlg.m_Combo;
		}
		else if(name==ID_UINPUT_GET_COUNTRY)
		{			
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			CComboUInputDlg dlg;
			dlg.m_type=COMBOUINPUT_COUNTRIES;
			dlg.m_Caption=params[1];

			dlg.DoModal();
			
			variables[params[0]]=dlg.m_Combo;
		}
		else if(name==ID_UINPUT_GET_TRIGGER)
		{			
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			CComboUInputDlg dlg;
			dlg.m_type=COMBOUINPUT_TRIGGERS;
			dlg.m_Caption=params[1];

			dlg.DoModal();
			
			variables[params[0]]=dlg.m_Combo;
		}
		else if(name==ID_UINPUT_GET_TAG)
		{			
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			CComboUInputDlg dlg;
			dlg.m_type=COMBOUINPUT_TAGS;
			dlg.m_Caption=params[1];

			dlg.DoModal();
			
			variables[params[0]]=dlg.m_Combo;
		}
		/*else if(name==ID_UINPUT_GET_MANUAL)
		{			
			if(paramcount<4) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>4)
			{
				if(params[4].GetLength()>0)
				{
					if(!IsValSet(params[4])) goto nextline;
				}
			}

			CComboUInputDlg dlg;
			dlg.m_type=COMBOUINPUT_MANUAL;
			dlg.m_Caption=params[1];
			dlg.bTruncateStrings=IsValSet(params[2]);

						

			dlg.DoModal();
			
			variables[params[0]]=dlg.m_Combo;
		}*/
		else if(name==ID_JUMP_TO_LINE)
		{
			if(paramcount<1) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>1)
			{
				if(params[1].GetLength()>0)
				{
					if(!IsValSet(params[1])) goto nextline;
				}
			}

			
			if(!bAllowLoop) 
			{
				int res=MessageBox(TranslateStringACP("This script tries to use loops. Some scripts may require this. However, this holds the risk of infinite loops, which may cause the FinalSun/FinalAlert2 to crash. Do you want to allow looping for this script?"), TranslateStringACP("Allow looping?"), MB_YESNO);
				if(res==IDYES)
				{
					bAllowLoop=TRUE;
				}else
				
				goto nextline; // not allowed in safe mode because of possible infinte loops#
			}

			if(!bIgnoreLoopCounts)
			{
				loop_count++;

				if(loop_count>300)
				{
					int res=MessageBox(TranslateStringACP("This script has exceeded the 300 loops limit. Do you want to remove the loop limit (not recommended, inherits risk of infinite loops if script has bugs)? If you press no, the script will stop after another 300 loops to ask you again. If you press cancel, the script will be cancelled."), TranslateStringACP("Loop Limit exceeded"), MB_YESNOCANCEL | MB_DEFBUTTON2);
					if(res==IDYES) bIgnoreLoopCounts=TRUE;
					if(res==IDNO) loop_count=0;
					if(res==IDCANCEL)
					{
						delete[] params;
						break;
					}

				}
			}

			
			int n=s.FindJumpLine(params[0]);
			if(n<0 || n>s.functioncount)
			{
								
				ReportScriptError(i);
				delete[] params;
				break;
			}


			i=n-1; // not n, as the for loop adds 1 again!
			goto nextline_no_update;
		}
		else if(name==ID_SET_AUTO_UPDATE)
		{
			if(paramcount<1) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>1)
			{
				if(params[1].GetLength()>0)
				{
					if(!IsValSet(params[1])) goto nextline;
				}
			}

			bAutoUpdate=IsValSet(params[0]);

			goto nextline;
		}
		else if(name==ID_GET_RANDOM)
		{
			if(paramcount<1) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>1)
			{
				if(params[1].GetLength()>0)
				{
					if(!IsValSet(params[1])) goto nextline;
				}
			}

			int n=rand(); // 0 and 32767
			
			char c[50];
			itoa(n, c, 10);
			variables[params[0]]=c;
		}
		else if(name==ID_ADD_TERRAIN)
		{ 
			if(paramcount<3) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>3)
			{
				if(params[3].GetLength()>0)
				{
					if(!IsValSet(params[3])) goto nextline;
				}
			}

			if(!bAddAllowed) goto nextline;

			DWORD pos;
			pos=atoi(params[1])+atoi(params[2])*Map->GetIsoSize();

			if(Map->GetTerrainAt(pos)<0)
			{
				Map->AddTerrain(params[0], pos);

				report+="Terrain added: " + params[0] + (CString)" at " + params[1] + (CString)"/" + params[2] + "\r\n";

				bUpdate=TRUE;
				bUpdateOnlyMission=FALSE;
			}
		}
#ifdef SMUDGE_SUPP
		else if(name==ID_ADD_SMUDGE)
		{ 
			if(paramcount<3) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>3)
			{
				if(params[3].GetLength()>0)
				{
					if(!IsValSet(params[3])) goto nextline;
				}
			}
			DWORD pos;
			pos=atoi(params[1])+atoi(params[2])*Map->GetIsoSize();

			FIELDDATA* fd=Map->GetFielddataAt(pos);
			if(fd->smudge<0)
			{
				SMUDGE s;
				s.deleted=0;
				s.type=params[0];
				s.x=atoi(params[1]);
				s.y=atoi(params[2]);
				Map->AddSmudge(&s);

				report+="Smudge added: " + params[0] + (CString) " at " + params[1] + (CString)"/" + params[2] + "\r\n";

				bUpdate=TRUE;
				bUpdateOnlyMission=FALSE;
			}
		}
#endif
		else if(name==ID_GET_WAYPOINT_POS)
		{ 
			if(paramcount<3) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>3)
			{
				if(params[3].GetLength()>0)
				{
					if(!IsValSet(params[3])) goto nextline;
				}
			}
			
			BOOL bFound=FALSE;
			DWORD pos;
			int k;
			for(k=0;k<Map->GetWaypointCount();k++)
			{
				CString id;
				Map->GetWaypointData(k, &id, &pos);

				if(id==params[0])
				{
					bFound=TRUE;
					break;
				}
			}

			if(!bFound) pos=0;

			int x=pos%Map->GetIsoSize();
			int y=pos/Map->GetIsoSize();

			char c[50];
			itoa(x, c, 10);
			variables[params[1]]=c;
			itoa(y, c, 10);
			variables[params[2]]=c;			
		}
		else if(name==ID_GET_PARAM)
		{ 
			if(paramcount<3) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>3)
			{
				if(params[3].GetLength()>0)
				{
					if(!IsValSet(params[3])) goto nextline;
				}
			}
			
			variables[params[0]]=GetParam(params[1], atoi(params[2]));
		}
		else if(name==ID_SET_PARAM)
		{ 
			if(paramcount<3) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>3)
			{
				if(params[3].GetLength()>0)
				{
					if(!IsValSet(params[3])) goto nextline;
				}
			}
			
			variables[params[0]]=SetParam(variables[params[0]], atoi(params[1]), params[2]);
		}
		else if(name==ID_GET_PARAM_COUNT)
		{ 
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			int k;
			int count=1; // we start at 1 param even if no , exists!
			for(k=0;k<params[1].GetLength();k++)
			{
				if(params[1].GetAt(k)==',') count++;				
			}

			char c[50];
			itoa(count, c, 10);
			
			variables[params[0]]=c;
		}
		else if(name==ID_ALLOW_DELETE)
		{
			if(paramcount<1) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>1)
			{
				if(params[1].GetLength()>0)
				{
					if(!IsValSet(params[1])) goto nextline;
				}
			}

			
			{
				CString s=TranslateStringVariables(1, TranslateStringACP("This script wants to delete objects or triggers from your map. For some scripts this may be necessary, but it can seriously damage your map. Reason why script wants to delete objects: %1. Do you want to allow the script to do this?"), params[0]);

				int res=MessageBox(s, TranslateStringACP("Allow deletion of objects?"), MB_YESNO | MB_DEFBUTTON2);
				if(res==IDNO) goto nextline;
			}

			bDeleteAllowed=TRUE;
		}
		else if(name==ID_ALLOW_ADD)
		{
			if(paramcount<1) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>1)
			{
				if(params[1].GetLength()>0)
				{
					if(!IsValSet(params[1])) goto nextline;
				}
			}

			
			{
				CString s=TranslateStringVariables(1, TranslateStringACP("This script wants to add objects or triggers to your map. For some scripts this may be necessary, but it can seriously damage your map. Reason why script wants to add objects: %1. Do you want to allow the script to do this?"), params[0]);

				int res=MessageBox(s, TranslateStringACP("Allow adding of objects?"), MB_YESNO | MB_DEFBUTTON2);
				if(res==IDNO) goto nextline;
			}

			bAddAllowed=TRUE;
		}
		else if(name==ID_DELETE_TERRAIN)
		{ 
			if(paramcount<1) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>1)
			{
				if(params[1].GetLength()>0)
				{
					if(!IsValSet(params[1])) goto nextline;
				}
			}
			
			if(!bDeleteAllowed ) goto nextline;

			int index=atoi(params[0]);
			if(index<0 || index>=Map->GetTerrainCount()) 
			{
				report+="Terrain deletion failed, invalid index\r\n";
				goto nextline;
			}

			lastTerrainDeleted=index;
			Map->DeleteTerrain(index);

			report+="Terrain deleted\r\n";

			bUpdate=TRUE;
			bUpdateOnlyMission=FALSE;
		}
		else if(name==ID_DELETE_INFANTRY)
		{ 
			if(paramcount<1) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>1)
			{
				if(params[1].GetLength()>0)
				{
					if(!IsValSet(params[1])) goto nextline;
				}
			}
			
			if(!bDeleteAllowed ) goto nextline;

			int index=atoi(params[0]);
			if(index<0 || index>=Map->GetInfantryCount()) 
			{
				report+="Infantry deletion failed, invalid index\r\n";
				goto nextline;
			}

			lastInfantryDeleted=index;
			Map->DeleteInfantry(index);

			report+="Infantry deleted\r\n";

			bUpdate=TRUE;
			bUpdateOnlyMission=FALSE;
		}
		else if(name==ID_DELETE_STRUCTURE)
		{ 
			if(paramcount<1) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>1)
			{
				if(params[1].GetLength()>0)
				{
					if(!IsValSet(params[1])) goto nextline;
				}
			}
			
			if(!bDeleteAllowed ) goto nextline;

			int index=atoi(params[0]);
			if(index<0 || index>=Map->GetStructureCount()) 
			{
				report+="Structure deletion failed, invalid index\r\n";
				goto nextline;
			}

			lastStructureDeleted=*ini.sections["Structures"].GetValueName(index);
			Map->DeleteStructure(index);

			report+="Structure deleted\r\n";

			bUpdate=TRUE;
			bUpdateOnlyMission=FALSE;
		}
		else if(name==ID_DELETE_VEHICLE)
		{ 
			if(paramcount<1) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>1)
			{
				if(params[1].GetLength()>0)
				{
					if(!IsValSet(params[1])) goto nextline;
				}
			}
			
			if(!bDeleteAllowed ) goto nextline;

			int index=atoi(params[0]);
			if(index<0 || index>=Map->GetUnitCount()) 
			{
				report+="Vehicle deletion failed, invalid index\r\n";
				goto nextline;
			}

			lastUnitDeleted=*ini.sections["Units"].GetValueName(index);
			Map->DeleteUnit(index);

			report+="Vehicle deleted\r\n";

			bUpdate=TRUE;
			bUpdateOnlyMission=FALSE;
		}
		else if(name==ID_DELETE_AIRCRAFT)
		{ 
			if(paramcount<1) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>1)
			{
				if(params[1].GetLength()>0)
				{
					if(!IsValSet(params[1])) goto nextline;
				}
			}
			
			
			if(!bDeleteAllowed ) goto nextline;

			int index=atoi(params[0]);
			if(index<0 || index>=Map->GetAircraftCount()) 
			{
				report+="Aircraft deletion failed, invalid index\r\n";
				goto nextline;
			}
			
			lastAircraftDeleted=*ini.sections["Aircraft"].GetValueName(index);
			Map->DeleteAircraft(index);

			report+="Aircraft deleted\r\n";

			bUpdate=TRUE;
			bUpdateOnlyMission=FALSE;
		}
		else if(name==ID_IS_INFANTRY_DELETED)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			CString deleted="1";
			int index=atoi(params[1]);
			if(index>=0 && index<Map->GetInfantryCount())
			{
				INFANTRY id;
				Map->GetInfantryData(index, &id);
				if(id.deleted==0) deleted="0";
			}

			variables[params[0]]=deleted;
		}
		else if(name==ID_IS_TERRAIN_DELETED)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			CString deleted="1";
			int index=atoi(params[1]);
			if(index>=0 && index<Map->GetTerrainCount())
			{
				TERRAIN id;
				Map->GetTerrainData(index, &id);
				if(id.deleted==0) deleted="0";
			}

			variables[params[0]]=deleted;
		}
		else if(name==ID_ADD_INFANTRY)
		{
			if(paramcount<1) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>1)
			{
				if(params[1].GetLength()>0)
				{
					if(!IsValSet(params[1])) goto nextline;
				}
			}

			// check param count
			int count=1; // we start at 1 param even if no , exists!
			int k;
			for(k=0;k<params[0].GetLength();k++)
			{
				if(params[0].GetAt(k)==',') count++;				
			}

			if(count!=14)
			{
				report+="AddInfantry failed\r\n";
				goto nextline;
			}

			CString data=params[0];
				
			INFANTRY id;
			id.deleted=0;
			id.house=GetParam(data, 0);	
			id.type=GetParam(data, 1);
			id.strength=GetParam(data, 2);
			id.y=GetParam(data, 3);
			id.x=GetParam(data, 4);
			//id.pos=GetParam(data, 5);
			id.pos="-1"; // ignore pos values!
			id.action=GetParam(data, 6);
			id.direction=GetParam(data, 7);
			id.tag=GetParam(data, 8);
			id.flag1=GetParam(data, 9);
			id.flag2=GetParam(data, 10);
			id.flag3=GetParam(data, 11);
			id.flag4=GetParam(data, 12);
			id.flag5=GetParam(data, 13);

			if(Map->AddInfantry(&id, NULL, NULL, NULL, lastInfantryDeleted)==FALSE)
			{
				report+="AddInfantry failed\r\n";
			}
			else
			{
				report+="Infantry added\r\n";
				bUpdate=TRUE;
				bUpdateOnlyMission=FALSE;
			}
		}
		else if(name==ID_ADD_VEHICLE)
		{
			if(paramcount<1) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>1)
			{
				if(params[1].GetLength()>0)
				{
					if(!IsValSet(params[1])) goto nextline;
				}
			}

			// check param count
			int count=1; // we start at 1 param even if no , exists!
			int k;
			for(k=0;k<params[0].GetLength();k++)
			{
				if(params[0].GetAt(k)==',') count++;				
			}

			if(count!=14)
			{
				report+="AddVehicle failed\r\n";
				goto nextline;
			}

			CString data=params[0];
				
			UNIT unit;
			unit.house=GetParam(data, 0);	
			unit.type=GetParam(data, 1);
			unit.strength=GetParam(data, 2);
			unit.y=GetParam(data, 3);
			unit.x=GetParam(data, 4);
			unit.direction=GetParam(data, 5);
			unit.action=GetParam(data, 6);
			unit.tag=GetParam(data, 7);
			unit.flag1=GetParam(data, 8);
			unit.flag2=GetParam(data, 9);
			unit.flag3=GetParam(data, 10);
			unit.flag4=GetParam(data, 11);
			unit.flag5=GetParam(data, 12);
			unit.flag6=GetParam(data, 13);

			if(Map->GetUnitAt(atoi(unit.x)+atoi(unit.y)*Map->GetIsoSize())>=0)
			{
				report+="AddVehicle failed\r\n";
				goto nextline;
			}

			if(Map->AddUnit(&unit, NULL, NULL, NULL, lastUnitDeleted)==FALSE)
			{
				report+="AddVehicle failed\r\n";
			}
			else
			{
				report+="Vehicle added\r\n";
				bUpdate=TRUE;
				bUpdateOnlyMission=FALSE;
			}
		}
		else if(name==ID_ADD_AIRCRAFT)
		{
			if(paramcount<1) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>1)
			{
				if(params[1].GetLength()>0)
				{
					if(!IsValSet(params[1])) goto nextline;
				}
			}

			// check param count
			int count=1; // we start at 1 param even if no , exists!
			int k;
			for(k=0;k<params[0].GetLength();k++)
			{
				if(params[0].GetAt(k)==',') count++;				
			}

			if(count!=12)
			{
				report+="AddAircraft failed\r\n";
				goto nextline;
			}

			CString data=params[0];
				
			AIRCRAFT air;
			air.house=GetParam(data, 0);	
			air.type=GetParam(data, 1);
			air.strength=GetParam(data, 2);
			air.y=GetParam(data, 3);
			air.x=GetParam(data, 4);
			air.direction=GetParam(data, 5);
			air.action=GetParam(data, 6);
			air.tag=GetParam(data, 7);
			air.flag1=GetParam(data, 8);
			air.flag2=GetParam(data, 9);
			air.flag3=GetParam(data, 10);
			air.flag4=GetParam(data, 11);

			if(Map->GetAirAt(atoi(air.x)+atoi(air.y)*Map->GetIsoSize())>=0)
			{
				report+="AddAircraft failed\r\n";
				goto nextline;
			}

			if(Map->AddAircraft(&air, NULL, NULL, NULL, lastAircraftDeleted)==FALSE)
			{
				report+="AddAircraft failed\r\n";
			}
			else
			{
				report+="Aircraft added\r\n";
				bUpdate=TRUE;
				bUpdateOnlyMission=FALSE;
			}
		}
		else if(name==ID_ADD_STRUCTURE)
		{
			if(paramcount<1) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>1)
			{
				if(params[1].GetLength()>0)
				{
					if(!IsValSet(params[1])) goto nextline;
				}
			}

			// check param count
			int count=1; // we start at 1 param even if no , exists!
			int k;
			for(k=0;k<params[0].GetLength();k++)
			{
				if(params[0].GetAt(k)==',') count++;				
			}

			if(count!=17)
			{
				report+="AddStructure failed\r\n";
				goto nextline;
			}

			CString data=params[0];
				
			STRUCTURE structure;
			structure.house=GetParam(data, 0);	
			structure.type=GetParam(data, 1);
			structure.strength=GetParam(data, 2);
			structure.y=GetParam(data, 3);
			structure.x=GetParam(data, 4);
			structure.direction=GetParam(data, 5);
			structure.tag=GetParam(data, 6);
			structure.flag1=GetParam(data, 7);
			structure.flag2=GetParam(data, 8);
			structure.energy=GetParam(data, 9);
			structure.upgradecount=GetParam(data, 10);
			structure.spotlight=GetParam(data, 11);
			structure.upgrade1=GetParam(data, 12);
			structure.upgrade2=GetParam(data, 13);
			structure.upgrade3=GetParam(data, 14);
			structure.flag3=GetParam(data, 15);
			structure.flag4=GetParam(data, 16);

			if(Map->GetStructureAt(atoi(structure.x)+atoi(structure.y)*Map->GetIsoSize())>=0)
			{
				report+="AddStructure failed\r\n";
				goto nextline;
			}

			if(Map->AddStructure(&structure, NULL, NULL, NULL, lastStructureDeleted)==FALSE)
			{
				report+="AddStructure failed\r\n";
			}
			else
			{
				report+="Structure added\r\n";
				bUpdate=TRUE;
				bUpdateOnlyMission=FALSE;
			}
		}
		else if(name==ID_GET_INFANTRY)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			int index=atoi(params[1]);
			CString s;
			if(index>=0 && index<Map->GetInfantryCount())
			{
				INFANTRY id;
				Map->GetInfantryINIData(index, &s);
			}

			variables[params[0]]=s;
		}
		else if(name==ID_GET_AIRCRAFT)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			int index=atoi(params[1]);
			CString s;
			if(index>=0 && index<Map->GetAircraftCount())
			{
				s=*ini.sections["Aircraft"].GetValue(index);
			}

			variables[params[0]]=s;
		}
		else if(name==ID_GET_STRUCTURE)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{ 
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			int index=atoi(params[1]);
			CString s;
			if(index>=0 && index<Map->GetStructureCount())
			{
				s=*ini.sections["Structures"].GetValue(index);
			}

			variables[params[0]]=s;
		}
		else if(name==ID_GET_VEHICLE)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			int index=atoi(params[1]);
			CString s;
			if(index>=0 && index<Map->GetUnitCount())
			{
				s=*ini.sections["Units"].GetValue(index);
			}

			variables[params[0]]=s;
		}
		else if(name==ID_GET_HOUSE)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			int index=atoi(params[1]);
			
			CString s;
			if(index>=0 && index<Map->GetHousesCount(FALSE))
			{
				s=Map->GetHouseID(index, FALSE);
			}

			variables[params[0]]=s;
		}
		else if(name==ID_GET_COUNTRY)
		{
			if(paramcount<2) 
			{
				ReportScriptError(i);
				delete[] params;
				break;
			}

			// check bool
			if(paramcount>2)
			{
				if(params[2].GetLength()>0)
				{
					if(!IsValSet(params[2])) goto nextline;
				}
			}

			int index=atoi(params[1]);
			
			CString s;
			if(index>=0 && index<Map->GetHousesCount(TRUE))
			{
				s=Map->GetHouseID(index, TRUE);
			}

			variables[params[0]]=s;
		}
		
		else
		{
			ReportScriptError(i);
			delete[] params;
			break;
		}
		
nextline:
		if(bAutoUpdate)
		{			
			m_Report=report;
			UpdateData(FALSE);
		}

nextline_no_update:

		delete[] params;	
		
	}

	m_Report=report;
	UpdateData(FALSE);
	
	//if(bUpdateWaypoints) Map->UpdateIniFile(MAPDATA_UPDATE_FROM_INI);

	if(bUpdate) ((CFinalSunDlg*)theApp.GetMainWnd())->UpdateDialogs(bUpdateOnlyMission, bNoRepos);
	
	// CDialog::OnOK();
}

BOOL CUserScriptsDlg::OnInitDialog() 
{
	CDialog::OnInitDialog();
	ApplyEditorUIFont(this);
	SetWindowText(TranslateStringACP("Map Scripts"));
	((CEdit*)GetDlgItem(IDC_SCRIPT_EDITOR))->SetTabStops(16);

	const CString scriptFolder=(CString)AppPath+"\\Scripts\\";
	{
		CListBox* lb=(CListBox*)GetDlgItem(IDC_SCRIPTS);
		const char* patterns[]={ "*.fscript", "*.lua" };
		for(const char* pattern : patterns)
		{
			CFileFind ff;
			if(ff.FindFile(scriptFolder + pattern))
			{
				BOOL bWorking=TRUE;
				while(bWorking)
				{
					bWorking=ff.FindNextFile();
					if(!ff.IsDirectory() && !ff.IsDots()) lb->AddString(ff.GetFileName());
				}
			}
		}

		if(lb->GetCount()>0)
		{
			lb->SetCurSel(0);
			CString scriptName;
			lb->GetText(0, scriptName);
			LoadScriptSource(scriptName);
		}
	}

	UpdateEditorState();
	
	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX-Eigenschaftenseiten sollten FALSE zurückgeben
}

void CUserScriptsDlg::ReportScriptError(int line)
{
	if(line>=0 && line<static_cast<int>(m_sourceLines.size())) line=m_sourceLines[line];

	char c[50];
	itoa(line, c, 10);

	MessageBox(TranslateStringVariables(1, TranslateStringACP("Script error in line %1. Probably wrong parameter count or unknown function call."), c), TranslateStringACP("Error"));
}
