/*
	FinalSun/FinalAlert 2 Mission Editor

	Copyright (C) 1999-2024 Electronic Arts, Inc.
	Authored by Matthias Wagner

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version BR of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "stdafx.h"
#include "tests.h"
#include "inlines.h"
#include <string>
#include <functional>
#include <iostream>
#include <filesystem>
#include <fstream>
#include "Tube.h"
#include "MissionEditorPackLib.h"
#include "RustCore.h"
#include "WaypointCodec.h"
#include "MapSnapshots.h"
#include "PropertyBrushTool.h"
#include "PalettedImageComposition.h"
#include "AlliesEditorDlg.h"

class TestError : public std::runtime_error
{
public:
	TestError(const std::string text) : std::runtime_error(text)
	{

	}
};

bool RaiseTestError(const char* file, int line, const char* function, const char* assertion)
{
	const std::string error = std::string("Test assertion: ") + assertion + " in file " + file + ", line " + std::to_string(line);
	throw TestError(error);
	return false;
}

bool ReportTest(const char* file, int line, const char* function, const char* assertion)
{
	const std::string error = std::string("Test succeeded in ") + function + " " + assertion + " in file " + file + ", line " + std::to_string(line);
	std::cout << error << std::endl;
	return true;
}

#define STR( s ) # s
#define REPORT_TEST(COND) (void) (((!!(COND)) && ReportTest(THIS_FILE, __LINE__, __FUNCTION__, STR(COND) )) || RaiseTestError(THIS_FILE, __LINE__, __FUNCTION__, STR(COND)))
#define TEST(COND) (void) ((!!(COND)) || RaiseTestError(THIS_FILE, __LINE__, __FUNCTION__, STR(COND)))

bool run_test(const std::function<void()>& f)
{
	try
	{
		f();
		return true;
	}
	catch(const TestError& e)
	{
		std::cout << e.what() << std::endl;
	}
	catch (const std::exception& e)
	{
		std::cout << "ERROR: Exception occurred: " << e.what() << std::endl;
	}
	return false;
}

int main(int argc, char* argv[])
{
#ifdef _DEBUG
	// Route CRT assertions to the debugger/stdout instead of a message box so
	// automated runs never block on a dialog; the resulting abort() still
	// terminates the process for CI.
	_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
#endif
	Tests t;
	return t.run();
}

int Tests::run()
{
	int failed_tests = 0;
	std::vector<std::function<void()>> test_functions({ 
		[this]() { test_inlines(); },
		[this]() { test_tube_create(); },
		[this]() { test_tube_reverse(); },
		[this]() { test_tube_append(); },
		[this]() { test_tube_delimiter(); },
		[this]() { test_hsv(); },
		[this]() { test_iso(); },
		[this]() { test_codecs(); },
		[this]() { test_csf(); },
		[this]() { test_lua_runtime(); },
		[this]() { test_waypoint_codec(); },
		[this]() { test_ini_utf8_normalization(); },
		[this]() { test_ini_malformed_section_is_discarded(); },
		[this]() { test_snapshot_redraw_flag(); },
		[this]() { test_property_brush_settings(); },
		[this]() { test_turret_offset_parsing(); },
		[this]() { test_paletted_image_composition(); },
		[this]() { test_alliance_sync(); },
		[this]() { test_rules_section_cache(); },
	});
	for (const auto f : test_functions)
	{
		if (!run_test(f))
			++failed_tests;
	}

	std::cout << "Failed: " << failed_tests << std::endl;
	std::cout << "Succeeded: " << test_functions.size() - failed_tests << std::endl;
	return failed_tests ? 1 : 0;
}

namespace
{
	struct LuaRuntimeTestHost
	{
		std::string output;
	};

	int LuaRuntimeTestGet(void*, const char*, const char*, char*, size_t, size_t*)
	{
		return 0;
	}

	size_t LuaRuntimeTestList(void*, const char*, char*, size_t)
	{
		return 0;
	}

	int LuaRuntimeTestMutate(void*, int, const char*, const char*, const char*)
	{
		return 1;
	}

	void LuaRuntimeTestPrint(void* context, const char* text)
	{
		((LuaRuntimeTestHost*)context)->output=text;
	}

	int LuaRuntimeTestInvoke(void*, const char* operation, const char* const*, size_t,
		char* dst, size_t dstCap, size_t* outLength)
	{
		if(strcmp(operation, "capabilities")!=0 || outLength==NULL) return 0;
		const char capabilities[]="test\0";
		const size_t length=sizeof(capabilities) - 1;
		*outLength=length;
		if(dst==NULL || dstCap<length) return 0;
		memcpy(dst, capabilities, length);
		return 1;
	}
}

void Tests::test_lua_runtime()
{
	LuaRuntimeTestHost host;
	rs_lua_callbacks callbacks{};
	callbacks.get=LuaRuntimeTestGet;
	callbacks.list=LuaRuntimeTestList;
	callbacks.mutate=LuaRuntimeTestMutate;
	callbacks.print=LuaRuntimeTestPrint;
	callbacks.invoke=LuaRuntimeTestInvoke;

	const char* source="assert(_VERSION == 'Lua 5.5'); assert(os == nil and io == nil and type(require) == 'function'); print('linked Lua 5.5')";
	char error[512]{};
	const int result=rs_lua_run(
		(const unsigned char*)source,
		strlen(source),
		"linked-test.lua",
		&callbacks,
		&host,
		error,
		_countof(error));
	REPORT_TEST(result == RS_OK);
	REPORT_TEST(host.output == "linked Lua 5.5");
}

void Tests::test_property_brush_settings()
{
	PropertyBrushSettings settings;
	REPORT_TEST(!settings.HasSelectedFields());
	REPORT_TEST(!settings.randomDirection);
	REPORT_TEST(GetPropertyBrushFieldCount(PropertyBrushObjectType::Structure) == 14);
	REPORT_TEST(GetPropertyBrushFieldCount(PropertyBrushObjectType::Infantry) == 10);
	REPORT_TEST(GetPropertyBrushFieldCount(PropertyBrushObjectType::Unit) == 11);
	REPORT_TEST(GetPropertyBrushFieldCount(PropertyBrushObjectType::Aircraft) == 9);

	settings.selected[8] = true;
	settings.values[8] = "GAPOWR";
	REPORT_TEST(settings.HasSelectedFields());
	REPORT_TEST(settings.values[8] == "GAPOWR");
}

void Tests::test_turret_offset_parsing()
{
	// vanilla single-value form stays identical to the old atoi handling
	Vec3f offset = ParseTurretOffset("30");
	REPORT_TEST(offset[0] == 5.0f && offset[1] == 0.0f && offset[2] == 0.0f);

	// Ares/Phobos F,L,H form: forward, lateral, height (in leptons)
	offset = ParseTurretOffset("6,-12,24");
	REPORT_TEST(offset[0] == 1.0f && offset[1] == -2.0f && offset[2] == 4.0f);

	offset = ParseTurretOffset("18, 0, 6");
	REPORT_TEST(offset[0] == 3.0f && offset[1] == 0.0f && offset[2] == 1.0f);

	// missing components default to zero
	offset = ParseTurretOffset("6,6");
	REPORT_TEST(offset[0] == 1.0f && offset[1] == 1.0f && offset[2] == 0.0f);

	offset = ParseTurretOffset("");
	REPORT_TEST(offset[0] == 0.0f && offset[1] == 0.0f && offset[2] == 0.0f);
}

void Tests::test_alliance_sync()
{
	// splitting trims entries and drops empty ones
	auto parts = SplitAlliesValue("GDI, Nod,,Neutral");
	REPORT_TEST(parts.size() == 3);
	REPORT_TEST(parts[0] == "GDI" && parts[1] == "Nod" && parts[2] == "Neutral");

	parts = SplitAlliesValue("");
	REPORT_TEST(parts.empty());

	parts = SplitAlliesValue(" GDI ");
	REPORT_TEST(parts.size() == 1 && parts[0] == "GDI");

	// adding a house appends it exactly once and keeps existing entries
	REPORT_TEST(UpdateAlliesValue("GDI", "Nod", TRUE) == "GDI,Nod");
	REPORT_TEST(UpdateAlliesValue("GDI,Nod", "Nod", TRUE) == "GDI,Nod");
	REPORT_TEST(UpdateAlliesValue("", "GDI", TRUE) == "GDI");

	// removing a house leaves all other entries intact
	REPORT_TEST(UpdateAlliesValue("GDI,Nod,Neutral", "Nod", FALSE) == "GDI,Neutral");
	REPORT_TEST(UpdateAlliesValue("GDI , Nod", "Nod", FALSE) == "GDI");
	REPORT_TEST(UpdateAlliesValue("GDI", "Nod", FALSE) == "GDI");
	REPORT_TEST(UpdateAlliesValue("GDI,GDI,Nod", "GDI", FALSE) == "Nod");
}

void Tests::test_paletted_image_composition()
{
	using namespace PalettedImageComposition;

	// The building origin remains at the result center even when an off-center
	// voxel projects beyond the original SHP canvas.
	const std::vector<std::uint8_t> base(16, 1);
	const std::vector<std::uint8_t> turret = { 9, 0, 0, 0, 0, 0 };
	const std::vector<std::uint8_t> normals = { 22, 0, 0, 0, 0, 0 };
	const Layer layers[] = {
		{ base, {}, 4, 4, 2, 2, 0, 0 },
		{ turret, normals, 3, 2, 0, 1, 0, 0 },
	};
	const auto composite = Compose(layers, CanvasMode::CenteredOrigin);
	REPORT_TEST(composite.width == 5 && composite.height == 4);
	REPORT_TEST(composite.colors[2 + 1 * composite.width] == 9);
	REPORT_TEST(composite.lighting[2 + 1 * composite.width] == 22);
	REPORT_TEST(composite.colors[1 + 1 * composite.width] == 1);
	REPORT_TEST(composite.lighting[1 + 1 * composite.width] == 46);

	// SHP turrets use the center of their own canvas as the anchor. Rules pixel
	// offsets therefore move that center relative to the building center.
	const std::vector<std::uint8_t> shpTurret(4, 7);
	const Layer shpLayers[] = {
		{ base, {}, 4, 4, 2, 2, 0, 0 },
		{ shpTurret, {}, 2, 2, 1, 1, 3, -2 },
	};
	const auto shpComposite = Compose(shpLayers, CanvasMode::CenteredOrigin);
	REPORT_TEST(shpComposite.width == 7 && shpComposite.height == 6);
	REPORT_TEST(shpComposite.colors[5 + 0 * shpComposite.width] == 7);

	// VXL bodies, turrets and barrels use independently projected canvases. Tight
	// composition must retain layers beyond every body edge and translate the
	// body's original anchor into the expanded result.
	const std::vector<std::uint8_t> vxlTurret = { 9, 0, 0, 0, 0, 0 };
	const std::vector<std::uint8_t> vxlTurretNormals = { 21, 0, 0, 0, 0, 0 };
	const std::vector<std::uint8_t> vxlBarrel = { 7, 0, 0, 0, 0, 0 };
	const std::vector<std::uint8_t> vxlBarrelNormals = { 35, 0, 0, 0, 0, 0 };
	const std::vector<std::uint8_t> vxlBodyNormals(16, 46);
	const Layer vxlLayers[] = {
		{ base, vxlBodyNormals, 4, 4, 2, 2, 0, 0 },
		{ vxlTurret, vxlTurretNormals, 3, 2, 1, 1, -3, -2 },
		{ vxlBarrel, vxlBarrelNormals, 2, 3, 0, 0, 4, 3 },
	};
	const auto vxlComposite = Compose(vxlLayers, CanvasMode::Tight);
	REPORT_TEST(vxlComposite.width == 10 && vxlComposite.height == 9);
	REPORT_TEST(vxlComposite.anchorX == 4 && vxlComposite.anchorY == 3);
	REPORT_TEST(vxlComposite.colors[0] == 9 && vxlComposite.lighting[0] == 21);
	REPORT_TEST(vxlComposite.colors[8 + 6 * vxlComposite.width] == 7);
	REPORT_TEST(vxlComposite.lighting[8 + 6 * vxlComposite.width] == 35);
	REPORT_TEST(vxlComposite.colors[2 + 1 * vxlComposite.width] == 1);
}

void Tests::test_ini_utf8_normalization()
{
	const std::filesystem::path inputPath = std::filesystem::temp_directory_path() /
		("FinalAlert2YRTests_cp936_" + std::to_string(GetCurrentProcessId()) + ".ini");
	const std::filesystem::path outputPath = std::filesystem::temp_directory_path() /
		("FinalAlert2YRTests_utf8_" + std::to_string(GetCurrentProcessId()) + ".ini");
	struct TempFileCleanup
	{
		std::filesystem::path input;
		std::filesystem::path output;
		~TempFileCleanup()
		{
			std::error_code error;
			std::filesystem::remove(input, error);
			std::filesystem::remove(output, error);
		}
	} cleanup{ inputPath, outputPath };

	const std::string cp936Map =
		"[Triggers]\r\n"
		"01000000=GDI,<none>,\xB2\xE2\xCA\xD4,0,1,1,1,0\r\n";
	{
		std::ofstream input(inputPath, std::ios::binary | std::ios::trunc);
		input.write(cp936Map.data(), static_cast<std::streamsize>(cp936Map.size()));
	}

	CIniFile ini;
	REPORT_TEST(ini.LoadFile(inputPath.string()) == 0);
	REPORT_TEST(GetParam(ini.sections["Triggers"].values["01000000"], 2) == CString("测试"));
	REPORT_TEST(ini.SaveFile(outputPath.string()));

	std::ifstream savedFile(outputPath, std::ios::binary);
	const std::string saved((std::istreambuf_iterator<char>(savedFile)),
		std::istreambuf_iterator<char>());
	REPORT_TEST(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, saved.data(),
		static_cast<int>(saved.size()), nullptr, 0) > 0);
	REPORT_TEST(saved.find("测试") != std::string::npos);

	CIniFile reloaded;
	REPORT_TEST(reloaded.LoadFile(outputPath.string()) == 0);
	REPORT_TEST(GetParam(reloaded.sections["Triggers"].values["01000000"], 2) == CString("测试"));

	// D2 BB (GBK "一") is coincidentally valid two-byte UTF-8 and must still
	// follow the Chinese legacy-code-page path instead of turning into "һ".
	const std::string ambiguousCp936Map =
		"[Triggers]\r\n"
		"01000000=GDI,<none>,\xD2\xBB,0,1,1,1,0\r\n";
	{
		std::ofstream input(inputPath, std::ios::binary | std::ios::trunc);
		input.write(ambiguousCp936Map.data(),
			static_cast<std::streamsize>(ambiguousCp936Map.size()));
	}
	CIniFile ambiguous;
	REPORT_TEST(ambiguous.LoadFile(inputPath.string()) == 0);
	REPORT_TEST(GetParam(ambiguous.sections["Triggers"].values["01000000"], 2) == CString("一"));
	REPORT_TEST(ambiguous.SaveFile(outputPath.string()));
	CIniFile ambiguousReloaded;
	REPORT_TEST(ambiguousReloaded.LoadFile(outputPath.string()) == 0);
	REPORT_TEST(GetParam(ambiguousReloaded.sections["Triggers"].values["01000000"], 2) == CString("一"));
}

void Tests::test_ini_malformed_section_is_discarded()
{
	const std::filesystem::path inputPath = std::filesystem::temp_directory_path() /
		("FinalAlert2YRTests_malformed_ini_" + std::to_string(GetCurrentProcessId()) + ".ini");
	const std::filesystem::path outputPath = std::filesystem::temp_directory_path() /
		("FinalAlert2YRTests_clean_ini_" + std::to_string(GetCurrentProcessId()) + ".ini");
	struct TempFileCleanup
	{
		std::filesystem::path input;
		std::filesystem::path output;
		~TempFileCleanup()
		{
			std::error_code error;
			std::filesystem::remove(input, error);
			std::filesystem::remove(output, error);
		}
	} cleanup{ inputPath, outputPath };

	const std::string malformedMap =
		"[Map]\r\n"
		"Theater=TEMPERATE\r\n"
		"[\r\n"
		"[Basic]\r\n"
		"Name=Clean map\r\n"
		"Translation key [Map] Theater=Localized text\r\n";
	{
		std::ofstream input(inputPath, std::ios::binary | std::ios::trunc);
		input.write(malformedMap.data(), static_cast<std::streamsize>(malformedMap.size()));
	}

	CIniFile ini;
	REPORT_TEST(ini.LoadFile(inputPath.string()) == 0);
	REPORT_TEST(ini.GetValueByName("Map", "Theater", CString()) == CString("TEMPERATE"));
	REPORT_TEST(ini.GetValueByName("Basic", "Name", CString()) == CString("Clean map"));
	REPORT_TEST(ini.GetValueByName("Basic", "Translation key [Map] Theater", CString()) == CString("Localized text"));
	REPORT_TEST(ini.SaveFile(outputPath.string()));

	std::ifstream savedFile(outputPath, std::ios::binary);
	const std::string saved((std::istreambuf_iterator<char>(savedFile)),
		std::istreambuf_iterator<char>());
	REPORT_TEST(saved.find("\n[\n") == std::string::npos);
}

void Tests::test_inlines()
{
	REPORT_TEST(GetParam("SOME,,Value", 1) == CString(""));
	REPORT_TEST(GetParam("SOME,,Value", 2) == CString("Value"));
	REPORT_TEST(GetParam("SOME,,Value", 0) == CString("SOME"));
	REPORT_TEST(GetParam("SOME,,Value", 77) == CString(""));
	REPORT_TEST(GetParam("SOME,,Value,", 3) == CString(""));
	REPORT_TEST(GetParam("SOME,,Value,0", 3) == CString("0"));
	REPORT_TEST(GetParam(" SOME,", 0) == CString(" SOME"));
	REPORT_TEST(SplitParams("SOME,,Value,0") == std::vector<CString>({ "SOME","","Value","0" }));
	REPORT_TEST(SplitParams("") == std::vector<CString>({ "" }));
	REPORT_TEST(SplitParams("SOME,,Value,0,") == std::vector<CString>({ "SOME","","Value","0", "" }));
	REPORT_TEST(Join("::", { "my", "value" }) == "my::value");
	REPORT_TEST(SetParam("SOME,,Value,0,", 0, "NOTSOME") == "NOTSOME,,Value,0,");
	REPORT_TEST(SetParam("SOME,,Value,0,", 1, "NOTSOME") == "SOME,NOTSOME,Value,0,");
	REPORT_TEST(SetParam("SOME,,Value,0,", 3, "1") == "SOME,,Value,1,");
	REPORT_TEST(SetParam("SOME,,Value,0,", 10, "A") == "SOME,,Value,0,,,,,,,A");
	REPORT_TEST(GetVoxelTurretFilename("YGGGUN", "YAGGUN") == "YAGGUN.vxl");
	REPORT_TEST(GetVoxelTurretFilename("GTGCAN", "") == "GTGCANtur.vxl");

	// rewritten helpers (previously fixed-buffer strcpy/strcat)
	REPORT_TEST(TranslateStringVariables(9, "Hello %9", "FinalAlert") == CString("Hello FinalAlert"));
	REPORT_TEST(TranslateStringVariables(9, "no variable", "FinalAlert") == CString("no variable"));
	{
		int x = -1, y = -1;
		PosToXY("123,456", &x, &y);
		REPORT_TEST(x == 123 && y == 456);
	}
	{
		int x = -1, y = -1;
		PosToXY("12", &x, &y); // shorter than 3 chars: used to read before the buffer
		REPORT_TEST(x == 0 && y == 0);
	}
	{
		CString name;
		GetNodeName(name, 0);
		REPORT_TEST(name == CString("000"));
		GetNodeName(name, 12);
		REPORT_TEST(name == CString("012"));
		GetNodeName(name, 123);
		REPORT_TEST(name == CString("123"));
		GetNodeName(name, 1234); // used to overflow char[5]
		REPORT_TEST(name == CString("1234"));
	}
}

namespace TubeDirections
{
	auto TL = ETubeDirection::TopLeft;
	auto TC = ETubeDirection::Top;
	auto TR = ETubeDirection::TopRight;
	auto CR = ETubeDirection::Right;
	auto BR = ETubeDirection::BottomRight;
	auto BC = ETubeDirection::Bottom;
	auto BL = ETubeDirection::BottomLeft;
	auto CL = ETubeDirection::Left;
	auto XX = ETubeDirection::Undefined;
}

void Tests::test_tube_create()
{
	using namespace TubeDirections;

	REPORT_TEST(CTube::autocreate(50, 50, 50, 50) == CTube(50, 50, XX, 50, 50, std::vector<ETubeDirection>({ XX })));
	REPORT_TEST(CTube::autocreate(50, 50, 55, 50) == CTube(50, 50, BC, 55, 50, std::vector<ETubeDirection>({ BC, BC, BC, BC, BC, XX})));
	REPORT_TEST(CTube::autocreate(50, 50, 45, 50) == CTube(50, 50, TC, 45, 50, std::vector<ETubeDirection>({ TC, TC, TC, TC, TC, XX })));
	REPORT_TEST(CTube::autocreate(50, 50, 45, 45, 0) == CTube(50, 50, CL, 45, 45, std::vector<ETubeDirection>({ TL, TL, TL, TL, TL, XX })));
	REPORT_TEST(CTube::autocreate(50, 50, 45, 45) == CTube(50, 50, CL, 45, 45, std::vector<ETubeDirection>({ CL, TL, TL, TL, TL, TC, XX })));
	REPORT_TEST(CTube::autocreate(50, 50, 45, 46, 0) == CTube(50, 50, TC, 45, 46, std::vector<ETubeDirection>({ TL, TL, TL, TL, TC, XX })));
	REPORT_TEST(CTube::autocreate(50, 50, 45, 46, 1) == CTube(50, 50, TC, 45, 46, std::vector<ETubeDirection>({ TC, TL, TL, TL, TL, XX })));
	REPORT_TEST(CTube::autocreate(50, 50, 46, 45, 1) == CTube(50, 50, CL, 46, 45, std::vector<ETubeDirection>({ CL, TL, TL, TL, TL, XX })));

}

void Tests::test_tube_reverse()
{
	using namespace TubeDirections;

	REPORT_TEST(CTube(50, 50, BC, 55, 50, std::vector<ETubeDirection>({ BC, BC, BC, BC, BC, XX})).reverse() ==
				CTube(55, 50, TC, 50, 50, std::vector<ETubeDirection>({ TC, TC, TC, TC, TC, XX })));
	REPORT_TEST(CTube(50, 50, TC, 45, 46, std::vector<ETubeDirection>({ TC, TL, TL, TL, TL, XX })).reverse() ==
				CTube(45, 46, BR, 50, 50, std::vector<ETubeDirection>({ BR, BR, BR, BR, BC, XX })));
	REPORT_TEST(CTube(50, 50, BC, 51, 50, std::vector<ETubeDirection>({ BC, XX })).reverse() ==
		        CTube(51, 50, TC, 50, 50, std::vector<ETubeDirection>({ TC, XX })));
	REPORT_TEST(CTube(50, 50, BC, 52, 50, std::vector<ETubeDirection>({ BC, BC, XX })).reverse() ==
				CTube(52, 50, TC, 50, 50, std::vector<ETubeDirection>({ TC, TC, XX })));

}

void Tests::test_tube_append()
{
	using namespace TubeDirections;

	CTube tubeToBottom(50, 50, BC, 53, 50, std::vector<ETubeDirection>({ BC, BC, BC, XX }));
	tubeToBottom.append(55, 50);
	REPORT_TEST(tubeToBottom == CTube(50, 50, BC, 55, 50, std::vector<ETubeDirection>({ BC, BC, BC, BC, BC, XX })));

	CTube tubeToTL(50, 50, TC, 49, 49, std::vector<ETubeDirection>({ TL, XX }));
	tubeToTL.append(49, 47);
	REPORT_TEST(tubeToTL == CTube(50, 50, TC, 49, 47, std::vector<ETubeDirection>({ TL, CL, CL, XX })));

	CTube nullLen(50, 50, XX, 50, 50, std::vector<ETubeDirection>({ XX }));
	nullLen.append(55, 50);
	REPORT_TEST(nullLen == CTube(50, 50, BC, 55, 50, std::vector<ETubeDirection>({ BC, BC, BC, BC, BC, XX })));

	CTube nullLenYMajor(50, 50, XX, 50, 50, std::vector<ETubeDirection>({ XX }));
	nullLenYMajor.append(51, 53);
	REPORT_TEST(nullLenYMajor == CTube(50, 50, CR, 51, 53, std::vector<ETubeDirection>({ CR, BR, CR, XX })));

	CTube nullAppend(50, 50, BC, 52, 50, std::vector<ETubeDirection>({ BC, BC, XX }));
	nullAppend.append(52, 50);
	REPORT_TEST(nullAppend == CTube(50, 50, BC, 52, 50, std::vector<ETubeDirection>({ BC, BC, XX })));

	CTube zeroTube(50, 50, XX, 50, 50, std::vector<ETubeDirection>());
	zeroTube.append(55, 50);
	REPORT_TEST(zeroTube == CTube(50, 50, BC, 55, 50, std::vector<ETubeDirection>({ BC, BC, BC, BC, BC, XX })));

	// Intersection - for now assume TS allows this
	CTube tubeWithIntersection(50, 50, TC, 49, 49, std::vector<ETubeDirection>({ TL, XX }));
	tubeWithIntersection.append(51, 51);
	REPORT_TEST(tubeWithIntersection == CTube(50, 50, TC, 51, 51, std::vector<ETubeDirection>({ TL, BR, BR, XX })));

	// Shorten
	CTube tubeShorten(50, 50, TC, 47, 47, std::vector<ETubeDirection>({ TL, TL, TL, XX }));
	tubeShorten.append(48, 48);
	REPORT_TEST(tubeShorten == CTube(50, 50, TC, 48, 48, std::vector<ETubeDirection>({ TL, TL, XX })));

}

void Tests::test_tube_delimiter()
{
	using namespace TubeDirections;

	// delimiter needs to be auto-added in any case as it causes crashes in TS/RA2 if it's missing somehow
	//REPORT_TEST(CTube(50, 50, XX, 50, 50, std::vector<ETubeDirection>()) == CTube(50, 50, XX, 50, 50, std::vector<ETubeDirection>({ XX })));
	//REPORT_TEST(CTube(50, 50, XX, 49, 49, std::vector<ETubeDirection>({ TL })) == CTube(50, 50, XX, 49, 49, std::vector<ETubeDirection>({ TL, XX })));
	REPORT_TEST(CTube(1, "50, 50, -1, 50, 50").toString() == "50,50,-1,50,50,-1");

	// right now we're also fixing when loading the map, TBD:
	REPORT_TEST(CTube(0xFFFF, "50, 50, -1, 50, 50") == CTube(50, 50, XX, 50, 50, std::vector<ETubeDirection>({ XX })));
}

void Tests::test_hsv()
{
	typedef std::array<unsigned char, 3> ba;
	REPORT_TEST(HSVToRGB(0, 1.0, 1.0) == ba({ 255, 0, 0 }));
	REPORT_TEST(HSVToRGB(0, 0.0, 1.0) == ba({ 255, 255, 255 }));
	REPORT_TEST(HSVToRGB(0, 0.0, 0.0) == ba({ 0, 0, 0 }));
	REPORT_TEST(HSVToRGB(45.0, 1.0, 1.0) == ba({ 255, 191, 0 }));
	REPORT_TEST(HSVToRGB(75.0, 1.0, 1.0) == ba({ 191, 255, 0 }));
	REPORT_TEST(HSVToRGB(135.0, 1.0, 1.0) == ba({ 0, 255, 63 }));
	REPORT_TEST(HSVToRGB(240.0, 1.0, 1.0) == ba({ 0, 0, 255 }));
	REPORT_TEST(HSVToRGB(184.0, 1.0, 1.0) == ba({ 0, 238, 255 }));
	REPORT_TEST(HSVToRGB(285.0, 1.0, 1.0) == ba({ 191, 0, 255 }));
	REPORT_TEST(HSVToRGB(330.0, 1.0, 1.0) == ba({ 255, 0, 127 }));
	REPORT_TEST(HSVToRGB(180.0, 1.0, 0.5) == ba({ 0, 127, 127 }));
	REPORT_TEST(HSVToRGB(180.0, 0.5, 0.5) == ba({ 63, 127, 127 }));
}

void Tests::test_iso()
{
	CMapData d;
	d.CreateMap(16, 10, 0, 0);
	const CMapData* previousMap = Map;
	struct MapPointerRestorer
	{
		const CMapData* previous;
		~MapPointerRestorer() { Map = const_cast<CMapData*>(previous); }
	} restoreMap{ previousMap };
	Map = &d;

	// Rendered pictures use the logical type as cache identity. Types sharing an
	// Image= must not reuse or overwrite each other's turret composition.
	const CString overrideType = "TEST_MAP_IMAGE_OVERRIDE_A";
	const CString secondType = "TEST_MAP_IMAGE_OVERRIDE_B";
	const CString globalImage = "TEST_GLOBAL_IMAGE";
	const CString mapImage = "TEST_MAP_IMAGE";
	rules.sections[overrideType].values["Image"] = globalImage;
	rules.sections[secondType].values["Image"] = globalImage;
	d.GetIniFile().sections[overrideType].values["Image"] = mapImage;
	const CString overrideKey = MakeUnitPictureCacheKey(overrideType, 3);
	const CString secondKey = MakeUnitPictureCacheKey(secondType, 3);
	const CString bitmapKey = mapImage + ".bmp";
	pics[overrideKey].pic = reinterpret_cast<void*>(1);
	pics[bitmapKey].pic = reinterpret_cast<void*>(1);
	struct PictureOverrideRestorer
	{
		CString type;
		CString secondType;
		CString globalImage;
		CString mapImage;
		CString cacheKey;
		CString bitmapKey;
		~PictureOverrideRestorer()
		{
			rules.sections.erase(type);
			rules.sections.erase(secondType);
			art.sections.erase(globalImage);
			art.sections.erase(mapImage);
			pics.erase(cacheKey);
			pics.erase(bitmapKey);
		}
	} restorePictureOverride{ overrideType, secondType, globalImage, mapImage, overrideKey, bitmapKey };
	REPORT_TEST(overrideKey != secondKey);
	REPORT_TEST(GetUnitPictureFilename(overrideType, 3) == overrideKey);
	REPORT_TEST(GetUnitPictureFilename(overrideType, 4) == bitmapKey);
	REPORT_TEST(GetUnitPictureFilename(secondType, 3).IsEmpty());

	REPORT_TEST(d.ProjectCoords3d(MapCoords(0, 0)) == ProjectedCoords((26 - 2) * f_x / 2, 0));
	REPORT_TEST(d.ProjectCoords3d(MapCoords(1, 0)) == ProjectedCoords((26 - 2 - 1) * f_x / 2, f_y / 2));
	REPORT_TEST(d.ProjectCoords3d(MapCoords(0, 1)) == ProjectedCoords((26 - 2 + 1) * f_x / 2, f_y / 2));
	REPORT_TEST(d.ProjectCoords3d(MapCoords(1, 1)) == ProjectedCoords((26 - 2) * f_x / 2, f_y));
	REPORT_TEST(d.ProjectCoords3d(MapCoords(1, 1), 1) == ProjectedCoords((26 - 2) * f_x / 2, f_y / 2));

	REPORT_TEST(d.ToMapCoords3d(ProjectedCoords((26 - 2) * f_x / 2, 0), 0) == MapCoords(0, 0));

	// Incremental unit cache maintenance must preserve the INI-sorted indices when a
	// deleted numeric id is reused, and moving must update both field data and INI data.
	const DWORD unitPosA = 5 + 5 * d.GetIsoSize();
	const DWORD unitPosB = 6 + 5 * d.GetIsoSize();
	const DWORD unitPosC = 7 + 5 * d.GetIsoSize();
	const DWORD unitPosD = 8 + 5 * d.GetIsoSize();
	TEST(d.AddUnit(NULL, "TEST_UNIT_A", "Neutral", unitPosA));
	TEST(d.AddUnit(NULL, "TEST_UNIT_B", "Neutral", unitPosB));
	d.DeleteUnit(0);
	TEST(d.AddUnit(NULL, "TEST_UNIT_C", "Neutral", unitPosC));
	REPORT_TEST(d.GetUnitAt(unitPosC) == 0);
	REPORT_TEST(d.GetUnitAt(unitPosB) == 1);
	TEST(d.MoveUnit(1, unitPosD));
	REPORT_TEST(d.GetUnitAt(unitPosB) == -1);
	REPORT_TEST(d.GetUnitAt(unitPosD) == 1);
	REPORT_TEST(GetParam(*d.GetIniFile().sections["Units"].GetValue(1), 4) == "8");

	const DWORD infantryPosA = 9 + 5 * d.GetIsoSize();
	const DWORD infantryPosB = 10 + 5 * d.GetIsoSize();
	TEST(d.AddInfantry(NULL, "TEST_INFANTRY", "Neutral", infantryPosA));
	const int infantryIndex = d.GetInfantryAt(infantryPosA);
	TEST(infantryIndex >= 0);
	TEST(d.MoveInfantry(infantryIndex, infantryPosB));
	REPORT_TEST(d.GetInfantryAt(infantryPosA) == -1);
	REPORT_TEST(d.GetInfantryAt(infantryPosB) == infantryIndex);
	TEST(d.AddInfantry(NULL, "TEST_INFANTRY_2", "Neutral", infantryPosB));
	TEST(d.AddInfantry(NULL, "TEST_INFANTRY_3", "Neutral", infantryPosB));
	REPORT_TEST(d.GetInfantryCountAt(infantryPosB) == SUBPOS_COUNT);
	REPORT_TEST(!d.AddInfantry(NULL, "TEST_INFANTRY_4", "Neutral", infantryPosB));

	const DWORD aircraftPosA = 5 + 6 * d.GetIsoSize();
	const DWORD aircraftPosB = 6 + 6 * d.GetIsoSize();
	const DWORD aircraftPosC = 7 + 6 * d.GetIsoSize();
	const DWORD aircraftPosD = 8 + 6 * d.GetIsoSize();
	TEST(d.AddAircraft(NULL, "TEST_AIRCRAFT_A", "Neutral", aircraftPosA));
	TEST(d.AddAircraft(NULL, "TEST_AIRCRAFT_B", "Neutral", aircraftPosB));
	d.DeleteAircraft(0);
	TEST(d.AddAircraft(NULL, "TEST_AIRCRAFT_C", "Neutral", aircraftPosC));
	REPORT_TEST(d.GetAirAt(aircraftPosC) == 0);
	REPORT_TEST(d.GetAirAt(aircraftPosB) == 1);
	TEST(d.MoveAircraft(1, aircraftPosD));
	REPORT_TEST(d.GetAirAt(aircraftPosB) == -1);
	REPORT_TEST(d.GetAirAt(aircraftPosD) == 1);

	const DWORD structurePos = 5 + 7 * d.GetIsoSize();
	d.m_noAutoObjectUpdate = TRUE; // UpdateStructures needs the editor's live view to calculate remap colors.
	TEST(d.AddStructure(NULL, "TEST_STRUCTURE", "Neutral", structurePos));
	REPORT_TEST(GetParam(*d.GetIniFile().sections["Structures"].GetValue(0), 15) == "1");
	
}

void Tests::test_snapshot_redraw_flag()
{
	FIELDDATA fielddata[1];
	fielddata[0].bRedrawTerrain = TRUE;
	fielddata[0].bHeight = 7;
	fielddata[0].bMapData = 0x1234;
	fielddata[0].bSubTile = 5;
	fielddata[0].bMapData2 = 6;
	fielddata[0].wGround = 0x5678;
	fielddata[0].overlay = 9;
	fielddata[0].overlaydata = 10;
	fielddata[0].bRNDImage = 11;

	CMapSnapshots snapshots;
	snapshots.TakeSnapshot(fielddata, 1, TRUE, 0, 0, 1, 1);
	fielddata[0].bRedrawTerrain = FALSE;
	fielddata[0].bHeight = 0;
	fielddata[0].bMapData = 0;
	fielddata[0].bSubTile = 0;
	fielddata[0].bMapData2 = 0;
	fielddata[0].wGround = 0;
	fielddata[0].overlay = 0;
	fielddata[0].overlaydata = 0;
	fielddata[0].bRNDImage = 0;

	TEST(snapshots.Undo(fielddata, 1, {}, {}));
	REPORT_TEST(fielddata[0].bRedrawTerrain == TRUE);
	REPORT_TEST(fielddata[0].bHeight == 7);
	REPORT_TEST(fielddata[0].bMapData == 0x1234);
	REPORT_TEST(fielddata[0].bSubTile == 5);
	REPORT_TEST(fielddata[0].bMapData2 == 6);
	REPORT_TEST(fielddata[0].wGround == 0x5678);
	REPORT_TEST(fielddata[0].overlay == 9);
	REPORT_TEST(fielddata[0].overlaydata == 10);
	REPORT_TEST(fielddata[0].bRNDImage == 11);

	// The history must stay bounded and discarding a redo branch must not
	// retain buffers from the removed snapshots.
	for (int i = 0; i < CMapSnapshots::MAX_SNAPSHOTS + 2; ++i)
	{
		fielddata[0].bHeight = static_cast<BYTE>(i);
		snapshots.TakeSnapshot(fielddata, 1, TRUE, 0, 0, 1, 1);
	}
	TEST(snapshots.Undo(fielddata, 1, {}, {}));
	fielddata[0].bHeight = 42;
	snapshots.TakeSnapshot(fielddata, 1, TRUE, 0, 0, 1, 1);
	REPORT_TEST(!snapshots.CanRedo());
}

void Tests::test_waypoint_codec()
{
	REPORT_TEST(GetWaypoint("A") == 0);
	REPORT_TEST(GetWaypoint("Z") == 25);
	REPORT_TEST(GetWaypoint("AA") == 26);
	REPORT_TEST(GetWaypoint("ZZ") == 701);
	REPORT_TEST(GetWaypoint("AAA") == 702);
	REPORT_TEST(GetWaypoint("aBc") == 730);
	REPORT_TEST(GetWaypoint("A1") == -1);
	REPORT_TEST(GetWaypoint(0) == CString("A"));
	REPORT_TEST(GetWaypoint(701) == CString("ZZ"));
	REPORT_TEST(GetWaypoint(702) == CString("AAA"));
	REPORT_TEST(GetWaypoint(18277) == CString("ZZZ"));
}

void Tests::test_codecs()
{
	// base64 roundtrip through the Rust-backed FSunPackLib API
	{
		std::vector<BYTE> data(300);
		for (size_t i = 0; i < data.size(); i++) data[i] = (BYTE)(i * 7 + 3);
		BYTE* b64 = FSunPackLib::EncodeBase64(data.data(), (UINT)data.size());
		TEST(b64 != nullptr);
		std::vector<BYTE> decoded;
		int len = FSunPackLib::DecodeBase64((const char*)b64, decoded);
		delete[] b64;
		REPORT_TEST(len == (int)data.size());
		REPORT_TEST(decoded == data);
	}

	// sectioned Format80 (overlay pack) roundtrip
	{
		std::vector<BYTE> overlay(262144, 0xFF);
		for (size_t i = 0; i < overlay.size(); i++) overlay[i] = (BYTE)(i % 251);
		BYTE* packed = nullptr;
		int packedLen = FSunPackLib::EncodeF80(overlay.data(), 262144, 32, &packed);
		TEST(packed != nullptr && packedLen > 0);
		std::vector<BYTE> unpacked(262144, 0);
		bool ok = FSunPackLib::DecodeF80(packed, packedLen, unpacked, 262144);
		delete[] packed;
		REPORT_TEST(ok);
		REPORT_TEST(unpacked == overlay);
	}

	// IsoMapPack5 (LZO sections) roundtrip
	{
		const size_t mapCells = 200 * 200;
		std::vector<BYTE> mfd(mapCells * MAPFIELDDATA_SIZE);
		for (size_t i = 0; i < mfd.size(); i++) mfd[i] = (BYTE)(i % 47);
		BYTE* packed5 = nullptr;
		UINT packed5Len = FSunPackLib::EncodeIsoMapPack5(mfd.data(), (UINT)mfd.size(), &packed5);
		TEST(packed5 != nullptr && packed5Len > 0);
		UINT needed = FSunPackLib::DecodeIsoMapPack5(packed5, packed5Len, NULL, 0, NULL, TRUE);
		REPORT_TEST(needed == mfd.size());
		std::vector<BYTE> unpacked5(needed);
		UINT got = FSunPackLib::DecodeIsoMapPack5(packed5, packed5Len, unpacked5.data(), unpacked5.size(), NULL, TRUE);
		delete[] packed5;
		REPORT_TEST(got == needed);
		REPORT_TEST(unpacked5 == mfd);
	}

	// corrupt data is rejected instead of corrupting the heap
	{
		BYTE junk[] = { 0x10, 0x00, 0x00, 0x08 };
		std::vector<BYTE> unpacked(262144, 0);
		REPORT_TEST(FSunPackLib::DecodeIsoMapPack5(junk, sizeof(junk), NULL, 0, NULL, TRUE) == 0);
		REPORT_TEST(FSunPackLib::DecodeF80(junk, sizeof(junk), unpacked, 262144) == false);
	}
}

void Tests::test_csf()
{
	auto push_u32 = [](std::vector<BYTE>& v, unsigned int x) {
		v.push_back((BYTE)(x & 0xFF));
		v.push_back((BYTE)((x >> 8) & 0xFF));
		v.push_back((BYTE)((x >> 16) & 0xFF));
		v.push_back((BYTE)((x >> 24) & 0xFF));
	};

	// build a minimal CSF file: " FSC" + 20-byte header + one entry
	std::vector<BYTE> csf;
	const char* id = "Name:ABC";
	const wchar_t* value = L"hello";
	const size_t vlen = wcslen(value);
	csf.insert(csf.end(), (const BYTE*)" FSC", (const BYTE*)" FSC" + 4);
	push_u32(csf, 1); push_u32(csf, 1); push_u32(csf, 0); push_u32(csf, 0); push_u32(csf, 0);
	csf.insert(csf.end(), (const BYTE*)" LBL", (const BYTE*)" LBL" + 4);
	push_u32(csf, 1); // strings for this label
	push_u32(csf, (unsigned int)strlen(id));
	csf.insert(csf.end(), (const BYTE*)id, (const BYTE*)id + strlen(id));
	csf.insert(csf.end(), (const BYTE*)" RTS", (const BYTE*)" RTS" + 4);
	push_u32(csf, (unsigned int)vlen);
	for (size_t i = 0; i < vlen; i++)
	{
		unsigned short w = (unsigned short)~value[i];
		csf.push_back((BYTE)(w & 0xFF));
		csf.push_back((BYTE)((w >> 8) & 0xFF));
	}

	size_t entry_count = 0, ids_len = 0, values_len = 0, values_asc_len = 0;
	int truncated = 0;
	int res = rs_csf_parse(csf.data(), csf.size(),
		NULL, 0, &entry_count,
		NULL, 0, &ids_len,
		NULL, 0, &values_len,
		NULL, 0, &values_asc_len, &truncated);
	REPORT_TEST(res == RS_ERR_SMALL_BUFFER);
	REPORT_TEST(entry_count == 1);
	REPORT_TEST(ids_len == strlen(id));
	REPORT_TEST(values_len == vlen * 2);

	std::vector<rs_csf_entry> entries(entry_count);
	std::vector<BYTE> ids(ids_len), values(values_len);
	res = rs_csf_parse(csf.data(), csf.size(),
		entries.data(), entries.size(), &entry_count,
		ids.data(), ids.size(), &ids_len,
		values.data(), values.size(), &values_len,
		NULL, 0, &values_asc_len, &truncated);
	REPORT_TEST(res == RS_OK);
	REPORT_TEST(truncated == 0);
	REPORT_TEST(memcmp(ids.data(), id, strlen(id)) == 0);
	bool valuesMatch = true;
	for (size_t i = 0; i < vlen; i++)
		valuesMatch = valuesMatch && ((const WCHAR*)values.data())[i] == value[i];
	REPORT_TEST(valuesMatch);

	// a truncated file stops cleanly with the partial result reported
	std::vector<BYTE> cut(csf.begin(), csf.end() - 3);
	size_t c_entry_count = 0, c_ids_len = 0, c_values_len = 0, c_values_asc_len = 0;
	res = rs_csf_parse(cut.data(), cut.size(),
		NULL, 0, &c_entry_count,
		NULL, 0, &c_ids_len,
		NULL, 0, &c_values_len,
		NULL, 0, &c_values_asc_len, &truncated);
	REPORT_TEST(c_entry_count == 0);
	REPORT_TEST(truncated == 1);

	// a file without the " FSC" marker is rejected
	BYTE junk[16] = { 0 };
	size_t j = 0;
	res = rs_csf_parse(junk, sizeof(junk), NULL, 0, &j, NULL, 0, &j, NULL, 0, &j, NULL, 0, &j, &truncated);
	REPORT_TEST(res == RS_ERR_BAD_ARG);

#ifdef RA2_MODE
	// Trigger dialogs resolve only the display preview through the cached
	// table. The serialized value remains the original CSF key.
	AllStrings[id].SetString(value, static_cast<int>(vlen));
	CString resolved;
	REPORT_TEST(IsCsfParamListType(PARAMTYPE_TUTORIALTEXTS));
	REPORT_TEST(IsCsfParamListType(PARAMTYPE_CSFSTRINGS));
	REPORT_TEST(TryGetCsfString(id, resolved));
	REPORT_TEST(resolved == CString("hello"));
	REPORT_TEST(TryGetCsfString("name:abc", resolved));

	AllStrings["MISSION:UNTHK06"].SetString(L"Mission text", 12);
	REPORT_TEST(TryGetCsfString("mission:unthk06", resolved));
	REPORT_TEST(resolved == CString("Mission text"));

	REPORT_TEST(TryGetCsfString(id, resolved));
	CString serialized=id;
	serialized+=" : ";
	serialized+=resolved;
	TruncSpace(serialized);
	REPORT_TEST(serialized == CString(id));
	AllStrings.erase(id);
	AllStrings.erase("mission:unthk06");
#endif
}

void Tests::test_rules_section_cache()
{
	CMapData mapData;
	CIniFile& ini = mapData.GetIniFile();

	// a house is never a rules section
	ini.sections["Houses"].values["0"] = "Americans";

	// weapon heuristic: ROF/Range/Damage/Warhead
	ini.sections["MyWeapon"].values["ROF"] = "50";
	ini.sections["MyWeapon"].values["Range"] = "5";
	ini.sections["MyWeapon"].values["Damage"] = "10";
	ini.sections["MyWeapon"].values["Warhead"] = "MyWarhead";

	// warhead heuristic: Spread/Range/Damage/Warhead
	ini.sections["MyWarhead"].values["Spread"] = "2";
	ini.sections["MyWarhead"].values["Range"] = "5";
	ini.sections["MyWarhead"].values["Damage"] = "10";
	ini.sections["MyWarhead"].values["Warhead"] = "none";

	// a Projectile= reference marks the target as rules data
	ini.sections["MyWeapon"].values["Projectile"] = "MyProjectile";
	ini.sections["MyProjectile"].values["Speed"] = "100";

	// type list values are rules sections
	ini.sections["BuildingTypes"].values["0"] = "GAGAP";

	// a plain map section stays a map section
	ini.sections["MyMapSection"].values["Foo"] = "Bar";

	mapData.BuildRulesSectionCache();

	REPORT_TEST(!mapData.IsRulesSection("Americans"));
	REPORT_TEST(mapData.IsRulesSection("MyWeapon"));
	REPORT_TEST(mapData.IsRulesSection("MyWarhead"));
	REPORT_TEST(mapData.IsRulesSection("MyProjectile"));
	REPORT_TEST(mapData.IsRulesSection("GAGAP"));
	REPORT_TEST(!mapData.IsRulesSection("MyMapSection"));

	// the cache must not serve stale results after new references appear
	ini.sections["NewWeapon"].values["ROF"] = "50";
	ini.sections["NewWeapon"].values["Range"] = "5";
	ini.sections["NewWeapon"].values["Damage"] = "10";
	ini.sections["NewWeapon"].values["Warhead"] = "none";
	ini.sections["NewWeapon"].values["Projectile"] = "NewProjectile";
	REPORT_TEST(!mapData.IsRulesSection("NewProjectile"));
	mapData.BuildRulesSectionCache();
	REPORT_TEST(mapData.IsRulesSection("NewProjectile"));
	REPORT_TEST(mapData.IsRulesSection("NewWeapon"));
}
