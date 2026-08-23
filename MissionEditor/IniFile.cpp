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

// IniFile.cpp: Implementierung der Klasse CIniFile.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "IniFile.h"
#include <string>
#include <string_view>
#include <algorithm>
#include <stdexcept>
#include <sstream>



#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

using namespace std;

namespace
{
	constexpr std::string_view Utf8Marker = "; Encoding=UTF-8";

	bool IsValidUtf8(std::string_view text)
	{
		if (text.empty())
			return true;
		if (text.size() > static_cast<size_t>(INT_MAX))
			return false;

		return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
			static_cast<int>(text.size()), nullptr, 0) > 0;
	}

	bool LooksLikeCp936(std::string_view text)
	{
		size_t pairCount = 0;
		for (size_t i = 0; i < text.size(); ++i)
		{
			const auto lead = static_cast<unsigned char>(text[i]);
			if (lead < 0x80)
				continue;
			if (lead < 0x81 || lead > 0xFE || i + 1 >= text.size())
				return false;

			const auto trail = static_cast<unsigned char>(text[++i]);
			if (trail < 0x40 || trail == 0x7F || trail > 0xFE)
				return false;
			++pairCount;
		}
		return pairCount > 0;
	}

	bool ContainsThreeOrFourByteUtf8(std::string_view text)
	{
		return std::ranges::any_of(text, [](char value)
		{
			const auto byte = static_cast<unsigned char>(value);
			return byte >= 0xE0 && byte <= 0xF4;
		});
	}

	UINT GetLegacyAnsiCodePage()
	{
		wchar_t codePageText[16]{};
		if (GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE,
			codePageText, _countof(codePageText)) > 0)
		{
			const auto codePage = static_cast<UINT>(wcstoul(codePageText, nullptr, 10));
			if (codePage != 0 && codePage != CP_UTF8 && IsValidCodePage(codePage))
				return codePage;
		}

		const UINT oemCodePage = GetOEMCP();
		if (oemCodePage != CP_UTF8 && IsValidCodePage(oemCodePage))
			return oemCodePage;
		return 1252;
	}

	std::string ConvertToUtf8(std::string_view text, UINT sourceCodePage)
	{
		if (text.empty() || text.size() > static_cast<size_t>(INT_MAX))
			return std::string(text);

		const int sourceLength = static_cast<int>(text.size());
		const int wideLength = MultiByteToWideChar(sourceCodePage, 0, text.data(),
			sourceLength, nullptr, 0);
		if (wideLength <= 0)
			return std::string(text);

		std::wstring wide(wideLength, L'\0');
		if (MultiByteToWideChar(sourceCodePage, 0, text.data(), sourceLength,
			wide.data(), wideLength) == 0)
			return std::string(text);

		const int utf8Length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
			wide.data(), wideLength, nullptr, 0, nullptr, nullptr);
		if (utf8Length <= 0)
			return std::string(text);

		std::string utf8(utf8Length, '\0');
		if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), wideLength,
			utf8.data(), utf8Length, nullptr, nullptr) == 0)
			return std::string(text);
		return utf8;
	}

	std::string NormalizeToUtf8(std::string text)
	{
		constexpr std::string_view utf8Bom("\xEF\xBB\xBF", 3);
		bool explicitlyUtf8 = text.find(Utf8Marker) != std::string::npos;
		if (text.starts_with(utf8Bom))
		{
			text.erase(0, utf8Bom.size());
			explicitlyUtf8 = true;
		}
		if (IsValidUtf8(text))
		{
			// Some CP936 pairs are also valid two-byte UTF-8 sequences (for example,
			// GBK D2 BB is decoded as UTF-8 "һ" instead of "一"). On a Chinese
			// legacy system, prefer CP936 for this otherwise indistinguishable case.
			if (explicitlyUtf8 || GetLegacyAnsiCodePage() != 936 || ContainsThreeOrFourByteUtf8(text) ||
				!LooksLikeCp936(text))
				return text;
			return ConvertToUtf8(text, 936);
		}

		// Maps made by older Chinese editors commonly contain CP936 text. Detect
		// well-formed double-byte text even when Windows itself uses another locale.
		const UINT sourceCodePage = LooksLikeCp936(text) ? 936 : GetLegacyAnsiCodePage();
		return ConvertToUtf8(text, sourceCodePage);
	}
}

bool SortDummy::operator()(const CString& x, const CString& y) const
{
	// the length is more important than spelling (numbers!!!)...
	if (x.GetLength() < y.GetLength()) return true;
	if (x.GetLength() == y.GetLength())
	{
		if (x < y) return true;
	}

	return false;

}

typedef map<CString, CIniFileSection>::iterator CIniI;
typedef map<CString, CString, SortDummy>::iterator SI;
typedef map<CString, int, SortDummy>::iterator SII;


//////////////////////////////////////////////////////////////////////
// Konstruktion/Destruktion
//////////////////////////////////////////////////////////////////////

CIniFile::CIniFile()
{
	Clear();
}

CIniFile::~CIniFile()
{
	sections.clear();
}

WORD CIniFile::LoadFile(const CString& filename,  BOOL bNoSpaces)
{
	return LoadFile(std::string(filename.GetString()), bNoSpaces);
}

WORD CIniFile::LoadFile(const std::string& filename, BOOL bNoSpaces)
{
	Clear();

	if (filename.size() == NULL) return 1;
	m_filename = filename;

	return(InsertFile(filename, NULL, bNoSpaces));

}


void CIniFile::Clear()
{

	sections.clear();
}

CIniFileSection::CIniFileSection()
{
	values.clear();
	value_orig_pos.clear();
};

CIniFileSection::~CIniFileSection()
{
	values.clear();
	value_orig_pos.clear();
};

WORD CIniFile::InsertFile(const CString& filename, const char* Section, BOOL bNoSpaces)
{
	return InsertFile(std::string(filename.GetString()), Section, bNoSpaces);
}

WORD CIniFile::InsertFile(const std::string& filename, const char* Section, BOOL bNoSpaces)
{
	if (filename.size() == 0)
		return 1;

	ifstream input(filename, ios::in | ios::binary);
	if (!input.good())
		return 2;

	std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	std::istringstream file(NormalizeToUtf8(std::move(contents)));
	if (!file.good())
		return 2;


	//char cSec[256];
	//char cLine[4096];

	//memset(cSec, 0, 256);
	//memset(cLine, 0, 4096);
	CString cSec;
	std::string cLine;
	CIniFileSection* currentSection = nullptr;

	const auto npos = std::string::npos;

	while (std::getline(file, cLine))
	{
		// strip to left side of newline or comment
		cLine.erase(std::find_if(cLine.begin(), cLine.end(), [](const char c) { return c == '\r' || c == '\n' || c == ';'; }), cLine.end());

		const auto equals = cLine.find('=');
		const auto firstNonSpace = cLine.find_first_not_of(" \t");
		const auto openBracket = firstNonSpace != npos && cLine[firstNonSpace] == '['
			? firstNonSpace
			: npos;

		if (openBracket != npos && (equals == npos || equals > openBracket))
		{
			const auto closeBracket = cLine.find(']', openBracket + 1);
			if (closeBracket == npos || closeBracket == openBracket + 1)
			{
				// Ignore incomplete section headers such as a line containing only
				// "[". Clearing the active section also prevents subsequent lines
				// from being assigned to the preceding valid section by accident.
				cSec.Empty();
				currentSection = nullptr;
				continue;
			}

			if ((Section != nullptr) && cSec == Section)
				return 0; // the section we want to insert is finished

			cSec = cLine.substr(openBracket + 1, closeBracket - openBracket - 1).c_str();
			currentSection = nullptr;
		}
		else if (equals != npos && !cSec.IsEmpty())
		{
			if (Section == NULL || cSec == Section)
			{
				// a value is set and we have a valid current section!
				CString name = cLine.substr(0, equals).c_str();
				CString value = cLine.substr(equals + 1, cLine.size() - equals - 1).c_str();

				if (bNoSpaces)
				{
					name.Trim();
					value.Trim();
				}

				if (!currentSection)
					currentSection = &sections[cSec];

				const int cuValueIndex = static_cast<int>(currentSection->values.size());
				currentSection->values[name] = value;
				currentSection->value_orig_pos[name] = cuValueIndex;
			}
		}

	}

	return 0;
}

const CIniFileSection* CIniFile::GetSection(std::size_t index) const
{
	if (index > sections.size() - 1)
		return NULL;

	auto i = sections.cbegin();
	for (auto e = 0;e < index;e++)
		i++;

	return &i->second;
}

CIniFileSection* CIniFile::GetSection(std::size_t index)
{
	if (index > sections.size() - 1)
		return NULL;

	CIniI i = sections.begin();
	for (auto e = 0;e < index;e++)
		i++;

	return &i->second;
}

const CIniFileSection* CIniFile::GetSection(const CString& section) const
{
	auto it = sections.find(section);
	if (it == sections.end())
		return nullptr;
	return &it->second;
}

CIniFileSection* CIniFile::GetSection(const CString& section)
{
	auto it = sections.find(section);
	if (it == sections.end())
		return nullptr;
	return &it->second;
}

const CString* CIniFileSection::GetValue(std::size_t index) const noexcept
{
	if (index > values.size() - 1)
		return NULL;

	auto i = values.begin();
	for (auto e = 0;e < index;e++)
		i++;

	return &i->second;
}

CString* CIniFileSection::GetValue(std::size_t index) noexcept
{
	if (index > values.size() - 1)
		return NULL;

	auto i = values.begin();
	for (auto e = 0;e < index;e++)
		i++;

	return &i->second;
}

CString CIniFileSection::GetValueByName(const CString& valueName, const CString& defaultValue) const
{
	auto it = values.find(valueName);
	return (it == values.end()) ? defaultValue : it->second;
}

const CString* CIniFile::GetSectionName(std::size_t index) const noexcept
{
	if (index > sections.size() - 1)
		return NULL;

	auto i = sections.cbegin();
	for (auto e = 0; e < index; ++e)
		i++;

	return &(i->first);
}

CString& CIniFileSection::AccessValueByName(const CString& valueName)
{
	return values[valueName];
}

const CString* CIniFileSection::GetValueName(std::size_t index) const noexcept
{
	if (index > values.size() - 1)
		return NULL;

	auto i = values.begin();
	for (auto e = 0; e < index; ++e)
		i++;


	return &(i->first);
}

BOOL CIniFile::SaveFile(const CString& filename) const
{
	return SaveFile(std::string(filename.GetString()));
}

BOOL CIniFile::SaveFile(const std::string& Filename) const
{
	ofstream file(Filename, ios::out | ios::trunc);
	file << Utf8Marker << '\n';

	for (const auto& section : sections)
	{
		// skip sections without any values. They carry no information and are
		// usually the result of read accesses that accidentally created them.
		if (section.second.values.empty())
			continue;

		file << "[" << (LPCTSTR)section.first << "]\n";
		for (const auto& value : section.second.values)
		{
			file << (LPCTSTR)value.first << "=" << (LPCTSTR)value.second << '\n';
		}
		file << '\n';
	}

	file << '\n';

	return TRUE;
}


int CIniFileSection::FindValue(CString sval) const noexcept
{
	int i;
	auto it = values.cbegin();
	for (i = 0;i < values.size();i++)
	{
		if (sval == it->second)
			return i;
		it++;
	}
	return -1;
}

int CIniFileSection::FindName(CString sval) const noexcept
{
	int i;
	auto it = values.cbegin();
	for (i = 0;i < values.size();i++)
	{
		if (sval == it->first)
			return i;
		it++;
	}
	return -1;
}

void CIniFile::DeleteLeadingSpaces(BOOL bValueNames, BOOL bValues)
{
	int i;
	for (i = 0;i < sections.size();i++)
	{
		CIniFileSection& sec = *GetSection(i);
		int e;
		for (e = 0;e < sec.values.size();e++)
		{
			if (bValues) sec.GetValue(e)->TrimLeft();
			if (bValueNames)
			{
				CString value = *sec.GetValue(e);
				CString name = *sec.GetValueName(e);

				sec.values.erase(name);
				name.TrimLeft();
				sec.values[name] = value;
			}
		}
	}
}

void CIniFile::DeleteEndingSpaces(BOOL bValueNames, BOOL bValues)
{
	int i;
	for (i = 0;i < sections.size();i++)
	{
		CIniFileSection& sec = *GetSection(i);
		int e;
		for (e = 0;e < sec.values.size();e++)
		{
			if (bValues) sec.GetValue(e)->TrimRight();
			if (bValueNames)
			{
				//CString& name=(CString&)*sec.GetValueName(e);
				//name.TrimRight();
				CString value = *sec.GetValue(e);
				CString name = *sec.GetValueName(e);

				sec.values.erase(name);
				name.TrimRight();
				sec.values[name] = value;
			}
		}
	}
}

CString CIniFile::GetValueByName(const CString& sectionName, const CString& valueName, const CString& defaultValue) const
{
	auto section = GetSection(sectionName);
	if (!section)
		return defaultValue;
	return section->GetValueByName(valueName, defaultValue);
}

int CIniFileSection::GetValueOrigPos(int index) const noexcept
{
	if (index > value_orig_pos.size() - 1)
		return -1;

	auto i = value_orig_pos.cbegin();
	for (int e = 0;e < index;e++)
		i++;

	return i->second;
}

