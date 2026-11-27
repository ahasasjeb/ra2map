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
#include "PanScroll.h"
#include "VulkanScene.h"
#include "TileSetVulkanRecovery.h"
#include "TileSetBrowserView.h"
#include "CrashReporter.h"
#include "MapSaveUtil.h"
#include "functions.h"

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
		[this]() { test_pan_scroll(); },
		[this]() { test_vulkan_color_and_bitmap(); },
		[this]() { test_tileset_vulkan_recovery(); },
		[this]() { test_tileset_browser_long_list_coords(); },
		[this]() { test_crash_reporter(); },
		[this]() { test_map_save_and_emergency_backup(); },
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

void Tests::test_pan_scroll()
{
	// Equal elapsed time must give the same travel despite uneven frame times.
	PanScrollMotion regular, delayed;
	CPoint regularTotal(0, 0), delayedTotal(0, 0);
	for (int i = 0; i < 60; ++i)
		regularTotal += regular.Advance(17, -7, 0.016);
	for (int i = 0; i < 30; ++i)
	{
		delayedTotal += delayed.Advance(17, -7, 0.011);
		delayedTotal += delayed.Advance(17, -7, 0.021);
	}
	TEST(abs(regularTotal.x - 340) <= 1 && abs(regularTotal.y + 140) <= 1);
	TEST(abs(delayedTotal.x - regularTotal.x) <= 1);
	TEST(abs(delayedTotal.y - regularTotal.y) <= 1);

	PanScrollMotion slow;
	TEST(slow.Advance(1, -1, 0.024) == CPoint(0, 0));
	TEST(slow.Advance(1, -1, 0.024) == CPoint(1, -1));
	slow.Reset();
	TEST(slow.Advance(1, 0, 0.024).x == 0);
	TEST(slow.Advance(-1, 0, 0.024).x == 0); // reverse without stale drift
	TEST(slow.Advance(-1, 0, 0.024).x == -1);
	slow.Reset();
	slow.Advance(1, 1, 0.024);
	slow.Clamp(true, false);
	TEST(slow.Advance(1, 1, 0.024) == CPoint(0, 1));
	slow.Reset();
	TEST(slow.Advance(48, -48, 3.0) == CPoint(50, -50)); // stalled UI
	slow.Reset();
	TEST(slow.Advance(48, -48, -0.1) == CPoint(0, 0));

	// Every newly exposed pixel must be painted exactly once, including the
	// diagonal corner. Double painting changes semi-transparent shadow colors.
	const RECT area = { 13, 21, 22, 28 };
	const auto contains = [](const RECT& rect, int x, int y)
	{
		return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
	};
	for (int dx = -12; dx <= 12; ++dx)
	{
		for (int dy = -10; dy <= 10; ++dy)
		{
			const auto bands = GetPanExposedBands(area, dx, dy);
			for (int y = area.top; y < area.bottom; ++y)
			{
				for (int x = area.left; x < area.right; ++x)
				{
					const int paints = contains(bands.x, x, y) + contains(bands.y, x, y);
					TEST(paints == (contains(area, x - dx, y - dy) ? 0 : 1));
				}
			}
		}
	}
	REPORT_TEST(true);
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

void Tests::test_vulkan_color_and_bitmap()
{
	FSunPackLib::ColorConverter conv;
	REPORT_TEST(conv.GetColor(0x12, 0x34, 0x56) == 0x00123456);
	REPORT_TEST(conv.GetColor(0xAB, 0x12, 0x34, 0x56) == static_cast<int>(0xAB123456u));
	REPORT_TEST(conv.GetColor(RGB(0x12, 0x34, 0x56)) == 0x00123456);

	// Create a 2x2 32-bit DIB section: top-left white (key), top-right red,
	// bottom-left green, bottom-right white (key).
	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = 2;
	bmi.bmiHeader.biHeight = -2; // top-down
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void* bits = nullptr;
	HBITMAP hbm = ::CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
	REPORT_TEST(hbm != nullptr && bits != nullptr);
	auto* px = static_cast<uint32_t*>(bits);
	px[0] = 0x00FFFFFFu; // white
	px[1] = 0x00FF0000u; // red (BGRA32: R=0xFF)
	px[2] = 0x0000FF00u; // green
	px[3] = 0x00FFFFFFu; // white

	CBitmap bmp;
	bmp.Attach(hbm);

	auto rgbaAutoKey = BitmapToRgbaBitmap(bmp, CLR_INVALID);
	REPORT_TEST(rgbaAutoKey != nullptr);
	REPORT_TEST(rgbaAutoKey->width == 2 && rgbaAutoKey->height == 2);
	REPORT_TEST(rgbaAutoKey->pixels.size() == 4);
	REPORT_TEST(rgbaAutoKey->pixels[0] == 0u);
	REPORT_TEST(rgbaAutoKey->pixels[1] == 0xFFFF0000u);
	REPORT_TEST(rgbaAutoKey->pixels[2] == 0xFF00FF00u);
	REPORT_TEST(rgbaAutoKey->pixels[3] == 0u);

	auto rgbaExplicitKey = BitmapToRgbaBitmap(bmp, RGB(0, 255, 0));
	REPORT_TEST(rgbaExplicitKey != nullptr);
	REPORT_TEST(rgbaExplicitKey->id != rgbaAutoKey->id);
	REPORT_TEST(rgbaExplicitKey->pixels[0] == 0xFFFFFFFFu);
	REPORT_TEST(rgbaExplicitKey->pixels[1] == 0xFFFF0000u);
	REPORT_TEST(rgbaExplicitKey->pixels[2] == 0u);
	REPORT_TEST(rgbaExplicitKey->pixels[3] == 0xFFFFFFFFu);
}

void Tests::test_tileset_vulkan_recovery()
{
	struct RecoveryHarness
	{
		TileSetVulkanRecovery recovery{ 3, 250, 3, 50 };
		bool visible = true;
		bool minimized = false;
		int width = 240;
		int height = 320;
		bool rendererAlive = false;
		bool retryTimerActive = false;
		bool transientTimerActive = false;
		uint64_t activeRetryToken = 0;
		uint64_t activeTransientToken = 0;
		int createCount = 0;
		int destroyCount = 0;
		int presentCount = 0;
		int invalidateCount = 0;
		int timerStartCount = 0;
		int timerCancelCount = 0;
		std::vector<std::string> logs;

		std::function<TileSetVulkanInitResult(std::string&)> onInit;
		std::function<VulkanPresentResult(std::string&)> onPresent;
		std::function<bool(uint64_t, unsigned int)> onSetTimer;

		void Apply(const TileSetVulkanRecoveryAction& action)
		{
			if (action.shouldReleaseRenderer && rendererAlive)
			{
				rendererAlive = false;
				++destroyCount;
			}
			if (action.cancelRetryTimer && retryTimerActive)
			{
				retryTimerActive = false;
				activeRetryToken = 0;
				++timerCancelCount;
			}
			if (action.cancelTransientTimer && transientTimerActive)
			{
				transientTimerActive = false;
				activeTransientToken = 0;
			}
			if (!action.logMessage.empty())
				logs.push_back(action.logMessage);
			if (action.startRetryTimer)
			{
				if (onSetTimer && !onSetTimer(action.retryTimerToken, action.retryDelayMs))
				{
					Apply(recovery.OnRetryTimerScheduleFailed());
				}
				else
				{
					retryTimerActive = true;
					activeRetryToken = action.retryTimerToken;
					++timerStartCount;
				}
			}
			if (action.startTransientTimer)
			{
				if (onSetTimer && !onSetTimer(action.transientTimerToken, action.transientDelayMs))
				{
					Apply(recovery.OnTransientTimerScheduleFailed());
				}
				else
				{
					transientTimerActive = true;
					activeTransientToken = action.transientTimerToken;
				}
			}
			if (action.invalidate)
				++invalidateCount;
		}

		void Paint()
		{
			TileSetVulkanRecoveryAction gate{};
			if (!recovery.CanAttemptRender(visible && !minimized, width, height, gate))
			{
				Apply(gate);
				return;
			}

			std::string error;
			if (!rendererAlive)
			{
				++createCount;
				const TileSetVulkanInitResult initRes = onInit ? onInit(error) : TileSetVulkanInitResult::Succeeded;
				if (initRes != TileSetVulkanInitResult::Failed)
					rendererAlive = true;
				else
					++destroyCount; // failed init cleans up partial resources

				if (initRes != TileSetVulkanInitResult::Succeeded)
				{
					Apply(recovery.OnInitializeResult(initRes, error));
					return;
				}
			}

			++presentCount;
			error.clear();
			const VulkanPresentResult presentRes = onPresent ? onPresent(error) : VulkanPresentResult::Succeeded;
			Apply(recovery.OnPresentResult(presentRes, error));
		}

		void FireRetryTimerToken(uint64_t token)
		{
			Apply(recovery.OnRetryTimer(token, visible && !minimized, width, height));
		}

		void FireRetryTimer()
		{
			FireRetryTimerToken(activeRetryToken);
		}

		void FireTransientTimerToken(uint64_t token)
		{
			Apply(recovery.OnTransientSwapchainTimer(token, visible && !minimized, width, height));
		}

		void FireTransientTimer()
		{
			FireTransientTimerToken(activeTransientToken);
		}

		void Resize(bool isMinimized, int w, int h)
		{
			minimized = isMinimized;
			width = w;
			height = h;
			Apply(recovery.OnSize(minimized, width, height, visible));
		}

		void SetVisible(bool isVisible)
		{
			visible = isVisible;
			Apply(recovery.OnShowWindow(visible && !minimized, width, height));
		}

		void ManualRetry()
		{
			Apply(recovery.RequestManualRetry(visible && !minimized, width, height));
		}

		void Destroy()
		{
			Apply(recovery.OnDestroy());
		}
	};

	// 1. Init succeeds, present continuously fails -> bounded retries, then stop.
	{
		RecoveryHarness h;
		h.onPresent = [](std::string& err)
		{
			err = "scene present: ERROR_DEVICE_LOST";
			return VulkanPresentResult::Failed;
		};

		h.Paint(); // initial failure -> retry #1 scheduled
		TEST(h.createCount == 1 && h.destroyCount == 1 && !h.rendererAlive);
		TEST(h.recovery.GetState() == TileSetVulkanRecoveryState::RetryScheduled);
		TEST(h.retryTimerActive);
		TEST(h.invalidateCount == 0); // never unconditionally invalidate on failure
		TEST(h.logs.size() == 1);

		// Extra paints / resize / scroll while waiting for timer must NOT recreate Vulkan.
		h.Paint();
		h.Paint();
		h.Resize(false, 260, 340);
		h.Paint();
		TEST(h.createCount == 1 && h.destroyCount == 1 && h.presentCount == 1);

		// Exhaust all 3 delayed retries.
		for (int retry = 1; retry <= 3; ++retry)
		{
			h.FireRetryTimer();
			TEST(!h.retryTimerActive);
			TEST(h.recovery.GetState() == TileSetVulkanRecoveryState::RetryDue);
			h.Paint();
			TEST(h.createCount == 1 + retry && h.destroyCount == 1 + retry && !h.rendererAlive);
		}

		TEST(h.recovery.IsStopped());
		TEST(!h.retryTimerActive);
		TEST(h.logs.size() == 2); // first useful error + final stopped log only

		// Further paints, resizes, minimize/restore must NOT bypass the cap.
		for (int i = 0; i < 5; ++i)
			h.Paint();
		h.Resize(true, 0, 0);
		h.Resize(false, 300, 400);
		h.Paint();
		TEST(h.createCount == 4 && h.destroyCount == 4 && h.presentCount == 4);
		TEST(h.recovery.IsStopped());
		TEST(h.logs.size() == 2);

		// User explicit retry after fault clears recovers cleanly.
		h.onPresent = [](std::string&) { return VulkanPresentResult::Succeeded; };
		h.ManualRetry();
		TEST(h.recovery.GetState() == TileSetVulkanRecoveryState::Healthy);
		TEST(h.recovery.GetConsecutiveFailures() == 0);
		h.Paint();
		TEST(h.createCount == 5 && h.destroyCount == 4 && h.rendererAlive);
		TEST(h.recovery.GetState() == TileSetVulkanRecoveryState::Healthy);
	}

	// 2. Single transient present failure recovers automatically and resets counter.
	{
		RecoveryHarness h;
		int failRemaining = 1;
		h.onPresent = [&failRemaining](std::string& err)
		{
			if (failRemaining > 0)
			{
				--failRemaining;
				err = "scene submit: ERROR_DEVICE_LOST";
				return VulkanPresentResult::Failed;
			}
			return VulkanPresentResult::Succeeded;
		};

		h.Paint();
		TEST(h.createCount == 1 && h.destroyCount == 1 && !h.rendererAlive);
		TEST(h.recovery.GetState() == TileSetVulkanRecoveryState::RetryScheduled);
		h.FireRetryTimer();
		h.Paint();
		TEST(h.createCount == 2 && h.destroyCount == 1 && h.rendererAlive);
		TEST(h.recovery.GetState() == TileSetVulkanRecoveryState::Healthy);
		TEST(h.recovery.GetConsecutiveFailures() == 0);

		// A later failure gets a fresh full retry budget and logs a fresh first error.
		failRemaining = 1;
		h.Paint();
		TEST(h.recovery.GetConsecutiveFailures() == 1);
		TEST(h.recovery.GetState() == TileSetVulkanRecoveryState::RetryScheduled);
		TEST(h.logs.size() == 2);
	}

	// 3. Pure initialization failure is also bounded and recoverable via manual retry.
	{
		RecoveryHarness h;
		bool failInit = true;
		h.onInit = [&failInit](std::string& err)
		{
			if (failInit)
			{
				err = "vkCreateDevice failed: ERROR_INITIALIZATION_FAILED";
				return TileSetVulkanInitResult::Failed;
			}
			return TileSetVulkanInitResult::Succeeded;
		};

		h.Paint();
		for (int i = 0; i < 3; ++i)
		{
			h.FireRetryTimer();
			h.Paint();
		}
		TEST(h.recovery.IsStopped());
		TEST(h.createCount == 4 && h.presentCount == 0 && !h.rendererAlive);
		TEST(h.logs.size() == 2);

		h.Paint();
		TEST(h.createCount == 4);

		failInit = false;
		h.ManualRetry();
		h.Paint();
		TEST(h.recovery.GetState() == TileSetVulkanRecoveryState::Healthy);
		TEST(h.rendererAlive && h.presentCount == 1);
	}

	// 4. Minimize/zero-size and normal resize swapchain out-of-date are not permanent failures.
	{
		RecoveryHarness h;
		h.Paint();
		TEST(h.rendererAlive && h.createCount == 1 && h.destroyCount == 0);

		// Minimized client rect and Vulkan Minimized status do not destroy device or count failure.
		h.Resize(true, 0, 0);
		h.Paint();
		h.Resize(false, 240, 320);
		h.onPresent = [](std::string&) { return VulkanPresentResult::Minimized; };
		h.Paint();
		TEST(h.rendererAlive && h.destroyCount == 0 && h.recovery.GetConsecutiveFailures() == 0);
		TEST(h.recovery.GetState() == TileSetVulkanRecoveryState::Healthy);

		// Live resize causing repeated SwapchainOutOfDate across size changes stays Healthy.
		h.onPresent = [](std::string&) { return VulkanPresentResult::SwapchainOutOfDate; };
		for (int step = 0; step < 6; ++step)
		{
			h.Resize(false, 240 + step * 10, 320 + step * 5);
			h.Paint();
		}
		TEST(h.rendererAlive && h.destroyCount == 0 && h.recovery.GetConsecutiveFailures() == 0);
		TEST(h.logs.empty());

		// After resize settles, normal present succeeds.
		h.onPresent = [](std::string&) { return VulkanPresentResult::Succeeded; };
		h.FireTransientTimer();
		h.Paint();
		TEST(h.rendererAlive && h.recovery.GetState() == TileSetVulkanRecoveryState::Healthy);

		// Endless SwapchainOutOfDate at constant size escalates instead of looping forever.
		h.onPresent = [](std::string&) { return VulkanPresentResult::SwapchainOutOfDate; };
		for (int i = 0; i < 4; ++i)
		{
			h.Paint();
			if (h.transientTimerActive)
				h.FireTransientTimer();
		}
		TEST(!h.rendererAlive && h.destroyCount == 1);
		TEST(h.recovery.GetState() == TileSetVulkanRecoveryState::RetryScheduled);
	}

	// 5. Minimize/hide/destroy during pending retry cancels timers and preserves cap.
	{
		RecoveryHarness h;
		h.onPresent = [](std::string& err)
		{
			err = "scene present: ERROR_SURFACE_LOST_KHR";
			return VulkanPresentResult::Failed;
		};

		h.Paint(); // failure #1 -> RetryScheduled
		TEST(h.retryTimerActive && h.recovery.GetConsecutiveFailures() == 1);

		// Hiding the sidebar cancels the pending retry timer immediately.
		h.SetVisible(false);
		TEST(!h.retryTimerActive);
		TEST(h.recovery.GetState() == TileSetVulkanRecoveryState::RetryPaused);
		TEST(h.recovery.GetConsecutiveFailures() == 1);

		// Showing again resumes retry scheduling without resetting failure count.
		h.SetVisible(true);
		TEST(h.retryTimerActive);
		TEST(h.recovery.GetState() == TileSetVulkanRecoveryState::RetryScheduled);
		TEST(h.recovery.GetConsecutiveFailures() == 1);

		// Closing/destroying the window while retry is pending cancels timer cleanly.
		h.Destroy();
		TEST(!h.retryTimerActive);
		const int createsBefore = h.createCount;
		h.FireRetryTimer();
		h.Paint();
		h.ManualRetry();
		TEST(h.createCount == createsBefore);
	}

		// 5b. Consecutive repaints while waiting for the transient timer must NOT
		// consume the transient out-of-date budget or escalate to a hard failure.
		{
			RecoveryHarness h;
			h.onPresent = [](std::string&) { return VulkanPresentResult::SwapchainOutOfDate; };
			h.Paint();
			TEST(h.rendererAlive && h.presentCount == 1);
			TEST(h.transientTimerActive && h.recovery.IsWaitingForTransientRetry());
			TEST(h.recovery.GetConsecutiveSwapchainRetries() == 1);
			TEST(h.recovery.GetConsecutiveFailures() == 0);
			TEST(h.recovery.GetState() == TileSetVulkanRecoveryState::Healthy);

			// Repeat 6 ordinary repaints + redundant same-size WM_SIZE without firing timer.
			for (int i = 0; i < 6; ++i)
				h.Paint();
			h.Resize(false, h.width, h.height);
			h.Paint();
			TEST(h.presentCount == 1);
			TEST(h.rendererAlive && h.destroyCount == 0);
			TEST(h.recovery.GetConsecutiveSwapchainRetries() == 1);
			TEST(h.recovery.GetConsecutiveFailures() == 0);
			TEST(h.recovery.GetState() == TileSetVulkanRecoveryState::Healthy);
			TEST(h.transientTimerActive);

			// Once the active transient timer fires, the next paint proceeds and recovers.
			h.onPresent = [](std::string&) { return VulkanPresentResult::Succeeded; };
			h.FireTransientTimer();
			TEST(!h.transientTimerActive && !h.recovery.IsWaitingForTransientRetry());
			h.Paint();
			TEST(h.presentCount == 2 && h.rendererAlive);
			TEST(h.recovery.GetConsecutiveSwapchainRetries() == 0);
			TEST(h.recovery.GetState() == TileSetVulkanRecoveryState::Healthy);
		}

		// 5c. After cancel & reschedule, stale timer tokens from the previous round
		// must NOT wake the new round (for both retry and transient timers).
		{
			RecoveryHarness h;
			h.onPresent = [](std::string& err)
			{
				err = "scene present: ERROR_DEVICE_LOST";
				return VulkanPresentResult::Failed;
			};

			h.Paint(); // failure #1 -> RetryScheduled with token1
			const uint64_t staleRetryToken = h.activeRetryToken;
			TEST(staleRetryToken != 0 && h.retryTimerActive);

			// Hide cancels token1; show reschedules with a distinct token2.
			h.SetVisible(false);
			TEST(!h.retryTimerActive && h.recovery.GetState() == TileSetVulkanRecoveryState::RetryPaused);
			h.SetVisible(true);
			const uint64_t freshRetryToken = h.activeRetryToken;
			TEST(freshRetryToken != 0 && freshRetryToken != staleRetryToken);
			TEST(h.recovery.GetState() == TileSetVulkanRecoveryState::RetryScheduled);

			// Stale queued WM_TIMER carrying token1 must NOT wake the new round.
			const int invalidatesBefore = h.invalidateCount;
			h.FireRetryTimerToken(staleRetryToken);
			TEST(h.recovery.GetState() == TileSetVulkanRecoveryState::RetryScheduled);
			TEST(h.retryTimerActive && h.activeRetryToken == freshRetryToken);
			TEST(h.invalidateCount == invalidatesBefore);
			h.Paint();
			TEST(h.createCount == 1 && h.presentCount == 1);

			// Genuine token2 wakes the new round.
			h.FireRetryTimerToken(freshRetryToken);
			TEST(h.recovery.GetState() == TileSetVulkanRecoveryState::RetryDue);
			TEST(!h.retryTimerActive);
			h.onPresent = [](std::string&) { return VulkanPresentResult::Succeeded; };
			h.Paint();
			TEST(h.createCount == 2 && h.presentCount == 2 && h.rendererAlive);
			TEST(h.recovery.GetState() == TileSetVulkanRecoveryState::Healthy);

			// Same generation isolation for transient swapchain timers.
			h.onPresent = [](std::string&) { return VulkanPresentResult::SwapchainOutOfDate; };
			h.Paint();
			const uint64_t staleTransientToken = h.activeTransientToken;
			TEST(staleTransientToken != 0 && h.transientTimerActive);

			// Resize cancels old transient timer; next Paint schedules a fresh generation.
			h.Resize(false, 280, 360);
			TEST(!h.transientTimerActive);
			h.Paint();
			const uint64_t freshTransientToken = h.activeTransientToken;
			TEST(freshTransientToken != 0 && freshTransientToken != staleTransientToken);
			const int presentsBefore = h.presentCount;

			// Stale transient token must NOT unblock painting or cancel the active timer.
			h.FireTransientTimerToken(staleTransientToken);
			TEST(h.transientTimerActive && h.activeTransientToken == freshTransientToken);
			TEST(h.recovery.IsWaitingForTransientRetry());
			h.Paint();
			TEST(h.presentCount == presentsBefore);

			// Genuine transient token unblocks painting.
			h.onPresent = [](std::string&) { return VulkanPresentResult::Succeeded; };
			h.FireTransientTimerToken(freshTransientToken);
			TEST(!h.transientTimerActive && !h.recovery.IsWaitingForTransientRetry());
			h.Paint();
			TEST(h.presentCount == presentsBefore + 1);
		}

		// 5d. Transient SetTimer failure must not strand the view in a permanent wait.
		{
			RecoveryHarness h;
			h.onPresent = [](std::string&) { return VulkanPresentResult::SwapchainOutOfDate; };
			// Fail transient SetTimer only; allow retry SetTimer to succeed.
			h.onSetTimer = [](uint64_t token, unsigned int)
			{
				return !TileSetVulkanRecovery::IsTransientTimerToken(token);
			};
			h.Paint();
			TEST(!h.transientTimerActive && !h.recovery.IsWaitingForTransientRetry());
			TEST(!h.rendererAlive && h.destroyCount == 1);
			TEST(h.recovery.GetState() == TileSetVulkanRecoveryState::RetryScheduled);
			TEST(h.retryTimerActive);
			TEST(h.logs.size() == 1);

			// If SetTimer also fails for the retry timer, recovery stops cleanly with fallback.
			h.onSetTimer = [](uint64_t, unsigned int) { return false; };
			h.FireRetryTimer();
			h.Paint();
			TEST(!h.transientTimerActive && !h.retryTimerActive);
			TEST(h.recovery.IsStopped());
			TEST(h.recovery.ShouldDrawFallback());
			TEST(h.logs.size() == 2);
		}

		// 6. Real CTileSetBrowserView window integration test using test double hooks.
	{
		if (AfxGetInstanceHandle() == nullptr)
			AfxWinInit(::GetModuleHandle(nullptr), nullptr, ::GetCommandLine(), 0);

		CWnd hostWindow;
		LPCTSTR hostClass = AfxRegisterWndClass(0);
		const BOOL hostCreated = hostWindow.CreateEx(
			0, hostClass, "TileSetRecoveryTestHost",
			WS_POPUP | WS_VISIBLE,
			-32000, -32000, 320, 240,
			nullptr, 0);
		TEST(hostCreated != FALSE);

		CTileSetBrowserView view;
		const BOOL viewCreated = view.Create(
			nullptr, nullptr,
			WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL,
			CRect(0, 0, 320, 240), &hostWindow, 1001, nullptr);
		TEST(viewCreated != FALSE);
		view.SetScrollSizes(MM_TEXT, CSize(320, 1000));

		int creates = 0;
		int destroys = 0;
		int presents = 0;
		VulkanPresentResult nextPresentResult = VulkanPresentResult::Failed;
		bool allowRetrySetTimer = true;
		bool allowTransientSetTimer = true;
		std::vector<std::string> viewLogs;

		CTileSetBrowserView::VulkanTestHooks hooks{};
		hooks.initialize = [&](int, int, bool, std::string&)
		{
			++creates;
			return TileSetVulkanInitResult::Succeeded;
		};
		hooks.present = [&](const RECT&, int, int, bool, std::string& errorOut)
		{
			++presents;
			if (nextPresentResult == VulkanPresentResult::Failed)
				errorOut = "mock present failure";
			return nextPresentResult;
		};
		hooks.release = [&]() { ++destroys; };
		hooks.log = [&](const std::string& msg) { viewLogs.push_back(msg); };
		hooks.setTimer = [&](UINT_PTR timerId, unsigned int)
		{
			if (TileSetVulkanRecovery::IsRetryTimerToken(static_cast<uint64_t>(timerId)))
				return allowRetrySetTimer;
			if (TileSetVulkanRecovery::IsTransientTimerToken(static_cast<uint64_t>(timerId)))
				return allowTransientSetTimer;
			return true;
		};

		view.SetVulkanTestHooks(&hooks);
		view.SetRecoveryConfigForTest(3, 15, 3, 15);

		auto drawView = [&]()
		{
			CClientDC dc(&view);
			view.TriggerDrawForTest(&dc);
		};

		// Initial failure + 3 delayed retries -> Stopped.
		drawView();
		TEST(creates == 1 && destroys == 1 && presents == 1);
		TEST(view.IsRetryTimerActiveForTest());
		for (int retry = 1; retry <= 3; ++retry)
		{
			const UINT_PTR timerId = view.GetActiveRetryTimerIdForTest();
			TEST(timerId != 0);
			view.SendMessage(WM_TIMER, timerId, 0);
			drawView();
			TEST(creates == 1 + retry && destroys == 1 + retry && presents == 1 + retry);
		}
		TEST(view.GetVulkanRecoveryForTest().IsStopped());
		TEST(!view.IsRetryTimerActiveForTest());
		TEST(viewLogs.size() == 2);

		// Scrolling, resizing, and extra paints in Stopped state do not retry.
		view.SendMessage(WM_VSCROLL, SB_LINEDOWN, 0);
		view.SendMessage(WM_SIZE, SIZE_RESTORED, MAKELPARAM(300, 220));
		drawView();
		drawView();
		TEST(creates == 4 && destroys == 4 && presents == 4);

		// Retry button is laid out; clicking it after clearing fault recovers!
		const RECT retryRect = view.GetRetryButtonRectForTest();
		TEST(retryRect.right > retryRect.left && retryRect.bottom > retryRect.top);
		nextPresentResult = VulkanPresentResult::Succeeded;
		const int clickX = (retryRect.left + retryRect.right) / 2;
		const int clickY = (retryRect.top + retryRect.bottom) / 2;
		view.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(clickX, clickY));
		drawView();
		TEST(creates == 5 && destroys == 4 && presents == 5);
		TEST(view.GetVulkanRecoveryForTest().GetState() == TileSetVulkanRecoveryState::Healthy);

		// Consecutive repaints while transient swapchain timer is pending do NOT consume budget.
		nextPresentResult = VulkanPresentResult::SwapchainOutOfDate;
		drawView();
		TEST(presents == 6 && destroys == 4);
		TEST(view.IsTransientTimerActiveForTest());
		TEST(view.GetVulkanRecoveryForTest().GetConsecutiveSwapchainRetries() == 1);
		const UINT_PTR firstTransientId = view.GetActiveTransientTimerIdForTest();
		for (int i = 0; i < 5; ++i)
			drawView();
		TEST(presents == 6 && destroys == 4);
		TEST(view.GetVulkanRecoveryForTest().GetConsecutiveSwapchainRetries() == 1);
		TEST(view.GetVulkanRecoveryForTest().GetState() == TileSetVulkanRecoveryState::Healthy);

		// Cancel & reschedule transient timer via resize; old WM_TIMER ID must NOT wake new round.
		view.SendMessage(WM_SIZE, SIZE_RESTORED, MAKELPARAM(310, 225));
		TEST(!view.IsTransientTimerActiveForTest());
		drawView();
		TEST(presents == 7);
		const UINT_PTR secondTransientId = view.GetActiveTransientTimerIdForTest();
		TEST(secondTransientId != 0 && secondTransientId != firstTransientId);
		view.SendMessage(WM_TIMER, firstTransientId, 0);
		TEST(view.IsTransientTimerActiveForTest());
		TEST(view.GetActiveTransientTimerIdForTest() == secondTransientId);
		drawView();
		TEST(presents == 7);
		nextPresentResult = VulkanPresentResult::Succeeded;
		view.SendMessage(WM_TIMER, secondTransientId, 0);
		TEST(!view.IsTransientTimerActiveForTest());
		drawView();
		TEST(presents == 8);

		// Transient SetTimer failure escalates cleanly instead of hanging forever.
		allowTransientSetTimer = false;
		nextPresentResult = VulkanPresentResult::SwapchainOutOfDate;
		drawView();
		TEST(!view.IsTransientTimerActiveForTest());
		TEST(view.IsRetryTimerActiveForTest());
		TEST(destroys == 5);
		TEST(view.GetVulkanRecoveryForTest().GetState() == TileSetVulkanRecoveryState::RetryScheduled);
		allowTransientSetTimer = true;

		// Hiding and showing cancels & reschedules retry timer with a new generation;
		// a stale WM_TIMER carrying the old ID must NOT wake the new round.
		const UINT_PTR staleRetryId = view.GetActiveRetryTimerIdForTest();
		TEST(staleRetryId != 0);
		view.ShowWindow(SW_HIDE);
		TEST(!view.IsRetryTimerActiveForTest());
		view.ShowWindow(SW_SHOW);
		const UINT_PTR freshRetryId = view.GetActiveRetryTimerIdForTest();
		TEST(freshRetryId != 0 && freshRetryId != staleRetryId);
		view.SendMessage(WM_TIMER, staleRetryId, 0);
		TEST(view.IsRetryTimerActiveForTest());
		TEST(view.GetActiveRetryTimerIdForTest() == freshRetryId);
		TEST(view.GetVulkanRecoveryForTest().GetState() == TileSetVulkanRecoveryState::RetryScheduled);
		drawView();
		TEST(creates == 5); // still blocked while waiting for freshRetryId

		view.DestroyWindow();
		TEST(!view.IsRetryTimerActiveForTest());
		hostWindow.DestroyWindow();
	}

	REPORT_TEST(true);
}

void Tests::test_tileset_browser_long_list_coords()
{
	extern ACTIONDATA AD;
	extern PICDATA* ovrlpics[0xFF][max_ovrl_img];

	if (AfxGetInstanceHandle() == nullptr)
		AfxWinInit(::GetModuleHandle(nullptr), nullptr, ::GetCommandLine(), 0);

	constexpr int kViewWidth = 240;
	constexpr int kViewHeight = 240;
	constexpr int kTileWidth = 80;
	constexpr int kTileHeight = 80;
	constexpr int kCols = kViewWidth / kTileWidth; // 3
	constexpr int kRows = 900;                     // 72,000 px total (> 32,768 and > 65,536)
	constexpr int kItemCount = kRows * kCols;

	// Build a synthetic tile catalog with a shared 16x16 subtile image.
	std::vector<BYTE> subtilePixels(16 * 16, 1);
	std::vector<short> subtileBorders(16 * 2);
	for (int row = 0; row < 16; ++row)
	{
		subtileBorders[row * 2] = 0;
		subtileBorders[row * 2 + 1] = 15;
	}
	SUBTILE sharedSubtile{};
	sharedSubtile.pic = subtilePixels.data();
	sharedSubtile.vborder = reinterpret_cast<VBORDER*>(subtileBorders.data());
	sharedSubtile.wWidth = 16;
	sharedSubtile.wHeight = 16;
	sharedSubtile.sX = 0;
	sharedSubtile.sY = 0;
	sharedSubtile.bZHeight = 0;

	std::vector<TILEDATA> syntheticTiles(kItemCount);
	std::vector<DWORD> tileIds(kItemCount);
	for (int i = 0; i < kItemCount; ++i)
	{
		TILEDATA& td = syntheticTiles[i];
		td.rect = RECT{ 0, 0, 16, 16 };
		td.cx = 1;
		td.cy = 1;
		td.wTileCount = 1;
		td.wTileSet = 0;
		td.wMarbleGround = 0xFFFF;
		td.tiles = &sharedSubtile;
		tileIds[i] = static_cast<DWORD>(i);
	}

	TILEDATA* syntheticTileDataPtr = syntheticTiles.data();
	DWORD syntheticTileDataCount = static_cast<DWORD>(syntheticTiles.size());

	// Synthetic overlays: one RGBA bitmap overlay and one indexed overlay.
	auto syntheticBitmap = std::make_shared<RgbaBitmap>();
	syntheticBitmap->id = NextRgbaBitmapId();
	syntheticBitmap->width = 16;
	syntheticBitmap->height = 16;
	syntheticBitmap->pixels.assign(16 * 16, 0xFF00FF00u);

	PICDATA bmpOverlay{};
	bmpOverlay.pic = syntheticBitmap->pixels.data();
	bmpOverlay.bitmap = syntheticBitmap;
	bmpOverlay.bType = PICDATA_TYPE_BMP;
	bmpOverlay.wMaxWidth = 16;
	bmpOverlay.wMaxHeight = 16;

	PICDATA indexedOverlay{};
	indexedOverlay.pic = subtilePixels.data();
	indexedOverlay.vborder = reinterpret_cast<VBORDER*>(subtileBorders.data());
	indexedOverlay.bType = PICDATA_TYPE_SHP;
	indexedOverlay.wMaxWidth = 16;
	indexedOverlay.wMaxHeight = 16;

	struct GlobalStateRestorer
	{
		TILEDATA** prevTileData;
		DWORD* prevTileDataCount;
		DWORD prevMode;
		DWORD prevType;
		DWORD prevData;
		DWORD prevData2;
		DWORD prevData3;
		int prevZData;
		PICDATA* prevOvrl[5][2];
		~GlobalStateRestorer()
		{
			tiledata = prevTileData;
			tiledata_count = prevTileDataCount;
			AD.mode = prevMode;
			AD.type = prevType;
			AD.data = prevData;
			AD.data2 = prevData2;
			AD.data3 = prevData3;
			AD.z_data = prevZData;
			for (int t = 0; t < 5; ++t)
			{
				ovrlpics[1 + t][0] = prevOvrl[t][0];
				ovrlpics[1 + t][1] = prevOvrl[t][1];
			}
		}
	} restore{
		tiledata,
		tiledata_count,
		AD.mode,
		AD.type,
		AD.data,
		AD.data2,
		AD.data3,
		AD.z_data,
		{
			{ ovrlpics[1][0], ovrlpics[1][1] },
			{ ovrlpics[2][0], ovrlpics[2][1] },
			{ ovrlpics[3][0], ovrlpics[3][1] },
			{ ovrlpics[4][0], ovrlpics[4][1] },
			{ ovrlpics[5][0], ovrlpics[5][1] },
		}
	};

	tiledata = &syntheticTileDataPtr;
	tiledata_count = &syntheticTileDataCount;
	for (int t = 1; t <= 5; ++t)
	{
		ovrlpics[t][0] = &bmpOverlay;
		ovrlpics[t][1] = &indexedOverlay;
	}
	AD.mode = 0;
	AD.type = 0;
	AD.data = 0;
	AD.data2 = 0;
	AD.data3 = 0;
	AD.z_data = 0;

	const int windowWidth = kViewWidth + ::GetSystemMetrics(SM_CXVSCROLL) + 8;
	CWnd hostWindow;
	LPCTSTR hostClass = AfxRegisterWndClass(0);
	TEST(hostWindow.CreateEx(
		0, hostClass, "TileSetLongListTestHost",
		WS_POPUP | WS_VISIBLE,
		-32000, -32000, windowWidth + 32, kViewHeight + 32,
		nullptr, 0) != FALSE);

	CTileSetBrowserView view;
	TEST(view.Create(
		nullptr, nullptr,
		WS_CHILD | WS_VISIBLE | WS_VSCROLL,
		CRect(0, 0, windowWidth, kViewHeight), &hostWindow, 1002, nullptr) != FALSE);

	RECT presentedSource{};
	CTileSetBrowserView::VulkanTestHooks hooks{};
	hooks.recordSceneCommands = true;
	hooks.initialize = [](int, int, bool, std::string&) { return TileSetVulkanInitResult::Succeeded; };
	hooks.present = [&](const RECT& source, int, int, bool, std::string&)
	{
		presentedSource = source;
		return VulkanPresentResult::Succeeded;
	};
	view.SetVulkanTestHooks(&hooks);

	auto drawView = [&]()
	{
		CClientDC dc(&view);
		view.TriggerDrawForTest(&dc);
	};

	auto verifyHighlightRect = [](const std::vector<rs_scene_command>& cmds, int left, int top, int right, int bottom)
	{
		int lineCount = 0;
		bool hasTop = false, hasBottom = false, hasLeft = false, hasRight = false;
		for (const auto& cmd : cmds)
		{
			if (cmd.data[0] != 2)
				continue;
			++lineCount;
			TEST(cmd.tint[1] == 0x00ff0000u);
			const int x0 = cmd.line[0], y0 = cmd.line[1], x1 = cmd.line[2], y1 = cmd.line[3];
			if (y0 == top && y1 == top && min(x0, x1) == left && max(x0, x1) == right) hasTop = true;
			if (y0 == bottom && y1 == bottom && min(x0, x1) == left && max(x0, x1) == right) hasBottom = true;
			if (x0 == left && x1 == left && min(y0, y1) == top && max(y0, y1) == bottom) hasLeft = true;
			if (x0 == right && x1 == right && min(y0, y1) == top && max(y0, y1) == bottom) hasRight = true;
		}
		TEST(lineCount == 4);
		TEST(hasTop && hasBottom && hasLeft && hasRight);
	};

	auto hasVisibleCommandAt = [](const std::vector<rs_scene_command>& cmds, unsigned int kind, int expectedX, int expectedY)
	{
		for (const auto& cmd : cmds)
		{
			if (cmd.data[0] != kind)
				continue;
			const int left = max(cmd.rect[0], cmd.clip[0]);
			const int top = max(cmd.rect[1], cmd.clip[1]);
			const int right = min(cmd.rect[0] + cmd.rect[2], cmd.clip[2]);
			const int bottom = min(cmd.rect[1] + cmd.rect[3], cmd.clip[3]);
			if (cmd.rect[0] == expectedX && cmd.rect[1] == expectedY && left < right && top < bottom)
				return true;
		}
		return false;
	};

	// Checkpoints spanning below 32,768, above 32,768 (Line kSafeCoord), and above 65,536 (kMaxCoord).
	struct Checkpoint { int row; int col; };
	const Checkpoint checkpoints[] = {
		{ 1, 1 },     // list y = 80    (< 32,768)
		{ 420, 2 },   // list y = 33600 (> 32,768)
		{ 830, 0 },   // list y = 66400 (> 65,536)
	};

	// 1. Mode 1 (tile catalog): verify preview, click selection, and highlight at all 3 thresholds.
	view.ConfigureSyntheticTilesForTest(tileIds, kTileWidth, kTileHeight);
	CRect clientRect;
	view.GetClientRect(&clientRect);
	const int colsMode1 = max(1, static_cast<int>(clientRect.Width()) / kTileWidth);

	for (const auto& cp : checkpoints)
	{
		const int listY = cp.row * kTileHeight;
		const int targetScrollY = max(0, listY - 40);
		view.ScrollToPosition(CPoint(0, targetScrollY));
		const int actualScrollX = view.GetScrollPos(SB_HORZ);
		const int actualScrollY = view.GetScrollPos(SB_VERT);
		TEST(actualScrollY == targetScrollY);

		const int col = min(cp.col, colsMode1 - 1);
		const int expectedIdx = cp.row * colsMode1 + col;
		const int localX = col * kTileWidth - actualScrollX;
		const int localY = listY - actualScrollY;

		// Click center of the cell in client coordinates -> selects expected tile.
		view.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(localX + kTileWidth / 2, localY + kTileHeight / 2));
		TEST(AD.mode == ACTIONMODE_SETTILE);
		TEST(AD.type == static_cast<int>(tileIds[expectedIdx]));

		// Draw and inspect VulkanScene commands in viewport-local coordinates.
		drawView();
		TEST(presentedSource.left == 0 && presentedSource.top == 0);
		TEST(presentedSource.right == clientRect.Width() && presentedSource.bottom == clientRect.Height());
		const VulkanScene* scene = view.GetGpuSceneForTest();
		TEST(scene != nullptr);
		const auto& cmds = scene->GetCommandsForTest();

		const int expectedDrawX = localX + (kTileWidth - 16) / 2;
		const int expectedDrawY = localY + (kTileHeight - 16) / 2;
		TEST(hasVisibleCommandAt(cmds, 0u, expectedDrawX, expectedDrawY));
		verifyHighlightRect(cmds, localX + 2, localY + 2, localX + kTileWidth - 2, localY + kTileHeight - 2);
	}

	// 2. Mode 2 (overlay catalog with both BMP and Indexed entries): verify at all 3 thresholds.
	view.GetClientRect(&clientRect);
	const int colsMode2 = max(1, static_cast<int>(clientRect.Width()) / kTileWidth);
	TEST(colsMode2 >= 2); // ensures each row has both BMP and Indexed entries

	std::vector<std::pair<DWORD, DWORD>> overlayEntries(kRows * colsMode2, { 1u, 0u });
	for (int i = 0; i < static_cast<int>(overlayEntries.size()); ++i)
		overlayEntries[i] = { 1u, static_cast<DWORD>(i & 1) }; // background: even=BMP(0), odd=Indexed(1)
	// Give each checkpoint row unique overlay types so selection highlights only the clicked cell.
	for (int ci = 0; ci < 3; ++ci)
	{
		const DWORD uniqueType = static_cast<DWORD>(2 + ci);
		overlayEntries[checkpoints[ci].row * colsMode2 + 0] = { uniqueType, 0u }; // BMP
		overlayEntries[checkpoints[ci].row * colsMode2 + 1] = { uniqueType, 1u }; // Indexed
	}

	view.ConfigureSyntheticOverlaysForTest(overlayEntries, kTileWidth, kTileHeight);
	view.GetClientRect(&clientRect);

	for (int ci = 0; ci < 3; ++ci)
	{
		const auto& cp = checkpoints[ci];
		const DWORD expectedOverlayType = static_cast<DWORD>(2 + ci);
		const int listY = cp.row * kTileHeight;
		const int targetScrollY = max(0, listY - 40);
		view.ScrollToPosition(CPoint(0, targetScrollY));
		const int actualScrollX = view.GetScrollPos(SB_HORZ);
		const int actualScrollY = view.GetScrollPos(SB_VERT);
		TEST(actualScrollY == targetScrollY);

		// Verify both a BMP cell and an Indexed cell on this row.
		for (int subCol = 0; subCol < 2; ++subCol)
		{
			const DWORD expectedImageIndex = static_cast<DWORD>(subCol);
			const unsigned int expectedCmdKind = (expectedImageIndex == 0) ? 1u : 0u; // 1=Bitmap, 0=Indexed
			const int localX = subCol * kTileWidth - actualScrollX;
			const int localY = listY - actualScrollY;

			view.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(localX + kTileWidth / 2, localY + kTileHeight / 2));
			TEST(AD.mode == ACTIONMODE_PLACE);
			TEST(AD.type == 6 && AD.data == 33
				&& AD.data2 == static_cast<int>(expectedOverlayType)
				&& AD.data3 == static_cast<int>(expectedImageIndex));

			drawView();
			TEST(presentedSource.left == 0 && presentedSource.top == 0);
			TEST(presentedSource.right == clientRect.Width() && presentedSource.bottom == clientRect.Height());
			const VulkanScene* scene = view.GetGpuSceneForTest();
			TEST(scene != nullptr);
			const auto& cmds = scene->GetCommandsForTest();

			const int expectedDrawX = localX + (kTileWidth - 16) / 2;
			const int expectedDrawY = localY + (kTileHeight - 16) / 2;
			TEST(hasVisibleCommandAt(cmds, expectedCmdKind, expectedDrawX, expectedDrawY));
			verifyHighlightRect(cmds, localX + 2, localY + 2, localX + kTileWidth - 2, localY + kTileHeight - 2);
		}
	}

	view.DestroyWindow();
	hostWindow.DestroyWindow();
	REPORT_TEST(true);
}

namespace
{
	LONG CopySehAndWriteCrashArtifacts(
		EXCEPTION_POINTERS* ep,
		const std::wstring* crashDir,
		const CrashReportData* data,
		DWORD tid,
		CrashArtifacts* outArtifacts)
	{
		if (crashDir && data && outArtifacts)
		{
			*outArtifacts = CrashReporter::WriteCrashArtifacts(*crashDir, *data, ep, tid);
		}
		return EXCEPTION_EXECUTE_HANDLER;
	}

	bool TriggerSehAndWriteCrashArtifacts(
		const std::wstring& crashDir,
		const CrashReportData& data,
		CrashArtifacts& outArtifacts)
	{
		const DWORD tid = GetCurrentThreadId();
		__try
		{
			RaiseException(0xE0001234u, 0, 0, nullptr);
		}
		__except (CopySehAndWriteCrashArtifacts(GetExceptionInformation(), &crashDir, &data, tid, &outArtifacts))
		{
			return true;
		}
		return false;
	}

	struct DelayedDumpTestState
	{
		HANDLE startedEvent = nullptr;
		HANDLE releaseEvent = nullptr;
		HANDLE finishedEvent = nullptr;
		std::wstring expectedPath;
		DWORD expectedOuterCode = 0;
		DWORD expectedInnerCode = 0;
		DWORD64 expectedRip = 0;
		bool snapshotIntact = false;
	};

	bool DelayedDumpWriterForTest(
		const std::wstring& dumpPath,
		EXCEPTION_POINTERS* exceptionPointers,
		DWORD /*faultingThreadId*/,
		DWORD* outLastError,
		void* userContext)
	{
		auto* state = static_cast<DelayedDumpTestState*>(userContext);
		if (state && state->startedEvent)
			SetEvent(state->startedEvent);
		if (state && state->releaseEvent)
			WaitForSingleObject(state->releaseEvent, 5000);

		bool intact = false;
		if (state && exceptionPointers && exceptionPointers->ExceptionRecord && exceptionPointers->ContextRecord)
		{
			const EXCEPTION_RECORD* outer = exceptionPointers->ExceptionRecord;
			const EXCEPTION_RECORD* inner = outer->ExceptionRecord;
			const bool pathMatch = (dumpPath == state->expectedPath);
			const bool outerMatch = (outer->ExceptionCode == state->expectedOuterCode);
			const bool innerMatch = (inner != nullptr && inner->ExceptionCode == state->expectedInnerCode && inner->ExceptionRecord == nullptr);
#ifdef _M_X64
			const bool ripMatch = (exceptionPointers->ContextRecord->Rip == state->expectedRip);
#else
			const bool ripMatch = true;
#endif
			intact = pathMatch && outerMatch && innerMatch && ripMatch;
		}
		if (state)
		{
			state->snapshotIntact = intact;
			if (state->finishedEvent)
				SetEvent(state->finishedEvent);
		}
		if (outLastError)
			*outLastError = 0;
		return true;
	}

	DWORD FailingWaitForTest(HANDLE /*handle*/, DWORD /*timeoutMs*/, void* /*userContext*/)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return WAIT_FAILED;
	}
}

void Tests::test_crash_reporter()
{
	// 1. Verify exception descriptions (including access violation read/write/DEP details).
	{
		const ULONG_PTR readInfo[2] = { 0, 0x12345678ULL };
		const auto readSummary = CrashReporter::DescribeExceptionCode(EXCEPTION_ACCESS_VIOLATION, readInfo, 2);
		TEST(readSummary.code == EXCEPTION_ACCESS_VIOLATION);
		TEST(readSummary.name == "EXCEPTION_ACCESS_VIOLATION");
		TEST(readSummary.extraDetails.find("Read") != std::string::npos);
		TEST(readSummary.extraDetails.find("12345678") != std::string::npos);

		const ULONG_PTR writeInfo[2] = { 1, 0xDEADBEEFULL };
		const auto writeSummary = CrashReporter::DescribeExceptionCode(EXCEPTION_ACCESS_VIOLATION, writeInfo, 2);
		TEST(writeSummary.extraDetails.find("Write") != std::string::npos);
		TEST(writeSummary.extraDetails.find("DEADBEEF") != std::string::npos);

		const auto stackSummary = CrashReporter::DescribeExceptionCode(EXCEPTION_STACK_OVERFLOW);
		TEST(stackSummary.name == "EXCEPTION_STACK_OVERFLOW");

		const auto cppSummary = CrashReporter::DescribeExceptionCode(0xE06D7363u);
		TEST(cppSummary.name == "MSVC_CPP_EXCEPTION");

		const auto unknownSummary = CrashReporter::DescribeExceptionCode(0x12345678u);
		TEST(unknownSummary.name == "UNKNOWN_EXCEPTION");
	}

	// 2. Capture live stack frames and verify module/symbol info is populated.
	const auto frames = CrashReporter::CaptureStackFrames(nullptr, 0, 16);
	TEST(!frames.empty());
	bool foundModuleOrSymbol = false;
	for (const auto& frame : frames)
	{
		TEST(frame.address != 0);
		if (!frame.moduleName.empty() || !frame.symbolName.empty())
			foundModuleOrSymbol = true;
	}
	TEST(foundModuleOrSymbol);

	// 3. Format crash report + write both .txt report and .dmp minidump to a temp directory.
	CrashReportData data;
	data.timestamp = "2026-11-27 07:00:00.000";
	data.editorVariant = "FinalAlert 2: Yuri's Revenge";
	data.buildConfig = "Debug x64";
	data.osVersion = "Windows 10.0";
	data.processId = GetCurrentProcessId();
	data.threadId = GetCurrentThreadId();
	data.crashSource = "UnitTest Synthetic Crash";
	data.exception = CrashReporter::DescribeExceptionCode(EXCEPTION_INT_DIVIDE_BY_ZERO);
	data.faultAddress = frames[0].address;
	data.faultLocation = "FinalAlert2YRTestsd.exe+0x1234";
	data.cppExceptionType = "std::runtime_error";
	data.cppExceptionMessage = "synthetic test failure";
	data.lastSucceededOperation = 42;
	data.lastLibraryOperation = 7;
	data.currentMapFile = "C:\\maps\\test.yrm";
	data.vulkanLastError = "vkQueuePresentKHR: VK_ERROR_DEVICE_LOST";
	data.stackFrames = frames;

	namespace fs = std::filesystem;
	const fs::path tempDir = fs::temp_directory_path() / ("ra2map_crash_test_" + std::to_string(GetCurrentProcessId()));
	std::error_code ec;
	fs::remove_all(tempDir, ec);

	const auto verifyValidMinidump = [](const std::wstring& path)
	{
		std::ifstream dumpIn(path, std::ios::binary);
		TEST(dumpIn.is_open());
		char magic[4] = { 0 };
		dumpIn.read(magic, 4);
		TEST(dumpIn.gcount() == 4);
		TEST(std::memcmp(magic, "MDMP", 4) == 0);
	};

	const CrashArtifacts artifacts = CrashReporter::WriteCrashArtifacts(
		(tempDir / "basic").wstring(),
		data,
		nullptr,
		GetCurrentThreadId());

	TEST(artifacts.reportWritten);
	TEST(fs::exists(artifacts.reportFilePath));
	TEST(artifacts.dumpWritten);
	TEST(artifacts.dumpStatus == CrashDumpStatus::Succeeded);
	TEST(fs::exists(artifacts.dumpFilePath));

	// Verify UTF-8 BOM + expected report content.
	{
		std::ifstream in(artifacts.reportFilePath, std::ios::binary);
		TEST(in.is_open());
		const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		TEST(content.size() >= 3);
		TEST(static_cast<unsigned char>(content[0]) == 0xEF &&
			static_cast<unsigned char>(content[1]) == 0xBB &&
			static_cast<unsigned char>(content[2]) == 0xBF);
		TEST(content.find("EXCEPTION_INT_DIVIDE_BY_ZERO") != std::string::npos);
		TEST(content.find("synthetic test failure") != std::string::npos);
		TEST(content.find("vkQueuePresentKHR: VK_ERROR_DEVICE_LOST") != std::string::npos);
		TEST(content.find("Last Operation   : 42") != std::string::npos);
		TEST(content.find("Call Stack") != std::string::npos);
	}

	verifyValidMinidump(artifacts.dumpFilePath);

	// 4. Verify C++ synthetic EXCEPTION_POINTERS path produces a valid dump.
	{
		CONTEXT ctx = {};
		RtlCaptureContext(&ctx);
		EXCEPTION_RECORD rec = {};
		rec.ExceptionCode = 0xE06D7363u;
#ifdef _M_X64
		rec.ExceptionAddress = reinterpret_cast<PVOID>(ctx.Rip);
#endif
		EXCEPTION_POINTERS ep = {};
		ep.ExceptionRecord = &rec;
		ep.ContextRecord = &ctx;

		const CrashArtifacts cppArtifacts = CrashReporter::WriteCrashArtifacts(
			(tempDir / "cpp_ctx").wstring(),
			data,
			&ep,
			GetCurrentThreadId());
		TEST(cppArtifacts.reportWritten);
		TEST(cppArtifacts.dumpWritten);
		TEST(cppArtifacts.dumpStatus == CrashDumpStatus::Succeeded);
		verifyValidMinidump(cppArtifacts.dumpFilePath);
	}

	// 5. Verify real SEH EXCEPTION_POINTERS path produces a valid dump.
	{
		CrashArtifacts sehArtifacts;
		TEST(TriggerSehAndWriteCrashArtifacts((tempDir / "seh_ctx").wstring(), data, sehArtifacts));
		TEST(sehArtifacts.reportWritten);
		TEST(sehArtifacts.dumpWritten);
		TEST(sehArtifacts.dumpStatus == CrashDumpStatus::Succeeded);
		verifyValidMinidump(sehArtifacts.dumpFilePath);
	}

	// 6. Verify worker timeout: caller must NOT read unpublished results, must mark
	// dump incomplete, and worker must still see intact deep-copied exception context
	// after caller's stack inputs are overwritten/destroyed.
	{
		DelayedDumpTestState state;
		state.startedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		state.releaseEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		state.finishedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		state.expectedOuterCode = 0xE000CAFEu;
		state.expectedInnerCode = 0xE000BEEFu;
		state.expectedRip = 0x00007FF612345678ULL;
		TEST(state.startedEvent && state.releaseEvent && state.finishedEvent);

		CrashArtifacts timeoutArtifacts;
		{
			CONTEXT stackCtx = {};
#ifdef _M_X64
			stackCtx.Rip = state.expectedRip;
#endif
			EXCEPTION_RECORD stackInner = {};
			stackInner.ExceptionCode = state.expectedInnerCode;
			EXCEPTION_RECORD stackOuter = {};
			stackOuter.ExceptionCode = state.expectedOuterCode;
			stackOuter.ExceptionRecord = &stackInner;
			EXCEPTION_POINTERS stackEp = {};
			stackEp.ExceptionRecord = &stackOuter;
			stackEp.ContextRecord = &stackCtx;

			CrashReporter::MiniDumpExecutionOptions opts;
			opts.timeoutMs = 50;
			opts.customWriter = DelayedDumpWriterForTest;
			opts.customWriterContext = &state;

			timeoutArtifacts = CrashReporter::WriteCrashArtifactsEx(
				(tempDir / "timeout").wstring(),
				data,
				&stackEp,
				GetCurrentThreadId(),
				opts);
			state.expectedPath = timeoutArtifacts.dumpFilePath;

			// Poison caller stack structs before releasing worker.
			std::memset(&stackCtx, 0xDD, sizeof(stackCtx));
			std::memset(&stackInner, 0xDD, sizeof(stackInner));
			std::memset(&stackOuter, 0xDD, sizeof(stackOuter));
			std::memset(&stackEp, 0xDD, sizeof(stackEp));
		}

		TEST(timeoutArtifacts.reportWritten);
		TEST(!timeoutArtifacts.dumpWritten);
		TEST(timeoutArtifacts.dumpStatus == CrashDumpStatus::TimedOut);
		TEST(timeoutArtifacts.dumpError.find("timed out") != std::string::npos);

		{
			std::ifstream in(timeoutArtifacts.reportFilePath, std::ios::binary);
			TEST(in.is_open());
			const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			TEST(content.find("TIMED OUT / INCOMPLETE") != std::string::npos);
			TEST(content.find(".dmp (OK)") == std::string::npos);
		}

		TEST(WaitForSingleObject(state.startedEvent, 5000) == WAIT_OBJECT_0);
		SetEvent(state.releaseEvent);
		TEST(WaitForSingleObject(state.finishedEvent, 5000) == WAIT_OBJECT_0);
		TEST(state.snapshotIntact);

		CloseHandle(state.startedEvent);
		CloseHandle(state.releaseEvent);
		CloseHandle(state.finishedEvent);
	}

	// 7. Verify wait failure: caller marks WaitFailed without reading unpublished results,
	// and worker still owns valid deep-copied inputs until it exits.
	{
		DelayedDumpTestState state;
		state.startedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		state.releaseEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		state.finishedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		state.expectedOuterCode = 0xE0001111u;
		state.expectedInnerCode = 0xE0002222u;
		state.expectedRip = 0x00007FF687654321ULL;
		TEST(state.startedEvent && state.releaseEvent && state.finishedEvent);

		CrashArtifacts waitFailArtifacts;
		{
			CONTEXT stackCtx = {};
#ifdef _M_X64
			stackCtx.Rip = state.expectedRip;
#endif
			EXCEPTION_RECORD stackInner = {};
			stackInner.ExceptionCode = state.expectedInnerCode;
			EXCEPTION_RECORD stackOuter = {};
			stackOuter.ExceptionCode = state.expectedOuterCode;
			stackOuter.ExceptionRecord = &stackInner;
			EXCEPTION_POINTERS stackEp = {};
			stackEp.ExceptionRecord = &stackOuter;
			stackEp.ContextRecord = &stackCtx;

			CrashReporter::MiniDumpExecutionOptions opts;
			opts.timeoutMs = 5000;
			opts.customWriter = DelayedDumpWriterForTest;
			opts.customWriterContext = &state;
			opts.customWait = FailingWaitForTest;

			waitFailArtifacts = CrashReporter::WriteCrashArtifactsEx(
				(tempDir / "wait_failed").wstring(),
				data,
				&stackEp,
				GetCurrentThreadId(),
				opts);
			state.expectedPath = waitFailArtifacts.dumpFilePath;

			std::memset(&stackCtx, 0xCC, sizeof(stackCtx));
			std::memset(&stackInner, 0xCC, sizeof(stackInner));
			std::memset(&stackOuter, 0xCC, sizeof(stackOuter));
			std::memset(&stackEp, 0xCC, sizeof(stackEp));
		}

		TEST(waitFailArtifacts.reportWritten);
		TEST(!waitFailArtifacts.dumpWritten);
		TEST(waitFailArtifacts.dumpStatus == CrashDumpStatus::WaitFailed);
		TEST(waitFailArtifacts.dumpError.find("WaitForSingleObject failed") != std::string::npos);

		{
			std::ifstream in(waitFailArtifacts.reportFilePath, std::ios::binary);
			TEST(in.is_open());
			const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			TEST(content.find("WAIT FAILED / INCOMPLETE") != std::string::npos);
			TEST(content.find(".dmp (OK)") == std::string::npos);
		}

		TEST(WaitForSingleObject(state.startedEvent, 5000) == WAIT_OBJECT_0);
		SetEvent(state.releaseEvent);
		TEST(WaitForSingleObject(state.finishedEvent, 5000) == WAIT_OBJECT_0);
		TEST(state.snapshotIntact);

		CloseHandle(state.startedEvent);
		CloseHandle(state.releaseEvent);
		CloseHandle(state.finishedEvent);
	}

	// Verify both Chinese and English UI summary texts include key fields.
	const std::wstring zhSummary = CrashReporter::BuildCrashSummaryText(data, artifacts, true);
	const std::wstring enSummary = CrashReporter::BuildCrashSummaryText(data, artifacts, false);
	TEST(zhSummary.find(L"EXCEPTION_INT_DIVIDE_BY_ZERO") != std::wstring::npos);
	TEST(zhSummary.find(L"synthetic test failure") != std::wstring::npos);
	TEST(enSummary.find(L"EXCEPTION_INT_DIVIDE_BY_ZERO") != std::wstring::npos);
	TEST(enSummary.find(L"synthetic test failure") != std::wstring::npos);

	fs::remove_all(tempDir, ec);
	REPORT_TEST(true);
}

void Tests::test_map_save_and_emergency_backup()
{
	namespace fs = std::filesystem;
	const fs::path tempDir = fs::temp_directory_path() / ("ra2map_save_test_" + std::to_string(GetCurrentProcessId()));
	std::error_code ec;
	fs::remove_all(tempDir, ec);
	TEST(fs::create_directories(tempDir, ec));

	struct Cleanup
	{
		fs::path dir;
		~Cleanup()
		{
			MapSaveUtil::ClearCommittedMapSnapshot();
			std::error_code err;
			fs::remove_all(dir, err);
		}
	} cleanup{ tempDir };

	// 1. Extension resolution: only replace final extension, never directory/stem ".map" substrings.
	{
		const fs::path dirWithMap(L"C:\\my.map.folder\\arena.map");
		TEST(MapSaveUtil::ResolveMapOutputPath(dirWithMap, false, false, true) == dirWithMap);
		TEST(MapSaveUtil::ResolveMapOutputPath(dirWithMap, true, false, true) == fs::path(L"C:\\my.map.folder\\arena.mpr"));
		TEST(MapSaveUtil::ResolveMapOutputPath(dirWithMap, true, true, true) == fs::path(L"C:\\my.map.folder\\arena.yrm"));
		TEST(MapSaveUtil::ResolveMapOutputPath(fs::path(L"C:\\maps\\arena.mpr"), true, true, true) == fs::path(L"C:\\maps\\arena.yrm"));
		TEST(MapSaveUtil::ResolveMapOutputPath(fs::path(L"C:\\maps\\arena.mmx"), true, true, true) == fs::path(L"C:\\maps\\arena.mmx"));
		TEST(MapSaveUtil::ResolveMapOutputPath(dirWithMap, true, true, false) == dirWithMap);
	}

	// 2. Atomic write integrity: success, unwritable/nonexistent dir, incomplete write, commit failure.
	{
		const fs::path target = tempDir / L"existing.yrm";
		DWORD err = 0;
		TEST(MapSaveUtil::WriteBytesAtomically(target, "GOOD_V1", true, &err));

		// allowOverwrite=false must NOT overwrite an existing good file.
		TEST(!MapSaveUtil::WriteBytesAtomically(target, "BAD_OVERWRITE", false, &err));
		{
			std::ifstream in(target, std::ios::binary);
			const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			TEST(content == "GOOD_V1");
		}

		// Simulated incomplete write must fail, clean up temp file, and leave existing file intact.
		MapSaveUtil::AtomicWriteHooks partialWriteHooks;
		partialWriteHooks.writeFn = [](HANDLE file, std::string_view bytes, DWORD* outErr, void*) -> bool
		{
			DWORD written = 0;
			if (!bytes.empty())
				WriteFile(file, bytes.data(), 2, &written, nullptr);
			if (outErr)
				*outErr = ERROR_DISK_FULL;
			return false;
		};
		TEST(!MapSaveUtil::WriteBytesAtomically(target, "PARTIAL_NEW_CONTENT", true, &err, &partialWriteHooks));
		TEST(err == ERROR_DISK_FULL);
		{
			std::ifstream in(target, std::ios::binary);
			const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			TEST(content == "GOOD_V1");
		}

		// Simulated commit failure must fail, clean up temp file, and leave existing file intact.
		MapSaveUtil::AtomicWriteHooks commitFailHooks;
		commitFailHooks.commitFn = [](const fs::path&, const fs::path&, bool, DWORD* outErr, void*) -> bool
		{
			if (outErr)
				*outErr = ERROR_ACCESS_DENIED;
			return false;
		};
		TEST(!MapSaveUtil::WriteBytesAtomically(target, "COMMIT_FAIL_CONTENT", true, &err, &commitFailHooks));
		TEST(err == ERROR_ACCESS_DENIED);
		{
			std::ifstream in(target, std::ios::binary);
			const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			TEST(content == "GOOD_V1");
		}

		// Nonexistent parent directory must fail cleanly.
		TEST(!MapSaveUtil::WriteBytesAtomically(tempDir / L"no_such_dir" / L"out.map", "DATA", true, &err));

		// Verify no leftover .tmp files remain in tempDir.
		for (const auto& entry : fs::directory_iterator(tempDir))
		{
			TEST(entry.path().extension() != L".tmp");
		}
	}

	// 3. Reentrancy guard prevents nested live saves.
	{
		TEST(!MapSaveUtil::IsMapSaveInProgress());
		MapSaveUtil::ScopedMapSaveGuard outer;
		TEST(outer.IsAcquired());
		TEST(MapSaveUtil::IsMapSaveInProgress());
		MapSaveUtil::ScopedMapSaveGuard inner;
		TEST(!inner.IsAcquired());
	}
	TEST(!MapSaveUtil::IsMapSaveInProgress());

	// 4. Serialized map output (SP .map, MP .mpr, YR .yrm) reloads cleanly via CIniFile.
	CIniFile mapIni;
	mapIni.sections["Basic"].values["Name"] = "Backup Integrity Map";
	mapIni.sections["Basic"].values["GameMode"] = "standard";
	mapIni.sections["Map"].values["Theater"] = "TEMPERATE";
	mapIni.sections["Map"].values["Size"] = "0,0,16,16";
	mapIni.sections["Header"].values["NumberStartingPoints"] = "2";
	const std::string serialized = MapSaveUtil::SerializeMapIniToUtf8(
		mapIni,
		[](const CString&) { return false; },
		false,
		false);

	// 5. Emergency backup with no committed snapshot reports NoSnapshotAvailable honestly.
	MapSaveUtil::ClearCommittedMapSnapshot();
	const auto noSnapResult = MapSaveUtil::WriteEmergencyMapBackup(
		(tempDir / L"backups").wstring(),
		L"20261127_080000",
		GetCurrentProcessId());
	TEST(noSnapResult.status == MapSaveUtil::EmergencyBackupStatus::NoSnapshotAvailable);
	TEST(noSnapResult.actualPath.empty());

	// 6. Emergency backup with committed snapshot writes unique files for SP/MP/YR,
	//    never overwrites previous good backups, reports exact existing path, and reloads cleanly.
	const std::wstring exts[] = { L".map", L".mpr", L".yrm" };
	const fs::path backupDir = tempDir / L"backups";
	std::vector<fs::path> createdBackups;
	for (const auto& ext : exts)
	{
		MapSaveUtil::PublishCommittedMapSnapshot(serialized, ext, (tempDir / (L"src" + ext)).wstring());
		const auto res1 = MapSaveUtil::WriteEmergencyMapBackup(backupDir.wstring(), L"20261127_080001", 1234);
		TEST(res1.status == MapSaveUtil::EmergencyBackupStatus::Saved);
		TEST(!res1.actualPath.empty());
		const fs::path p1(res1.actualPath);
		TEST(fs::exists(p1));
		TEST(p1.extension() == ext);

		// Reload the written backup to confirm valid INI round-trip.
		CIniFile reloaded;
		TEST(reloaded.LoadFile(p1.string()) == 0);
		TEST(reloaded.GetValueByName("Basic", "Name", CString()) == CString("Backup Integrity Map"));
		TEST(reloaded.GetValueByName("Map", "Theater", CString()) == CString("TEMPERATE"));

		// Second emergency backup with identical timestamp+pid must create a distinct file,
		// preserving the first good backup intact.
		MapSaveUtil::PublishCommittedMapSnapshot(serialized + "; second\n", ext, (tempDir / (L"src" + ext)).wstring());
		const auto res2 = MapSaveUtil::WriteEmergencyMapBackup(backupDir.wstring(), L"20261127_080001", 1234);
		TEST(res2.status == MapSaveUtil::EmergencyBackupStatus::Saved);
		const fs::path p2(res2.actualPath);
		TEST(p2 != p1);
		TEST(fs::exists(p1) && fs::exists(p2));
		createdBackups.push_back(p1);
		createdBackups.push_back(p2);
	}

	// Failed emergency backup (e.g. path blocked by an existing regular file or failing hook)
	// must report WriteFailed and preserve all previous backups.
	{
		const fs::path fileAsDir = tempDir / L"not_a_directory";
		TEST(MapSaveUtil::WriteBytesAtomically(fileAsDir, "BLOCKER", true));
		const auto failDirRes = MapSaveUtil::WriteEmergencyMapBackup(fileAsDir.wstring(), L"20261127_080002", 1234);
		TEST(failDirRes.status == MapSaveUtil::EmergencyBackupStatus::WriteFailed);
		TEST(failDirRes.actualPath.empty());

		MapSaveUtil::AtomicWriteHooks failHooks;
		failHooks.writeFn = [](HANDLE, std::string_view, DWORD* outErr, void*) -> bool
		{
			if (outErr)
				*outErr = ERROR_WRITE_FAULT;
			return false;
		};
		const auto failWriteRes = MapSaveUtil::WriteEmergencyMapBackup(backupDir.wstring(), L"20261127_080003", 1234, &failHooks);
		TEST(failWriteRes.status == MapSaveUtil::EmergencyBackupStatus::WriteFailed);
		TEST(failWriteRes.actualPath.empty());

		for (const auto& existing : createdBackups)
			TEST(fs::exists(existing));
	}

	REPORT_TEST(true);
}
