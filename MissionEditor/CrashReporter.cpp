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

#include "stdafx.h"
#include "CrashReporter.h"
#include "FinalSun.h"
#include "FinalSunDlg.h"
#include "variables.h"
#include "functions.h"
#include "MapSaveUtil.h"
#include "MissionEditorPackLib.h"
#include "RustCore.h"

#include <DbgHelp.h>
#include <CommCtrl.h>
#include <shellapi.h>
#include <crtdbg.h>
#include <signal.h>
#include <stdlib.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <typeinfo>

namespace
{
#ifndef STATUS_HEAP_CORRUPTION
	constexpr DWORD kStatusHeapCorruption = 0xC0000374u;
#else
	constexpr DWORD kStatusHeapCorruption = STATUS_HEAP_CORRUPTION;
#endif
#ifndef STATUS_STACK_BUFFER_OVERRUN
	constexpr DWORD kStatusStackBufferOverrun = 0xC0000409u;
#else
	constexpr DWORD kStatusStackBufferOverrun = STATUS_STACK_BUFFER_OVERRUN;
#endif
	constexpr DWORD kMsvcCppExceptionCode = 0xE06D7363u;
	constexpr UINT kCrashExitCode = 0xC0000001u;

	constexpr int kBtnCopyDetails = 1001;
	constexpr int kBtnOpenCrashDir = 1002;
	constexpr int kBtnCloseApp = IDCLOSE;

	enum CrashPhase : int
	{
		kCrashPhaseIdle = 0,
		kCrashPhaseDiagnostics = 1,
		kCrashPhaseDialog = 2,
		kCrashPhaseFinished = 3,
	};

	constexpr DWORD kDbgHelpStackLockTimeoutMs = 1500;
	constexpr DWORD kDbgHelpSymbolLockTimeoutMs = 500;
	constexpr DWORD kSecondaryCrashMaxWaitMs = 20000;
	constexpr DWORD kStackOverflowDiagnosticsTimeoutMs = 20000;
	constexpr DWORD kStackOverflowDialogMaxWaitMs = 60000;

	std::atomic<DWORD> g_activeCrashThreadId{ 0 };
	std::atomic<int> g_crashPhase{ kCrashPhaseIdle };
	std::timed_mutex g_dbgHelpMutex;

	using SymInitializeWFn = BOOL(WINAPI*)(HANDLE, PCWSTR, BOOL);
	using SymCleanupFn = BOOL(WINAPI*)(HANDLE);
	using SymSetOptionsFn = DWORD(WINAPI*)(DWORD);
	using SymGetOptionsFn = DWORD(WINAPI*)();
	using StackWalk64Fn = BOOL(WINAPI*)(
		DWORD, HANDLE, HANDLE, LPSTACKFRAME64, PVOID,
		PREAD_PROCESS_MEMORY_ROUTINE64, PFUNCTION_TABLE_ACCESS_ROUTINE64,
		PGET_MODULE_BASE_ROUTINE64, PTRANSLATE_ADDRESS_ROUTINE64);
	using SymFunctionTableAccess64Fn = PVOID(WINAPI*)(HANDLE, DWORD64);
	using SymGetModuleBase64Fn = DWORD64(WINAPI*)(HANDLE, DWORD64);
	using SymFromAddrWFn = BOOL(WINAPI*)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFOW);
	using SymGetLineFromAddrW64Fn = BOOL(WINAPI*)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINEW64);
	using MiniDumpWriteDumpFn = BOOL(WINAPI*)(
		HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
		PMINIDUMP_EXCEPTION_INFORMATION,
		PMINIDUMP_USER_STREAM_INFORMATION,
		PMINIDUMP_CALLBACK_INFORMATION);
	using TaskDialogIndirectFn = HRESULT(WINAPI*)(
		const TASKDIALOGCONFIG*, int*, int*, BOOL*);

	struct DbgHelpApi
	{
		HMODULE module = nullptr;
		SymInitializeWFn symInitializeW = nullptr;
		SymCleanupFn symCleanup = nullptr;
		SymSetOptionsFn symSetOptions = nullptr;
		SymGetOptionsFn symGetOptions = nullptr;
		StackWalk64Fn stackWalk64 = nullptr;
		SymFunctionTableAccess64Fn symFunctionTableAccess64 = nullptr;
		SymGetModuleBase64Fn symGetModuleBase64 = nullptr;
		SymFromAddrWFn symFromAddrW = nullptr;
		SymGetLineFromAddrW64Fn symGetLineFromAddrW64 = nullptr;
		MiniDumpWriteDumpFn miniDumpWriteDump = nullptr;
		bool initialized = false;
	};

	std::string WideToUtf8Safe(const std::wstring& wide)
	{
		if (wide.empty())
			return {};
		const int needed = WideCharToMultiByte(
			CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
			nullptr, 0, nullptr, nullptr);
		if (needed <= 0)
			return {};
		std::string result(static_cast<size_t>(needed), '\0');
		WideCharToMultiByte(
			CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
			result.data(), needed, nullptr, nullptr);
		return result;
	}

	std::wstring Utf8ToWideSafe(const std::string& utf8)
	{
		if (utf8.empty())
			return {};
		int needed = MultiByteToWideChar(
			CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
			nullptr, 0);
		UINT cp = CP_UTF8;
		if (needed <= 0)
		{
			cp = CP_ACP;
			needed = MultiByteToWideChar(
				cp, 0, utf8.data(), static_cast<int>(utf8.size()),
				nullptr, 0);
		}
		if (needed <= 0)
			return {};
		std::wstring result(static_cast<size_t>(needed), L'\0');
		MultiByteToWideChar(
			cp, 0, utf8.data(), static_cast<int>(utf8.size()),
			result.data(), needed);
		return result;
	}

	std::wstring GetExecutableDirectoryW()
	{
		wchar_t path[MAX_PATH * 2] = { 0 };
		const DWORD len = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(_countof(path)));
		if (len == 0 || len >= _countof(path))
			return {};
		std::wstring result(path, len);
		const size_t slash = result.find_last_of(L"\\/");
		if (slash != std::wstring::npos)
			result.resize(slash);
		return result;
	}

	std::wstring GetBaseNameW(const std::wstring& fullPath)
	{
		const size_t slash = fullPath.find_last_of(L"\\/");
		if (slash == std::wstring::npos)
			return fullPath;
		return fullPath.substr(slash + 1);
	}

	DbgHelpApi& LoadDbgHelp()
	{
		static DbgHelpApi api;
		if (api.module != nullptr)
			return api;

		api.module = LoadLibraryW(L"dbghelp.dll");
		if (!api.module)
			return api;

		api.symInitializeW = reinterpret_cast<SymInitializeWFn>(GetProcAddress(api.module, "SymInitializeW"));
		api.symCleanup = reinterpret_cast<SymCleanupFn>(GetProcAddress(api.module, "SymCleanup"));
		api.symSetOptions = reinterpret_cast<SymSetOptionsFn>(GetProcAddress(api.module, "SymSetOptions"));
		api.symGetOptions = reinterpret_cast<SymGetOptionsFn>(GetProcAddress(api.module, "SymGetOptions"));
		api.stackWalk64 = reinterpret_cast<StackWalk64Fn>(GetProcAddress(api.module, "StackWalk64"));
		api.symFunctionTableAccess64 = reinterpret_cast<SymFunctionTableAccess64Fn>(GetProcAddress(api.module, "SymFunctionTableAccess64"));
		api.symGetModuleBase64 = reinterpret_cast<SymGetModuleBase64Fn>(GetProcAddress(api.module, "SymGetModuleBase64"));
		api.symFromAddrW = reinterpret_cast<SymFromAddrWFn>(GetProcAddress(api.module, "SymFromAddrW"));
		api.symGetLineFromAddrW64 = reinterpret_cast<SymGetLineFromAddrW64Fn>(GetProcAddress(api.module, "SymGetLineFromAddrW64"));
		api.miniDumpWriteDump = reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(api.module, "MiniDumpWriteDump"));
		return api;
	}

	bool EnsureSymbolsInitialized(DbgHelpApi& api)
	{
		if (api.initialized)
			return true;
		if (!api.symInitializeW)
			return false;

		if (api.symSetOptions && api.symGetOptions)
		{
			const DWORD opts = api.symGetOptions();
			api.symSetOptions(opts | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
		}

		const std::wstring exeDir = GetExecutableDirectoryW();
		const PCWSTR searchPath = exeDir.empty() ? nullptr : exeDir.c_str();
		if (api.symInitializeW(GetCurrentProcess(), searchPath, TRUE))
		{
			api.initialized = true;
			return true;
		}
		return false;
	}

	std::string FormatHex64(DWORD64 value)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "0x%016llX", static_cast<unsigned long long>(value));
		return buf;
	}

	std::string FormatHex32(DWORD value)
	{
		char buf[16];
		std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned int>(value));
		return buf;
	}

	std::string FormatTimestampNow(std::wstring* outFileStamp = nullptr)
	{
		SYSTEMTIME st = {};
		GetLocalTime(&st);

		char displayBuf[64];
		std::snprintf(
			displayBuf,
			sizeof(displayBuf),
			"%04u-%02u-%02u %02u:%02u:%02u.%03u",
			st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

		if (outFileStamp)
		{
			wchar_t fileBuf[64];
			std::swprintf(
				fileBuf,
				_countof(fileBuf),
				L"%04u%02u%02u_%02u%02u%02u",
				st.wYear, st.wMonth, st.wDay,
				st.wHour, st.wMinute, st.wSecond);
			*outFileStamp = fileBuf;
		}

		return displayBuf;
	}

	std::string QueryWindowsVersion()
	{
		using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
		HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
		if (ntdll)
		{
			auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
			if (rtlGetVersion)
			{
				RTL_OSVERSIONINFOW osvi = {};
				osvi.dwOSVersionInfoSize = sizeof(osvi);
				if (rtlGetVersion(&osvi) == 0)
				{
					char buf[96];
					std::snprintf(
						buf,
						sizeof(buf),
						"Windows %lu.%lu (Build %lu)",
						osvi.dwMajorVersion,
						osvi.dwMinorVersion,
						osvi.dwBuildNumber);
					return buf;
				}
			}
		}
		return "Windows (unknown version)";
	}

	std::string GetEditorVariantName()
	{
#ifdef TS_MODE
		return "FinalSun (Tiberian Sun)";
#elif defined(YR_MODE)
		return "FinalAlert 2: Yuri's Revenge";
#else
		return "FinalAlert 2";
#endif
	}

	std::string GetBuildConfigName()
	{
#ifdef _DEBUG
		return "Debug x64";
#else
		return "Release x64";
#endif
	}

	bool CreateDirectoryRecursiveW(const std::wstring& dirPath)
	{
		if (dirPath.empty())
			return false;

		DWORD attrs = GetFileAttributesW(dirPath.c_str());
		if (attrs != INVALID_FILE_ATTRIBUTES)
			return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;

		std::wstring current;
		current.reserve(dirPath.size());
		for (size_t i = 0; i < dirPath.size(); ++i)
		{
			const wchar_t ch = dirPath[i];
			current.push_back(ch);
			if ((ch == L'\\' || ch == L'/') && current.size() > 3)
			{
				CreateDirectoryW(current.c_str(), nullptr);
			}
		}
		if (CreateDirectoryW(dirPath.c_str(), nullptr))
			return true;
		const DWORD err = GetLastError();
		return err == ERROR_ALREADY_EXISTS;
	}

	std::wstring ResolveCrashDirectoryW()
	{
		if (!u8AppDataPath.empty())
		{
			std::wstring dir = Utf8ToWideSafe(u8AppDataPath);
			if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/')
				dir.push_back(L'\\');
			dir += L"crashes";
			if (CreateDirectoryRecursiveW(dir))
				return dir;
		}

		std::wstring exeDir = GetExecutableDirectoryW();
		if (!exeDir.empty())
		{
			std::wstring dir = exeDir + L"\\crashes";
			if (CreateDirectoryRecursiveW(dir))
				return dir;
		}

		wchar_t tempPath[MAX_PATH * 2] = { 0 };
		const DWORD len = GetTempPathW(static_cast<DWORD>(_countof(tempPath)), tempPath);
		if (len > 0 && len < _countof(tempPath))
		{
			std::wstring dir(tempPath, len);
			if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/')
				dir.push_back(L'\\');
			dir += L"MissionEditorCrashes";
			CreateDirectoryRecursiveW(dir);
			return dir;
		}
		return L".";
	}

	bool QueryModuleForAddressSafe(
		DWORD64 address,
		wchar_t* outPath,
		size_t outPathCount,
		DWORD* outLen,
		DWORD64* outBase)
	{
		if (outLen)
			*outLen = 0;
		if (outBase)
			*outBase = 0;
		if (!address || !outPath || outPathCount == 0)
			return false;

		__try
		{
			HMODULE mod = nullptr;
			if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(address)),
				&mod) || mod == nullptr)
			{
				return false;
			}
			const DWORD len = GetModuleFileNameW(mod, outPath, static_cast<DWORD>(outPathCount));
			if (outLen)
				*outLen = len;
			if (outBase)
				*outBase = reinterpret_cast<DWORD64>(mod);
			return len > 0 && len < outPathCount;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	void ResolveFrameModuleOnlySafe(DWORD64 address, CrashStackFrame& frame)
	{
		frame.address = address;
		wchar_t modPath[MAX_PATH * 2] = { 0 };
		DWORD len = 0;
		DWORD64 base = 0;
		if (QueryModuleForAddressSafe(address, modPath, _countof(modPath), &len, &base))
		{
			frame.moduleName = WideToUtf8Safe(GetBaseNameW(std::wstring(modPath, len)));
			frame.moduleOffset = address - base;
		}
	}

	void ResolveFrameInfo(HANDLE process, DbgHelpApi& api, bool hasSymbols, DWORD64 address, CrashStackFrame& frame)
	{
		ResolveFrameModuleOnlySafe(address, frame);

		if (hasSymbols && api.symFromAddrW)
		{
			alignas(SYMBOL_INFOW) unsigned char symBuf[sizeof(SYMBOL_INFOW) + MAX_SYM_NAME * sizeof(wchar_t)] = {};
			auto* symInfo = reinterpret_cast<PSYMBOL_INFOW>(symBuf);
			symInfo->SizeOfStruct = sizeof(SYMBOL_INFOW);
			symInfo->MaxNameLen = MAX_SYM_NAME;

			DWORD64 displacement = 0;
			if (api.symFromAddrW(process, address, &displacement, symInfo))
			{
				frame.symbolName = WideToUtf8Safe(std::wstring(symInfo->Name, symInfo->NameLen));
				frame.symbolDisplacement = displacement;
			}
		}

		if (hasSymbols && api.symGetLineFromAddrW64)
		{
			IMAGEHLP_LINEW64 lineInfo = {};
			lineInfo.SizeOfStruct = sizeof(lineInfo);
			DWORD lineDisp = 0;
			if (api.symGetLineFromAddrW64(process, address, &lineDisp, &lineInfo) && lineInfo.FileName)
			{
				frame.sourceFile = WideToUtf8Safe(std::wstring(lineInfo.FileName));
				frame.sourceLine = lineInfo.LineNumber;
			}
		}
	}

	std::string FormatLocation(const CrashStackFrame& frame)
	{
		std::ostringstream oss;
		if (!frame.moduleName.empty())
		{
			oss << frame.moduleName << "+0x" << std::hex << std::uppercase << frame.moduleOffset << std::dec;
		}
		else
		{
			oss << FormatHex64(frame.address);
		}

		if (!frame.symbolName.empty())
		{
			oss << " (" << frame.symbolName;
			if (frame.symbolDisplacement != 0)
				oss << "+0x" << std::hex << std::uppercase << frame.symbolDisplacement << std::dec;
			oss << ")";
		}

		if (!frame.sourceFile.empty() && frame.sourceLine > 0)
		{
			oss << " [" << frame.sourceFile << ":" << frame.sourceLine << "]";
		}

		return oss.str();
	}

	// Reads basic editor state inside a minimal SEH guard so corrupted globals
	// never prevent a crash report from being emitted.
	void SafeReadEditorContext(
		int* outLastOp,
		int* outLastLibOp,
		char* outMapBuf,
		size_t mapBufSize,
		char* outAppPathBuf,
		size_t appPathBufSize,
		char* outAppDataBuf,
		size_t appDataBufSize,
		char* outLogBuf,
		size_t logBufSize,
		char* outVulkanBuf,
		size_t vulkanBufSize)
	{
		__try
		{
			*outLastOp = last_succeeded_operation;
			*outLastLibOp = FSunPackLib::last_succeeded_operation;

			if (outMapBuf && mapBufSize > 0)
			{
				const char* mapPtr = currentMapFile.GetString();
				if (mapPtr)
				{
					strncpy_s(outMapBuf, mapBufSize, mapPtr, _TRUNCATE);
				}
			}
			if (outAppPathBuf && appPathBufSize > 0)
			{
				strncpy_s(outAppPathBuf, appPathBufSize, AppPath, _TRUNCATE);
			}
			if (outAppDataBuf && appDataBufSize > 0 && !u8AppDataPath.empty())
			{
				strncpy_s(outAppDataBuf, appDataBufSize, u8AppDataPath.c_str(), _TRUNCATE);
			}
			if (outLogBuf && logBufSize > 0)
			{
				const auto& logFile = theApp.getLogFileName();
				if (!logFile.empty())
				{
					strncpy_s(outLogBuf, logBufSize, logFile.c_str(), _TRUNCATE);
				}
			}
			if (outVulkanBuf && vulkanBufSize > 0)
			{
				rs_vulkan_last_error(outVulkanBuf, vulkanBufSize);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			// Ignore unreadable state during crash diagnostics.
		}
	}

	void PopulateCommonContext(CrashReportData& data, DWORD faultingThreadId)
	{
		data.timestamp = FormatTimestampNow();
		data.editorVariant = GetEditorVariantName();
		data.buildConfig = GetBuildConfigName();
		data.osVersion = QueryWindowsVersion();
		data.processId = GetCurrentProcessId();
		data.threadId = faultingThreadId != 0 ? faultingThreadId : GetCurrentThreadId();

		char mapBuf[1024] = { 0 };
		char appPathBuf[512] = { 0 };
		char appDataBuf[512] = { 0 };
		char logBuf[512] = { 0 };
		char vulkanBuf[1024] = { 0 };

		SafeReadEditorContext(
			&data.lastSucceededOperation,
			&data.lastLibraryOperation,
			mapBuf, sizeof(mapBuf),
			appPathBuf, sizeof(appPathBuf),
			appDataBuf, sizeof(appDataBuf),
			logBuf, sizeof(logBuf),
			vulkanBuf, sizeof(vulkanBuf));

		data.currentMapFile = mapBuf;
		data.appPath = appPathBuf;
		data.appDataPath = appDataBuf;
		data.logFilePath = logBuf;
		data.vulkanLastError = vulkanBuf;
	}

	constexpr size_t kMaxExceptionRecordChain = 16;

	struct ExceptionPointersSnapshotPod
	{
		bool hasContext = false;
		alignas(16) CONTEXT context = {};
		size_t recordCount = 0;
		EXCEPTION_RECORD records[kMaxExceptionRecordChain] = {};
	};

	bool CopyExceptionPointersSnapshotSafe(
		const EXCEPTION_POINTERS* src,
		ExceptionPointersSnapshotPod* out)
	{
		if (!src || !out)
			return false;

		__try
		{
			if (src->ContextRecord)
			{
				out->context = *src->ContextRecord;
				out->hasContext = true;
			}

			const EXCEPTION_RECORD* current = src->ExceptionRecord;
			while (current && out->recordCount < kMaxExceptionRecordChain)
			{
				out->records[out->recordCount] = *current;
				out->records[out->recordCount].ExceptionRecord = nullptr;
				++out->recordCount;
				current = current->ExceptionRecord;
			}
			return out->hasContext || out->recordCount > 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return out->hasContext || out->recordCount > 0;
		}
	}

	// Heap-owned task shared between the caller and the minidump worker thread.
	// If the caller times out or wait fails, the worker still owns a shared_ptr
	// keeping dumpPath, deep-copied exception records/context, and result fields
	// valid until the worker exits.
	struct MiniDumpTask
	{
		std::wstring dumpPath;
		DWORD faultingThreadId = 0;
		DWORD dbgHelpLockTimeoutMs = 2000;
		bool hasExceptionInfo = false;
		ExceptionPointersSnapshotPod exceptionSnapshot = {};
		EXCEPTION_POINTERS exceptionPointers = {};

		CrashReporter::MiniDumpExecutionOptions::WriterFn customWriter = nullptr;
		void* customWriterContext = nullptr;

		// Synchronized via worker thread exit (WAIT_OBJECT_0) or synchronous fallback.
		bool success = false;
		DWORD lastError = 0;
	};

	void PopulateMiniDumpExceptionSnapshot(const EXCEPTION_POINTERS* src, MiniDumpTask& task)
	{
		if (!CopyExceptionPointersSnapshotSafe(src, &task.exceptionSnapshot))
			return;

		for (size_t i = 0; i + 1 < task.exceptionSnapshot.recordCount; ++i)
		{
			task.exceptionSnapshot.records[i].ExceptionRecord = &task.exceptionSnapshot.records[i + 1];
		}
		if (task.exceptionSnapshot.recordCount > 0)
		{
			task.exceptionSnapshot.records[task.exceptionSnapshot.recordCount - 1].ExceptionRecord = nullptr;
			task.exceptionPointers.ExceptionRecord = &task.exceptionSnapshot.records[0];
		}
		if (task.exceptionSnapshot.hasContext)
		{
			task.exceptionPointers.ContextRecord = &task.exceptionSnapshot.context;
		}
		task.hasExceptionInfo = (task.exceptionPointers.ExceptionRecord != nullptr || task.exceptionPointers.ContextRecord != nullptr);
	}

	bool ExecuteMiniDumpTask(MiniDumpTask& task)
	{
		EXCEPTION_POINTERS* excPtr = task.hasExceptionInfo ? &task.exceptionPointers : nullptr;
		if (task.customWriter)
		{
			DWORD customErr = 0;
			const bool ok = task.customWriter(
				task.dumpPath,
				excPtr,
				task.faultingThreadId,
				&customErr,
				task.customWriterContext);
			task.lastError = ok ? 0 : customErr;
			task.success = ok;
			return ok;
		}

		std::unique_lock<std::timed_mutex> lock(g_dbgHelpMutex, std::defer_lock);
		if (!lock.try_lock_for(std::chrono::milliseconds(task.dbgHelpLockTimeoutMs)))
		{
			task.lastError = ERROR_BUSY;
			task.success = false;
			return false;
		}
		DbgHelpApi& api = LoadDbgHelp();
		if (!api.miniDumpWriteDump)
		{
			task.lastError = ERROR_PROC_NOT_FOUND;
			task.success = false;
			return false;
		}

		HANDLE file = CreateFileW(
			task.dumpPath.c_str(),
			GENERIC_WRITE,
			FILE_SHARE_READ,
			nullptr,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			task.lastError = GetLastError();
			task.success = false;
			return false;
		}

		MINIDUMP_EXCEPTION_INFORMATION excInfo = {};
		PMINIDUMP_EXCEPTION_INFORMATION excInfoPtr = nullptr;
		if (excPtr != nullptr)
		{
			excInfo.ThreadId = task.faultingThreadId != 0 ? task.faultingThreadId : GetCurrentThreadId();
			excInfo.ExceptionPointers = excPtr;
			excInfo.ClientPointers = FALSE;
			excInfoPtr = &excInfo;
		}

		const MINIDUMP_TYPE preferredType = static_cast<MINIDUMP_TYPE>(
			MiniDumpWithIndirectlyReferencedMemory |
			MiniDumpScanMemory |
			MiniDumpWithThreadInfo |
			MiniDumpWithUnloadedModules);

		BOOL ok = api.miniDumpWriteDump(
			GetCurrentProcess(),
			GetCurrentProcessId(),
			file,
			preferredType,
			excInfoPtr,
			nullptr,
			nullptr);

		if (!ok)
		{
			SetFilePointer(file, 0, nullptr, FILE_BEGIN);
			SetEndOfFile(file);
			ok = api.miniDumpWriteDump(
				GetCurrentProcess(),
				GetCurrentProcessId(),
				file,
				MiniDumpNormal,
				excInfoPtr,
				nullptr,
				nullptr);
		}

		if (!ok)
			task.lastError = GetLastError();

		CloseHandle(file);
		if (!ok)
		{
			DeleteFileW(task.dumpPath.c_str());
		}
		task.success = (ok != FALSE);
		return task.success;
	}

	DWORD WINAPI MiniDumpWorkerProc(LPVOID param)
	{
		std::unique_ptr<std::shared_ptr<MiniDumpTask>> holder(
			static_cast<std::shared_ptr<MiniDumpTask>*>(param));
		if (!holder || !(*holder))
			return 1;

		return ExecuteMiniDumpTask(**holder) ? 0 : 1;
	}

	bool WriteUtf8FileWithBom(const std::wstring& filePath, const std::string& content)
	{
		HANDLE file = CreateFileW(
			filePath.c_str(),
			GENERIC_WRITE,
			FILE_SHARE_READ,
			nullptr,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return false;

		const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
		DWORD written = 0;
		BOOL ok = WriteFile(file, bom, sizeof(bom), &written, nullptr) && (written == sizeof(bom));
		size_t totalWritten = 0;
		while (ok && totalWritten < content.size())
		{
			const size_t remaining = content.size() - totalWritten;
			const DWORD chunk = static_cast<DWORD>(remaining > (1u << 20) ? (1u << 20) : remaining);
			written = 0;
			ok = WriteFile(file, content.data() + totalWritten, chunk, &written, nullptr) && (written > 0);
			totalWritten += written;
		}
		if (ok)
			FlushFileBuffers(file);
		const BOOL closed = CloseHandle(file);
		return ok != FALSE && closed != FALSE;
	}

	void AppendToErrStreamSafe(const char* text)
	{
		__try
		{
			if (errstream.is_open() && text)
			{
				errstream << "\n" << text << std::endl;
				errstream.flush();
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			// Ignore stream errors during crash handling.
		}
	}

	bool InvokeEmergencyBackupFromSnapshot(
		const std::wstring* backupDir,
		const std::wstring* fileStamp,
		DWORD pid,
		MapSaveUtil::EmergencyBackupResult* outResult)
	{
		if (!backupDir || !fileStamp || !outResult)
			return false;
		*outResult = MapSaveUtil::WriteEmergencyMapBackup(*backupDir, *fileStamp, pid);
		return outResult->status == MapSaveUtil::EmergencyBackupStatus::Saved;
	}

	bool TryEmergencyBackupFromSnapshot(
		const std::wstring& backupDir,
		const std::wstring& fileStamp,
		DWORD pid,
		MapSaveUtil::EmergencyBackupResult* outResult,
		bool* outSehFaulted)
	{
		if (outSehFaulted)
			*outSehFaulted = false;
		if (!outResult)
			return false;
		__try
		{
			return InvokeEmergencyBackupFromSnapshot(&backupDir, &fileStamp, pid, outResult);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			if (outSehFaulted)
				*outSehFaulted = true;
			return false;
		}
	}

	bool SafeReadLanguageState(char* langBuf, size_t langBufSize, bool* outHasLoadedLanguage)
	{
		__try
		{
			if (outHasLoadedLanguage)
				*outHasLoadedLanguage = !language.sections.empty();
			if (langBuf && langBufSize > 0)
			{
				const char* ptr = theApp.m_Options.LanguageName.GetString();
				if (ptr)
					strncpy_s(langBuf, langBufSize, ptr, _TRUNCATE);
			}
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool CopyTextToClipboardW(const std::wstring& text)
	{
		if (!OpenClipboard(nullptr))
			return false;

		EmptyClipboard();
		const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
		HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
		if (!hMem)
		{
			CloseClipboard();
			return false;
		}
		void* dst = GlobalLock(hMem);
		if (!dst)
		{
			GlobalFree(hMem);
			CloseClipboard();
			return false;
		}
		std::memcpy(dst, text.c_str(), bytes);
		GlobalUnlock(hMem);
		SetClipboardData(CF_UNICODETEXT, hMem);
		CloseClipboard();
		return true;
	}

	struct CrashDialogContext
	{
		std::wstring fullReportW;
		std::wstring crashDirW;
		bool useChinese = false;
	};

	HRESULT CALLBACK CrashTaskDialogCallback(
		HWND hwnd,
		UINT notification,
		WPARAM wParam,
		LPARAM /*lParam*/,
		LONG_PTR refData)
	{
		auto* ctx = reinterpret_cast<CrashDialogContext*>(refData);
		if (!ctx)
			return S_OK;

		if (notification == TDN_BUTTON_CLICKED)
		{
			if (wParam == kBtnCopyDetails)
			{
				const bool copied = CopyTextToClipboardW(ctx->fullReportW);
				if (copied)
				{
					const wchar_t* footer = ctx->useChinese
						? L"已将完整崩溃报告复制到剪贴板。"
						: L"Full crash report copied to clipboard.";
					SendMessageW(hwnd, TDM_SET_ELEMENT_TEXT, TDE_FOOTER, reinterpret_cast<LPARAM>(footer));
				}
				return S_FALSE; // Keep dialog open
			}
			if (wParam == kBtnOpenCrashDir)
			{
				if (!ctx->crashDirW.empty())
				{
					ShellExecuteW(hwnd, L"open", ctx->crashDirW.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				}
				return S_FALSE; // Keep dialog open
			}
		}
		return S_OK;
	}

	bool TryShowTaskDialogSafe(const TASKDIALOGCONFIG* config)
	{
		__try
		{
			HMODULE comctl = LoadLibraryW(L"comctl32.dll");
			if (!comctl)
				return false;
			auto fn = reinterpret_cast<TaskDialogIndirectFn>(GetProcAddress(comctl, "TaskDialogIndirect"));
			if (!fn)
				return false;
			int pressedButton = 0;
			const HRESULT hr = fn(config, &pressedButton, nullptr, nullptr);
			return SUCCEEDED(hr);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	void ShowCrashDialog(
		const CrashReportData& data,
		const std::string& reportUtf8,
		const CrashArtifacts& artifacts,
		bool fatal)
	{
		const bool useChinese = CrashReporter::ShouldUseChineseUI();
		const std::wstring titleW = useChinese
			? (fatal ? L"地图编辑器发生致命错误" : L"地图编辑器发生未处理异常")
			: (fatal ? L"Mission Editor Fatal Error" : L"Mission Editor Unhandled Exception");

		const std::wstring mainInstructionW = useChinese
			? (fatal ? L"程序遇到未处理的错误，即将关闭。" : L"当前操作遇到未处理的异常。")
			: (fatal ? L"The editor encountered an unhandled error and must close." : L"An unhandled exception occurred during the current operation.");

		const std::wstring contentW = CrashReporter::BuildCrashSummaryText(data, artifacts, useChinese);
		const std::wstring fullReportW = Utf8ToWideSafe(reportUtf8);

		CrashDialogContext dlgCtx;
		dlgCtx.fullReportW = fullReportW;
		dlgCtx.crashDirW = artifacts.crashDirectory;
		dlgCtx.useChinese = useChinese;

		const std::wstring copyBtnW = useChinese ? L"复制错误信息" : L"Copy Report";
		const std::wstring openDirBtnW = useChinese ? L"打开崩溃日志目录" : L"Open Crash Folder";
		const std::wstring closeBtnW = useChinese
			? (fatal ? L"关闭程序" : L"确定")
			: (fatal ? L"Close" : L"OK");
		const std::wstring expandTextW = useChinese ? L"查看完整调用栈与诊断详情" : L"Show full stack trace and details";
		const std::wstring collapseTextW = useChinese ? L"隐藏诊断详情" : L"Hide details";
		const std::wstring footerW = useChinese
			? L"反馈问题时请附上崩溃报告 (.txt) 和转储文件 (.dmp)。"
			: L"Please attach the crash report (.txt) and minidump (.dmp) when reporting this issue.";

		TASKDIALOG_BUTTON buttons[3] = {
			{ kBtnCopyDetails, copyBtnW.c_str() },
			{ kBtnOpenCrashDir, openDirBtnW.c_str() },
			{ kBtnCloseApp, closeBtnW.c_str() },
		};

		TASKDIALOGCONFIG cfg = {};
		cfg.cbSize = sizeof(cfg);
		cfg.hwndParent = nullptr;
		cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_EXPAND_FOOTER_AREA | TDF_SIZE_TO_CONTENT;
		cfg.pszWindowTitle = titleW.c_str();
		cfg.pszMainIcon = TD_ERROR_ICON;
		cfg.pszMainInstruction = mainInstructionW.c_str();
		cfg.pszContent = contentW.c_str();
		cfg.cButtons = _countof(buttons);
		cfg.pButtons = buttons;
		cfg.nDefaultButton = kBtnCloseApp;
		cfg.pszExpandedInformation = fullReportW.c_str();
		cfg.pszExpandedControlText = collapseTextW.c_str();
		cfg.pszCollapsedControlText = expandTextW.c_str();
		cfg.pszFooter = footerW.c_str();
		cfg.pfCallback = CrashTaskDialogCallback;
		cfg.lpCallbackData = reinterpret_cast<LONG_PTR>(&dlgCtx);

		if (TryShowTaskDialogSafe(&cfg))
			return;

		// Guaranteed fallback when TaskDialogIndirect is unavailable.
		std::wstring fallbackMsg = mainInstructionW + L"\n\n" + contentW;
		if (!fullReportW.empty())
		{
			fallbackMsg += L"\n\n";
			const size_t maxChars = 2400;
			if (fullReportW.size() > maxChars)
				fallbackMsg += fullReportW.substr(0, maxChars) + L"\n...";
			else
				fallbackMsg += fullReportW;
		}
		MessageBoxW(
			nullptr,
			fallbackMsg.c_str(),
			titleW.c_str(),
			MB_OK | MB_ICONERROR | MB_TASKMODAL | MB_SETFOREGROUND);
	}

	// Best-effort extraction of std::exception::what() from an MSVC C++ exception
	// (0xE06D7363) when it reaches SEH unhandled filter without a C++ catch block.
	bool TryExtractMsvcExceptionWhat(
		const EXCEPTION_RECORD* rec,
		char* outBuf,
		size_t outBufSize)
	{
		if (!rec || rec->ExceptionCode != kMsvcCppExceptionCode || !outBuf || outBufSize == 0)
			return false;
		if (rec->NumberParameters < 3)
			return false;

		__try
		{
			void* thrownObject = reinterpret_cast<void*>(rec->ExceptionInformation[1]);
			if (!thrownObject)
				return false;

			// Only probe via dynamic_cast if we can confirm vtable readability;
			// even safer, rely on C++ catch blocks and std::set_terminate when possible.
			// Here we conservatively avoid dereferencing arbitrary non-std::exception types.
			return false;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	struct CrashFlowRequest
	{
		CrashReportData data;
		EXCEPTION_POINTERS* exceptionPointers = nullptr;
		const CONTEXT* stackCaptureContext = nullptr;
		int captureSkipFrames = 0;
		DWORD faultingThreadId = 0;
		bool allowEmergencySave = true;
		bool showDialog = true;
		bool fatal = true;
		HANDLE diagnosticsDoneEvent = nullptr;
	};

	void BuildArtifactPaths(
		const std::wstring& crashDirectory,
		DWORD processId,
		std::wstring& outReportPath,
		std::wstring& outDumpPath)
	{
		std::wstring fileStamp;
		FormatTimestampNow(&fileStamp);

		wchar_t baseName[128];
		std::swprintf(
			baseName,
			_countof(baseName),
			L"crash_%ls_pid%lu",
			fileStamp.c_str(),
			static_cast<unsigned long>(processId != 0 ? processId : GetCurrentProcessId()));

		std::wstring prefix = crashDirectory;
		if (!prefix.empty() && prefix.back() != L'\\' && prefix.back() != L'/')
			prefix.push_back(L'\\');
		prefix += baseName;

		outReportPath = prefix + L".txt";
		outDumpPath = prefix + L".dmp";
	}

	void ExecuteCrashFlow(CrashFlowRequest& req)
	{
		if (req.fatal)
			g_crashPhase.store(kCrashPhaseDiagnostics, std::memory_order_release);

		// Step 0: Resolve crash directory and emit a minimal diagnostic report FIRST,
		// without acquiring DbgHelp locks or running symbolization/minidump.
		const std::wstring crashDir = ResolveCrashDirectoryW();
		CreateDirectoryRecursiveW(crashDir);

		std::wstring reportPath;
		std::wstring dumpPath;
		BuildArtifactPaths(crashDir, req.data.processId, reportPath, dumpPath);

		if (req.data.faultAddress == 0 && req.data.hasRegisters)
		{
#ifdef _M_X64
			req.data.faultAddress = req.data.registers.Rip;
#endif
		}
		if (req.data.faultLocation.empty() && req.data.faultAddress != 0)
		{
			CrashStackFrame minimalFaultFrame;
			ResolveFrameModuleOnlySafe(req.data.faultAddress, minimalFaultFrame);
			req.data.faultLocation = FormatLocation(minimalFaultFrame);
		}

		CrashArtifacts minimalArtifacts;
		minimalArtifacts.crashDirectory = crashDir;
		minimalArtifacts.reportFilePath = reportPath;
		minimalArtifacts.dumpFilePath = dumpPath;
		minimalArtifacts.dumpStatus = CrashDumpStatus::NotAttempted;
		const std::string minimalReportUtf8 = CrashReporter::FormatCrashReport(req.data, &minimalArtifacts);
		minimalArtifacts.reportWritten = WriteUtf8FileWithBom(reportPath, minimalReportUtf8);

		// Step 1: Optional bounded stack capture and symbolization (degrades cleanly if DbgHelp is busy).
		if (req.data.stackFrames.empty())
		{
			const CONTEXT* ctx = req.stackCaptureContext;
			if (!ctx && req.exceptionPointers && req.exceptionPointers->ContextRecord)
				ctx = req.exceptionPointers->ContextRecord;
			const int skip = ctx ? req.captureSkipFrames : (req.captureSkipFrames > 0 ? req.captureSkipFrames : 2);
			req.data.stackFrames = CrashReporter::CaptureStackFramesBounded(
				ctx,
				skip,
				64,
				kDbgHelpStackLockTimeoutMs,
				nullptr);
		}

		if (req.data.faultAddress != 0)
		{
			std::unique_lock<std::timed_mutex> lock(g_dbgHelpMutex, std::defer_lock);
			if (lock.try_lock_for(std::chrono::milliseconds(kDbgHelpSymbolLockTimeoutMs)))
			{
				CrashStackFrame faultFrame;
				DbgHelpApi& api = LoadDbgHelp();
				const bool hasSymbols = EnsureSymbolsInitialized(api);
				ResolveFrameInfo(GetCurrentProcess(), api, hasSymbols, req.data.faultAddress, faultFrame);
				const std::string resolvedLocation = FormatLocation(faultFrame);
				if (!resolvedLocation.empty())
					req.data.faultLocation = resolvedLocation;
			}
		}
		else if (!req.data.stackFrames.empty())
		{
			req.data.faultAddress = req.data.stackFrames[0].address;
			req.data.faultLocation = FormatLocation(req.data.stackFrames[0]);
		}

		// Step 2: Write minidump + updated crash report BEFORE touching editor state or UI.
		CrashReporter::MiniDumpExecutionOptions dumpOpts;
		dumpOpts.precomputedReportPath = reportPath;
		dumpOpts.precomputedDumpPath = dumpPath;
		CrashArtifacts artifacts = CrashReporter::WriteCrashArtifactsEx(
			crashDir,
			req.data,
			req.exceptionPointers,
			req.faultingThreadId,
			dumpOpts);

		// Step 3: Attempt non-UI, non-reentrant emergency backup from the last committed
		//         immutable map snapshot (guarded by SEH, never touching live Map or UI).
		if (req.allowEmergencySave && req.fatal)
		{
			std::wstring backupDir;
			if (!u8AppDataPath.empty())
			{
				backupDir = Utf8ToWideSafe(u8AppDataPath);
			}
			else if (!crashDir.empty())
			{
				backupDir = crashDir;
			}

			if (!backupDir.empty())
			{
				std::wstring fileStamp;
				FormatTimestampNow(&fileStamp);
				const DWORD pid = req.data.processId != 0 ? req.data.processId : GetCurrentProcessId();
				MapSaveUtil::EmergencyBackupResult backupResult;
				bool sehFaulted = false;
				if (TryEmergencyBackupFromSnapshot(backupDir, fileStamp, pid, &backupResult, &sehFaulted))
				{
					artifacts.backupMapPath = backupResult.actualPath;
					artifacts.backupMapSaved = true;
					artifacts.backupMapError.clear();
				}
				else
				{
					artifacts.backupMapPath.clear();
					artifacts.backupMapSaved = false;
					artifacts.backupMapError = sehFaulted
						? "SEH exception while writing emergency backup"
						: backupResult.error;
				}
			}
		}

		// Re-write report text so it includes the final dump & backup map status.
		const std::string finalReportUtf8 = CrashReporter::FormatCrashReport(req.data, &artifacts);
		if (!artifacts.reportFilePath.empty())
		{
			artifacts.reportWritten = WriteUtf8FileWithBom(artifacts.reportFilePath, finalReportUtf8);
		}

		AppendToErrStreamSafe(finalReportUtf8.c_str());

		const std::wstring debugOut = Utf8ToWideSafe(finalReportUtf8 + "\n");
		OutputDebugStringW(debugOut.c_str());

		if (req.diagnosticsDoneEvent)
			SetEvent(req.diagnosticsDoneEvent);

		if (req.fatal)
			g_crashPhase.store(kCrashPhaseDialog, std::memory_order_release);

		if (req.showDialog && !IsDebuggerPresent())
		{
			ShowCrashDialog(req.data, finalReportUtf8, artifacts, req.fatal);
		}

		if (req.fatal)
			g_crashPhase.store(kCrashPhaseFinished, std::memory_order_release);
	}

	// Heap-owned stack-overflow crash task shared between faulting thread and
	// fresh-stack worker so a bounded wait/timeout never causes UAF.
	struct StackOverflowCrashTask
	{
		CrashFlowRequest req;
		ExceptionPointersSnapshotPod exceptionSnapshot = {};
		EXCEPTION_POINTERS exceptionPointers = {};
		bool hasExceptionInfo = false;
		HANDLE diagnosticsDoneEvent = nullptr;

		// Optional hook used by unit tests.
		void(*customWorkerHook)(const CrashReportData&, const EXCEPTION_POINTERS*, void*) = nullptr;
		void* customWorkerHookContext = nullptr;

		~StackOverflowCrashTask()
		{
			if (diagnosticsDoneEvent)
			{
				CloseHandle(diagnosticsDoneEvent);
				diagnosticsDoneEvent = nullptr;
			}
		}
	};

	void PopulateStackOverflowExceptionSnapshot(const EXCEPTION_POINTERS* src, StackOverflowCrashTask& task)
	{
		if (!CopyExceptionPointersSnapshotSafe(src, &task.exceptionSnapshot))
			return;

		for (size_t i = 0; i + 1 < task.exceptionSnapshot.recordCount; ++i)
		{
			task.exceptionSnapshot.records[i].ExceptionRecord = &task.exceptionSnapshot.records[i + 1];
		}
		if (task.exceptionSnapshot.recordCount > 0)
		{
			task.exceptionSnapshot.records[task.exceptionSnapshot.recordCount - 1].ExceptionRecord = nullptr;
			task.exceptionPointers.ExceptionRecord = &task.exceptionSnapshot.records[0];
		}
		if (task.exceptionSnapshot.hasContext)
		{
			task.exceptionPointers.ContextRecord = &task.exceptionSnapshot.context;
		}
		task.hasExceptionInfo = (task.exceptionPointers.ExceptionRecord != nullptr || task.exceptionPointers.ContextRecord != nullptr);
		if (task.hasExceptionInfo)
			task.req.exceptionPointers = &task.exceptionPointers;
	}

	DWORD WINAPI StackOverflowCrashThreadProc(LPVOID param)
	{
		std::unique_ptr<std::shared_ptr<StackOverflowCrashTask>> holder(
			static_cast<std::shared_ptr<StackOverflowCrashTask>*>(param));
		if (!holder || !(*holder))
			return 0;

		StackOverflowCrashTask& task = **holder;
		if (task.customWorkerHook)
		{
			task.customWorkerHook(
				task.req.data,
				task.hasExceptionInfo ? &task.exceptionPointers : nullptr,
				task.customWorkerHookContext);
			if (task.diagnosticsDoneEvent)
				SetEvent(task.diagnosticsDoneEvent);
			return 0;
		}

		ExecuteCrashFlow(task.req);
		return 0;
	}

	bool EnterCrashGuard()
	{
		const DWORD tid = GetCurrentThreadId();
		DWORD expected = 0;
		if (g_activeCrashThreadId.compare_exchange_strong(expected, tid))
		{
			g_crashPhase.store(kCrashPhaseDiagnostics, std::memory_order_release);
			return true;
		}

		// Another thread is already handling a crash: wait boundedly while it
		// finishes writing diagnostics so we don't kill the process mid-write,
		// yet never hang indefinitely if the primary handler is wedged.
		if (expected != tid)
		{
			const DWORD startTick = GetTickCount();
			while (g_activeCrashThreadId.load(std::memory_order_acquire) != 0)
			{
				const int phase = g_crashPhase.load(std::memory_order_acquire);
				if (phase >= kCrashPhaseFinished)
					break;
				const DWORD elapsed = GetTickCount() - startTick;
				const DWORD limit = (phase == kCrashPhaseDialog)
					? kStackOverflowDialogMaxWaitMs
					: kSecondaryCrashMaxWaitMs;
				if (elapsed >= limit)
					break;
				Sleep(50);
			}
		}
		return false;
	}

	LONG WINAPI UnhandledExceptionFilterProc(EXCEPTION_POINTERS* exceptionInfo)
	{
		if (!EnterCrashGuard())
			return EXCEPTION_EXECUTE_HANDLER;

		CrashFlowRequest req;
		req.exceptionPointers = exceptionInfo;
		req.faultingThreadId = GetCurrentThreadId();
		req.fatal = true;
		req.allowEmergencySave = true;
		req.showDialog = true;

		PopulateCommonContext(req.data, req.faultingThreadId);
		req.data.crashSource = "Unhandled Win32 SEH Exception";

		DWORD exceptionCode = 0;
		if (exceptionInfo && exceptionInfo->ExceptionRecord)
		{
			const auto* rec = exceptionInfo->ExceptionRecord;
			exceptionCode = rec->ExceptionCode;
			req.data.exception = CrashReporter::DescribeExceptionCode(
				rec->ExceptionCode,
				rec->ExceptionInformation,
				rec->NumberParameters);
			req.data.faultAddress = reinterpret_cast<DWORD64>(rec->ExceptionAddress);

			char whatBuf[512] = { 0 };
			if (TryExtractMsvcExceptionWhat(rec, whatBuf, sizeof(whatBuf)))
				req.data.cppExceptionMessage = whatBuf;
		}
		if (exceptionInfo && exceptionInfo->ContextRecord)
		{
			req.data.hasRegisters = true;
			req.data.registers = *exceptionInfo->ContextRecord;
		}

		// For stack overflow, run the entire report/dump/UI on a fresh thread stack
		// with heap-owned inputs and a bounded wait so we never hang or UAF.
		if (exceptionCode == EXCEPTION_STACK_OVERFLOW)
		{
			auto task = std::make_shared<StackOverflowCrashTask>();
			task->req = std::move(req);
			task->req.exceptionPointers = nullptr;
			task->req.allowEmergencySave = false; // Avoid heavy work on overflowed process
			task->diagnosticsDoneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			task->req.diagnosticsDoneEvent = task->diagnosticsDoneEvent;
			PopulateStackOverflowExceptionSnapshot(exceptionInfo, *task);

			auto* workerParam = new std::shared_ptr<StackOverflowCrashTask>(task);
			HANDLE hThread = CreateThread(nullptr, 0, StackOverflowCrashThreadProc, workerParam, 0, nullptr);
			if (hThread)
			{
				if (task->diagnosticsDoneEvent)
				{
					HANDLE handles[2] = { hThread, task->diagnosticsDoneEvent };
					const DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, kStackOverflowDiagnosticsTimeoutMs);
					if (waitResult == WAIT_OBJECT_0 + 1)
					{
						// Diagnostics completed cleanly; allow bounded time for user crash dialog.
						WaitForSingleObject(hThread, kStackOverflowDialogMaxWaitMs);
					}
				}
				else
				{
					WaitForSingleObject(hThread, kStackOverflowDiagnosticsTimeoutMs);
				}
				CloseHandle(hThread);
			}
			else
			{
				delete workerParam;
			}
		}
		else
		{
			ExecuteCrashFlow(req);
		}

		if (IsDebuggerPresent())
		{
			g_crashPhase.store(kCrashPhaseIdle, std::memory_order_release);
			g_activeCrashThreadId.store(0);
			return EXCEPTION_CONTINUE_SEARCH;
		}

		TerminateProcess(GetCurrentProcess(), exceptionCode != 0 ? exceptionCode : kCrashExitCode);
		return EXCEPTION_EXECUTE_HANDLER;
	}

	[[noreturn]] void ReportFatalRuntimeEvent(
		const char* source,
		const std::string& name,
		const std::string& description,
		const std::string& extraDetails,
		const std::string& cppType = {},
		const std::string& cppMsg = {})
	{
		if (!EnterCrashGuard())
			TerminateProcess(GetCurrentProcess(), kCrashExitCode);

		CONTEXT ctx = {};
		RtlCaptureContext(&ctx);
		EXCEPTION_RECORD rec = {};
		rec.ExceptionCode = kCrashExitCode;
		rec.ExceptionAddress = reinterpret_cast<PVOID>(ctx.Rip);
		EXCEPTION_POINTERS ep = {};
		ep.ExceptionRecord = &rec;
		ep.ContextRecord = &ctx;

		CrashFlowRequest req;
		req.exceptionPointers = &ep;
		req.faultingThreadId = GetCurrentThreadId();
		req.fatal = true;
		req.allowEmergencySave = true;
		req.showDialog = true;

		PopulateCommonContext(req.data, req.faultingThreadId);
		req.data.crashSource = source ? source : "Fatal Runtime Error";
		req.data.exception.code = kCrashExitCode;
		req.data.exception.name = name;
		req.data.exception.description = description;
		req.data.exception.extraDetails = extraDetails;
		req.data.cppExceptionType = cppType;
		req.data.cppExceptionMessage = cppMsg;
		req.data.hasRegisters = true;
		req.data.registers = ctx;
		req.stackCaptureContext = &ctx;
		req.captureSkipFrames = 1;

		ExecuteCrashFlow(req);

		if (IsDebuggerPresent())
		{
			DebugBreak();
		}
		TerminateProcess(GetCurrentProcess(), kCrashExitCode);
	}

	void TerminateHandler()
	{
		std::string cppType;
		std::string cppMsg;
		if (std::exception_ptr eptr = std::current_exception())
		{
			try
			{
				std::rethrow_exception(eptr);
			}
			catch (const std::exception& e)
			{
				cppType = typeid(e).name();
				cppMsg = e.what() ? e.what() : "";
			}
			catch (...)
			{
				cppType = "Unknown C++ exception";
			}
		}

		ReportFatalRuntimeEvent(
			"C++ std::terminate",
			"STD_TERMINATE",
			"std::terminate() was invoked (unhandled C++ exception or noexcept violation).",
			{},
			cppType,
			cppMsg);
	}

	void InvalidParameterHandler(
		const wchar_t* expression,
		const wchar_t* function,
		const wchar_t* file,
		unsigned int line,
		uintptr_t /*reserved*/)
	{
		std::ostringstream oss;
		if (expression)
			oss << "Expression: " << WideToUtf8Safe(expression) << "\n";
		if (function)
			oss << "Function: " << WideToUtf8Safe(function) << "\n";
		if (file)
			oss << "Location: " << WideToUtf8Safe(file) << ":" << line << "\n";

		ReportFatalRuntimeEvent(
			"CRT Invalid Parameter",
			"CRT_INVALID_PARAMETER",
			"An invalid parameter was passed to a C runtime function.",
			oss.str());
	}

	void PurecallHandler()
	{
		ReportFatalRuntimeEvent(
			"CRT Pure Virtual Function Call",
			"CRT_PURECALL",
			"A pure virtual function was called (likely use-after-free or call during construction/destruction).",
			{});
	}

	void AbortSignalHandler(int /*sig*/)
	{
		ReportFatalRuntimeEvent(
			"CRT SIGABRT (abort)",
			"SIGABRT",
			"The process aborted via abort() or a fatal assertion failure.",
			{});
	}

	void DisableCallbackExceptionSwallowing()
	{
		// On WOW64 / older Windows configurations, disable silent swallowing of
		// exceptions thrown across user-mode callbacks (WndProc).
		using SetPolicyFn = BOOL(WINAPI*)(DWORD);
		using GetPolicyFn = BOOL(WINAPI*)(LPDWORD);
		HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
		if (!kernel32)
			return;
		auto getPolicy = reinterpret_cast<GetPolicyFn>(GetProcAddress(kernel32, "GetProcessUserModeExceptionPolicy"));
		auto setPolicy = reinterpret_cast<SetPolicyFn>(GetProcAddress(kernel32, "SetProcessUserModeExceptionPolicy"));
		if (getPolicy && setPolicy)
		{
			DWORD flags = 0;
			if (getPolicy(&flags))
			{
				constexpr DWORD kProcessCallbackFilterEnabled = 0x1;
				setPolicy(flags & ~kProcessCallbackFilterEnabled);
			}
		}
	}
}

namespace CrashReporter
{
	void Install()
	{
		// Reserve stack space so EXCEPTION_STACK_OVERFLOW can still enter our filter
		// and spawn a worker thread cleanly.
		ULONG stackGuarantee = 64 * 1024;
		SetThreadStackGuarantee(&stackGuarantee);

		DisableCallbackExceptionSwallowing();

		SetUnhandledExceptionFilter(UnhandledExceptionFilterProc);
		std::set_terminate(TerminateHandler);
		_set_invalid_parameter_handler(InvalidParameterHandler);
		_set_purecall_handler(PurecallHandler);
		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
		signal(SIGABRT, AbortSignalHandler);
	}

	void ReportUnhandledCppException(
		const char* source,
		const char* exceptionType,
		const char* message,
		bool fatal)
	{
		if (fatal && !EnterCrashGuard())
			return;

		CONTEXT ctx = {};
		RtlCaptureContext(&ctx);
		EXCEPTION_RECORD rec = {};
		rec.ExceptionCode = kMsvcCppExceptionCode;
		rec.ExceptionAddress = reinterpret_cast<PVOID>(ctx.Rip);
		EXCEPTION_POINTERS ep = {};
		ep.ExceptionRecord = &rec;
		ep.ContextRecord = &ctx;

		CrashFlowRequest req;
		req.exceptionPointers = &ep;
		req.faultingThreadId = GetCurrentThreadId();
		req.fatal = fatal;
		req.allowEmergencySave = fatal;
		req.showDialog = true;

		PopulateCommonContext(req.data, req.faultingThreadId);
		req.data.crashSource = source ? source : "Unhandled C++ Exception";
		req.data.exception = DescribeExceptionCode(kMsvcCppExceptionCode);
		req.data.cppExceptionType = exceptionType ? exceptionType : "std::exception";
		req.data.cppExceptionMessage = message ? message : "";
		req.data.hasRegisters = true;
		req.data.registers = ctx;
		req.stackCaptureContext = &ctx;
		req.captureSkipFrames = 1;

		ExecuteCrashFlow(req);

		if (fatal)
		{
			if (IsDebuggerPresent())
			{
				g_crashPhase.store(kCrashPhaseIdle, std::memory_order_release);
				g_activeCrashThreadId.store(0);
				DebugBreak();
			}
			else
			{
				TerminateProcess(GetCurrentProcess(), kMsvcCppExceptionCode);
			}
		}
	}

	void ReportMfcException(CException* e, const MSG* pMsg)
	{
		char errorBuf[1024] = { 0 };
		if (e)
		{
			e->GetErrorMessage(errorBuf, static_cast<UINT>(sizeof(errorBuf)));
		}

		std::ostringstream extra;
		if (pMsg)
		{
			extra << "Window message: 0x" << std::hex << std::uppercase << pMsg->message
				<< " (hwnd=" << pMsg->hwnd
				<< ", wParam=0x" << pMsg->wParam
				<< ", lParam=0x" << pMsg->lParam << ")" << std::dec;
		}

		CONTEXT ctx = {};
		RtlCaptureContext(&ctx);

		CrashFlowRequest req;
		req.exceptionPointers = nullptr;
		req.faultingThreadId = GetCurrentThreadId();
		req.fatal = false;
		req.allowEmergencySave = false;
		req.showDialog = true;

		PopulateCommonContext(req.data, req.faultingThreadId);
		req.data.crashSource = "MFC Window Procedure Exception (CException)";
		req.data.exception.code = 0;
		req.data.exception.name = "MFC_CEXCEPTION";
		req.data.exception.description = "An unhandled MFC CException occurred while processing a window message.";
		req.data.exception.extraDetails = extra.str();
		req.data.cppExceptionType = (e && e->GetRuntimeClass() && e->GetRuntimeClass()->m_lpszClassName)
			? e->GetRuntimeClass()->m_lpszClassName
			: "CException";
		req.data.cppExceptionMessage = errorBuf[0] ? errorBuf : "No MFC error message available.";
		req.stackCaptureContext = &ctx;
		req.captureSkipFrames = 1;

		ExecuteCrashFlow(req);
	}

	CrashExceptionSummary DescribeExceptionCode(
		DWORD code,
		const ULONG_PTR* exceptionInfo,
		DWORD numberParameters)
	{
		CrashExceptionSummary summary;
		summary.code = code;

		switch (code)
		{
		case EXCEPTION_ACCESS_VIOLATION:
		{
			summary.name = "EXCEPTION_ACCESS_VIOLATION";
			summary.description = "The thread tried to read from or write to an invalid memory address.";
			if (exceptionInfo && numberParameters >= 2)
			{
				const char* op = "Access";
				if (exceptionInfo[0] == 0)
					op = "Read";
				else if (exceptionInfo[0] == 1)
					op = "Write";
				else if (exceptionInfo[0] == 8)
					op = "DEP / Execute";

				std::ostringstream oss;
				oss << op << " violation at address " << FormatHex64(static_cast<DWORD64>(exceptionInfo[1]));
				summary.extraDetails = oss.str();
			}
			break;
		}
		case EXCEPTION_IN_PAGE_ERROR:
		{
			summary.name = "EXCEPTION_IN_PAGE_ERROR";
			summary.description = "The thread tried to access a page that could not be loaded into memory.";
			if (exceptionInfo && numberParameters >= 3)
			{
				std::ostringstream oss;
				oss << "Address " << FormatHex64(static_cast<DWORD64>(exceptionInfo[1]))
					<< ", NTSTATUS " << FormatHex32(static_cast<DWORD>(exceptionInfo[2]));
				summary.extraDetails = oss.str();
			}
			break;
		}
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
			summary.name = "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
			summary.description = "The thread tried to access an array element that is out of bounds.";
			break;
		case EXCEPTION_BREAKPOINT:
			summary.name = "EXCEPTION_BREAKPOINT";
			summary.description = "A breakpoint was encountered.";
			break;
		case EXCEPTION_DATATYPE_MISALIGNMENT:
			summary.name = "EXCEPTION_DATATYPE_MISALIGNMENT";
			summary.description = "The thread tried to read or write misaligned data.";
			break;
		case EXCEPTION_FLT_DENORMAL_OPERAND:
			summary.name = "EXCEPTION_FLT_DENORMAL_OPERAND";
			summary.description = "A floating-point operand is denormal.";
			break;
		case EXCEPTION_FLT_DIVIDE_BY_ZERO:
			summary.name = "EXCEPTION_FLT_DIVIDE_BY_ZERO";
			summary.description = "The thread tried to divide a floating-point value by zero.";
			break;
		case EXCEPTION_FLT_INEXACT_RESULT:
			summary.name = "EXCEPTION_FLT_INEXACT_RESULT";
			summary.description = "A floating-point operation produced an inexact result.";
			break;
		case EXCEPTION_FLT_INVALID_OPERATION:
			summary.name = "EXCEPTION_FLT_INVALID_OPERATION";
			summary.description = "An invalid floating-point operation occurred.";
			break;
		case EXCEPTION_FLT_OVERFLOW:
			summary.name = "EXCEPTION_FLT_OVERFLOW";
			summary.description = "A floating-point overflow occurred.";
			break;
		case EXCEPTION_FLT_STACK_CHECK:
			summary.name = "EXCEPTION_FLT_STACK_CHECK";
			summary.description = "The stack overflowed or underflowed as the result of a floating-point operation.";
			break;
		case EXCEPTION_FLT_UNDERFLOW:
			summary.name = "EXCEPTION_FLT_UNDERFLOW";
			summary.description = "A floating-point underflow occurred.";
			break;
		case EXCEPTION_ILLEGAL_INSTRUCTION:
			summary.name = "EXCEPTION_ILLEGAL_INSTRUCTION";
			summary.description = "The thread tried to execute an invalid instruction.";
			break;
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
			summary.name = "EXCEPTION_INT_DIVIDE_BY_ZERO";
			summary.description = "The thread tried to divide an integer value by zero.";
			break;
		case EXCEPTION_INT_OVERFLOW:
			summary.name = "EXCEPTION_INT_OVERFLOW";
			summary.description = "An integer operation caused an overflow.";
			break;
		case EXCEPTION_INVALID_DISPOSITION:
			summary.name = "EXCEPTION_INVALID_DISPOSITION";
			summary.description = "An exception handler returned an invalid disposition.";
			break;
		case EXCEPTION_NONCONTINUABLE_EXCEPTION:
			summary.name = "EXCEPTION_NONCONTINUABLE_EXCEPTION";
			summary.description = "The thread tried to continue execution after a noncontinuable exception.";
			break;
		case EXCEPTION_PRIV_INSTRUCTION:
			summary.name = "EXCEPTION_PRIV_INSTRUCTION";
			summary.description = "The thread tried to execute a privileged instruction.";
			break;
		case EXCEPTION_SINGLE_STEP:
			summary.name = "EXCEPTION_SINGLE_STEP";
			summary.description = "A single-step instruction trap occurred.";
			break;
		case EXCEPTION_STACK_OVERFLOW:
			summary.name = "EXCEPTION_STACK_OVERFLOW";
			summary.description = "The thread exhausted its stack (infinite recursion or excessive stack allocation).";
			break;
		case kStatusHeapCorruption:
			summary.name = "STATUS_HEAP_CORRUPTION";
			summary.description = "Windows detected heap corruption.";
			break;
		case kStatusStackBufferOverrun:
			summary.name = "STATUS_STACK_BUFFER_OVERRUN";
			summary.description = "A stack buffer overrun or fail-fast security check was triggered.";
			break;
		case kMsvcCppExceptionCode:
			summary.name = "MSVC_CPP_EXCEPTION";
			summary.description = "An unhandled C++ exception escaped to the OS exception handler.";
			break;
		default:
			summary.name = "UNKNOWN_EXCEPTION";
			summary.description = "An unrecognized exception code was raised.";
			break;
		}

		return summary;
	}

	std::vector<CrashStackFrame> CaptureStackFrames(
		const CONTEXT* context,
		int skipFrames,
		int maxFrames)
	{
		return CaptureStackFramesBounded(context, skipFrames, maxFrames, kDbgHelpStackLockTimeoutMs, nullptr);
	}

	std::vector<CrashStackFrame> CaptureStackFramesBounded(
		const CONTEXT* context,
		int skipFrames,
		int maxFrames,
		DWORD lockTimeoutMs,
		bool* outSymbolized)
	{
		if (outSymbolized)
			*outSymbolized = false;

		std::vector<CrashStackFrame> frames;
		if (maxFrames <= 0)
			return frames;
		if (skipFrames < 0)
			skipFrames = 0;

		std::unique_lock<std::timed_mutex> lock(g_dbgHelpMutex, std::defer_lock);
		if (!lock.try_lock_for(std::chrono::milliseconds(lockTimeoutMs)))
		{
			// DbgHelp lock is contended or wedged: degrade gracefully to lock-free
			// module+offset frames without symbolization.
			if (context != nullptr)
			{
#ifdef _M_X64
				if (context->Rip != 0 && static_cast<int>(frames.size()) < maxFrames)
				{
					CrashStackFrame frame;
					ResolveFrameModuleOnlySafe(context->Rip, frame);
					frames.push_back(std::move(frame));
				}
#endif
			}
			const int remaining = maxFrames - static_cast<int>(frames.size());
			if (remaining > 0)
			{
				std::vector<void*> raw(static_cast<size_t>(remaining), nullptr);
				const USHORT captured = CaptureStackBackTrace(
					static_cast<DWORD>(skipFrames + (context == nullptr ? 1 : 0)),
					static_cast<DWORD>(remaining),
					raw.data(),
					nullptr);
				for (USHORT i = 0; i < captured; ++i)
				{
					CrashStackFrame frame;
					ResolveFrameModuleOnlySafe(reinterpret_cast<DWORD64>(raw[i]), frame);
					frames.push_back(std::move(frame));
				}
			}
			return frames;
		}

		DbgHelpApi& api = LoadDbgHelp();
		const HANDLE process = GetCurrentProcess();
		const HANDLE thread = GetCurrentThread();
		const bool hasSymbols = EnsureSymbolsInitialized(api);
		if (outSymbolized)
			*outSymbolized = hasSymbols;

		CONTEXT localCtx = {};
		if (context != nullptr)
		{
			localCtx = *context;
		}
		else
		{
			RtlCaptureContext(&localCtx);
			++skipFrames; // Skip CaptureStackFrames itself
		}

		if (api.stackWalk64 && api.symFunctionTableAccess64 && api.symGetModuleBase64)
		{
			STACKFRAME64 sf = {};
#ifdef _M_X64
			const DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
			sf.AddrPC.Offset = localCtx.Rip;
			sf.AddrPC.Mode = AddrModeFlat;
			sf.AddrFrame.Offset = localCtx.Rbp;
			sf.AddrFrame.Mode = AddrModeFlat;
			sf.AddrStack.Offset = localCtx.Rsp;
			sf.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_ARM64)
			const DWORD machineType = IMAGE_FILE_MACHINE_ARM64;
			sf.AddrPC.Offset = localCtx.Pc;
			sf.AddrPC.Mode = AddrModeFlat;
			sf.AddrFrame.Offset = localCtx.Fp;
			sf.AddrFrame.Mode = AddrModeFlat;
			sf.AddrStack.Offset = localCtx.Sp;
			sf.AddrStack.Mode = AddrModeFlat;
#else
			const DWORD machineType = IMAGE_FILE_MACHINE_I386;
			sf.AddrPC.Offset = localCtx.Eip;
			sf.AddrPC.Mode = AddrModeFlat;
			sf.AddrFrame.Offset = localCtx.Ebp;
			sf.AddrFrame.Mode = AddrModeFlat;
			sf.AddrStack.Offset = localCtx.Esp;
			sf.AddrStack.Mode = AddrModeFlat;
#endif

			int walked = 0;
			const int maxWalk = maxFrames + skipFrames + 8;
			while (walked < maxWalk && static_cast<int>(frames.size()) < maxFrames)
			{
				const BOOL ok = api.stackWalk64(
					machineType,
					process,
					thread,
					&sf,
					&localCtx,
					nullptr,
					api.symFunctionTableAccess64,
					api.symGetModuleBase64,
					nullptr);
				if (!ok || sf.AddrPC.Offset == 0)
					break;

				if (walked >= skipFrames)
				{
					CrashStackFrame frame;
					ResolveFrameInfo(process, api, hasSymbols, sf.AddrPC.Offset, frame);
					frames.push_back(std::move(frame));
				}
				++walked;
			}
		}

		// Fallback if StackWalk64 wasn't available or produced no frames.
		if (frames.empty())
		{
			std::vector<void*> raw(static_cast<size_t>(maxFrames), nullptr);
			const USHORT captured = CaptureStackBackTrace(
				static_cast<DWORD>(skipFrames),
				static_cast<DWORD>(maxFrames),
				raw.data(),
				nullptr);
			for (USHORT i = 0; i < captured; ++i)
			{
				CrashStackFrame frame;
				ResolveFrameInfo(process, api, hasSymbols, reinterpret_cast<DWORD64>(raw[i]), frame);
				frames.push_back(std::move(frame));
			}
		}

		return frames;
	}

	std::string FormatStackFrame(size_t index, const CrashStackFrame& frame)
	{
		char prefix[32];
		std::snprintf(prefix, sizeof(prefix), "  [%02zu] %s  ", index, FormatHex64(frame.address).c_str());
		return std::string(prefix) + FormatLocation(frame);
	}

	std::string FormatCrashReport(
		const CrashReportData& data,
		const CrashArtifacts* artifacts)
	{
		std::ostringstream oss;
		oss << "====================================================================\n";
		oss << "FinalSun / FinalAlert 2 Mission Editor - Crash Report\n";
		oss << "====================================================================\n";
		oss << "Timestamp        : " << data.timestamp << "\n";
		oss << "Editor           : " << data.editorVariant << " (" << data.buildConfig << ")\n";
		oss << "OS               : " << data.osVersion << "\n";
		oss << "Process / Thread : PID " << data.processId << " / TID " << data.threadId << "\n";
		oss << "Crash Source     : " << data.crashSource << "\n";

		if (data.exception.code != 0 || !data.exception.name.empty())
		{
			oss << "Exception        : " << data.exception.name;
			if (data.exception.code != 0)
				oss << " (" << FormatHex32(data.exception.code) << ")";
			oss << "\n";
		}
		if (!data.exception.description.empty())
			oss << "Description      : " << data.exception.description << "\n";
		if (!data.exception.extraDetails.empty())
			oss << "Details          : " << data.exception.extraDetails << "\n";
		if (!data.cppExceptionType.empty())
			oss << "C++ Exception    : " << data.cppExceptionType << "\n";
		if (!data.cppExceptionMessage.empty())
			oss << "Exception What   : " << data.cppExceptionMessage << "\n";
		if (data.faultAddress != 0)
		{
			oss << "Fault Address    : " << FormatHex64(data.faultAddress);
			if (!data.faultLocation.empty())
				oss << " (" << data.faultLocation << ")";
			oss << "\n";
		}

		oss << "\n--- Editor State ---\n";
		oss << "Current Map      : " << (data.currentMapFile.empty() ? "<none>" : data.currentMapFile) << "\n";
		oss << "Last Operation   : " << data.lastSucceededOperation << "\n";
		oss << "Last Lib Op      : " << data.lastLibraryOperation << "\n";
		if (!data.vulkanLastError.empty())
			oss << "Vulkan Last Error: " << data.vulkanLastError << "\n";
		if (!data.appPath.empty())
			oss << "App Path         : " << data.appPath << "\n";
		if (!data.appDataPath.empty())
			oss << "AppData Path     : " << data.appDataPath << "\n";
		if (!data.logFilePath.empty())
			oss << "Log File         : " << data.logFilePath << "\n";

		if (artifacts)
		{
			oss << "\n--- Crash Artifacts ---\n";
			if (!artifacts->reportFilePath.empty())
				oss << "Crash Report     : " << WideToUtf8Safe(artifacts->reportFilePath) << "\n";
			if (!artifacts->dumpFilePath.empty())
			{
				const char* statusTag = " (FAILED)";
				if (artifacts->dumpWritten && artifacts->dumpStatus == CrashDumpStatus::Succeeded)
					statusTag = " (OK)";
				else if (artifacts->dumpStatus == CrashDumpStatus::TimedOut)
					statusTag = " (TIMED OUT / INCOMPLETE)";
				else if (artifacts->dumpStatus == CrashDumpStatus::WaitFailed)
					statusTag = " (WAIT FAILED / INCOMPLETE)";
				else if (artifacts->dumpStatus == CrashDumpStatus::NotAttempted)
					statusTag = " (NOT ATTEMPTED)";

				oss << "Minidump         : " << WideToUtf8Safe(artifacts->dumpFilePath)
					<< statusTag << "\n";
				if (!artifacts->dumpWritten && !artifacts->dumpError.empty())
					oss << "Minidump Error   : " << artifacts->dumpError << "\n";
			}
			if (artifacts->backupMapSaved && !artifacts->backupMapPath.empty())
				oss << "Backup Map       : " << WideToUtf8Safe(artifacts->backupMapPath) << " (SAVED)\n";
			else if (!artifacts->backupMapError.empty())
				oss << "Backup Map       : (NOT SAVED: " << artifacts->backupMapError << ")\n";
		}

		oss << "\n--- Call Stack (" << data.stackFrames.size() << " frames) ---\n";
		if (data.stackFrames.empty())
		{
			oss << "  <stack trace unavailable>\n";
		}
		else
		{
			for (size_t i = 0; i < data.stackFrames.size(); ++i)
			{
				oss << FormatStackFrame(i, data.stackFrames[i]) << "\n";
			}
		}

#ifdef _M_X64
		if (data.hasRegisters)
		{
			const CONTEXT& r = data.registers;
			oss << "\n--- Registers (x64) ---\n";
			oss << "  RIP=" << FormatHex64(r.Rip) << "  RSP=" << FormatHex64(r.Rsp) << "  RBP=" << FormatHex64(r.Rbp) << "\n";
			oss << "  RAX=" << FormatHex64(r.Rax) << "  RBX=" << FormatHex64(r.Rbx) << "  RCX=" << FormatHex64(r.Rcx) << "  RDX=" << FormatHex64(r.Rdx) << "\n";
			oss << "  RSI=" << FormatHex64(r.Rsi) << "  RDI=" << FormatHex64(r.Rdi) << "  R8 =" << FormatHex64(r.R8)  << "  R9 =" << FormatHex64(r.R9)  << "\n";
			oss << "  R10=" << FormatHex64(r.R10) << "  R11=" << FormatHex64(r.R11) << "  R12=" << FormatHex64(r.R12) << "  R13=" << FormatHex64(r.R13) << "\n";
			oss << "  R14=" << FormatHex64(r.R14) << "  R15=" << FormatHex64(r.R15) << "  EFL=" << FormatHex32(r.EFlags) << "\n";
		}
#endif

		return oss.str();
	}

	CrashArtifacts WriteCrashArtifacts(
		const std::wstring& crashDirectory,
		const CrashReportData& data,
		EXCEPTION_POINTERS* exceptionPointers,
		DWORD faultingThreadId)
	{
		return WriteCrashArtifactsEx(
			crashDirectory,
			data,
			exceptionPointers,
			faultingThreadId,
			MiniDumpExecutionOptions{});
	}

	CrashArtifacts WriteCrashArtifactsEx(
		const std::wstring& crashDirectory,
		const CrashReportData& data,
		EXCEPTION_POINTERS* exceptionPointers,
		DWORD faultingThreadId,
		const MiniDumpExecutionOptions& options)
	{
		CrashArtifacts artifacts;
		artifacts.crashDirectory = crashDirectory;
		CreateDirectoryRecursiveW(crashDirectory);

		if (!options.precomputedReportPath.empty() && !options.precomputedDumpPath.empty())
		{
			artifacts.reportFilePath = options.precomputedReportPath;
			artifacts.dumpFilePath = options.precomputedDumpPath;
		}
		else
		{
			BuildArtifactPaths(crashDirectory, data.processId, artifacts.reportFilePath, artifacts.dumpFilePath);
		}

		// Write an initial minimal/current text report BEFORE waiting on minidump
		// so diagnostic data is already persisted on disk even if dump generation stalls.
		artifacts.dumpStatus = CrashDumpStatus::NotAttempted;
		artifacts.reportWritten = WriteUtf8FileWithBom(
			artifacts.reportFilePath,
			FormatCrashReport(data, &artifacts));

		// Write minidump on a dedicated worker thread (recommended by Microsoft
		// and required when handling EXCEPTION_STACK_OVERFLOW). The task and deep
		// copies of exception records/context are heap-owned via shared_ptr so
		// they remain valid even if the caller stops waiting on timeout/error.
		auto task = std::make_shared<MiniDumpTask>();
		task->dumpPath = artifacts.dumpFilePath;
		task->faultingThreadId = faultingThreadId != 0 ? faultingThreadId : GetCurrentThreadId();
		task->dbgHelpLockTimeoutMs = options.dbgHelpLockTimeoutMs;
		task->customWriter = options.customWriter;
		task->customWriterContext = options.customWriterContext;
		PopulateMiniDumpExceptionSnapshot(exceptionPointers, *task);

		auto* workerParam = new std::shared_ptr<MiniDumpTask>(task);
		HANDLE worker = CreateThread(nullptr, 0, MiniDumpWorkerProc, workerParam, 0, nullptr);
		if (worker)
		{
			const DWORD waitResult = options.customWait
				? options.customWait(worker, options.timeoutMs, options.customWaitContext)
				: WaitForSingleObject(worker, options.timeoutMs);
			const DWORD waitError = (waitResult == WAIT_FAILED) ? GetLastError() : 0;
			CloseHandle(worker);

			if (waitResult == WAIT_OBJECT_0)
			{
				artifacts.dumpWritten = task->success;
				artifacts.dumpStatus = task->success ? CrashDumpStatus::Succeeded : CrashDumpStatus::Failed;
				if (!task->success)
				{
					char errBuf[64];
					std::snprintf(errBuf, sizeof(errBuf), "Win32 error %lu", static_cast<unsigned long>(task->lastError));
					artifacts.dumpError = errBuf;
				}
			}
			else if (waitResult == WAIT_TIMEOUT)
			{
				artifacts.dumpWritten = false;
				artifacts.dumpStatus = CrashDumpStatus::TimedOut;
				char errBuf[128];
				std::snprintf(
					errBuf,
					sizeof(errBuf),
					"Minidump worker timed out after %lu ms (dump incomplete)",
					static_cast<unsigned long>(options.timeoutMs));
				artifacts.dumpError = errBuf;
			}
			else
			{
				artifacts.dumpWritten = false;
				artifacts.dumpStatus = CrashDumpStatus::WaitFailed;
				char errBuf[128];
				std::snprintf(
					errBuf,
					sizeof(errBuf),
					"WaitForSingleObject failed (result=%lu, Win32 error %lu; dump incomplete)",
					static_cast<unsigned long>(waitResult),
					static_cast<unsigned long>(waitError));
				artifacts.dumpError = errBuf;
			}
		}
		else
		{
			delete workerParam;
			ExecuteMiniDumpTask(*task);
			artifacts.dumpWritten = task->success;
			artifacts.dumpStatus = task->success ? CrashDumpStatus::Succeeded : CrashDumpStatus::Failed;
			if (!task->success)
			{
				char errBuf[64];
				std::snprintf(errBuf, sizeof(errBuf), "Win32 error %lu", static_cast<unsigned long>(task->lastError));
				artifacts.dumpError = errBuf;
			}
		}

		const std::string reportUtf8 = FormatCrashReport(data, &artifacts);
		artifacts.reportWritten = WriteUtf8FileWithBom(artifacts.reportFilePath, reportUtf8);

		return artifacts;
	}

	bool ShouldUseChineseUI()
	{
		char langBuf[64] = { 0 };
		bool hasLoadedLanguage = false;
		if (SafeReadLanguageState(langBuf, sizeof(langBuf), &hasLoadedLanguage))
		{
			if (_stricmp(langBuf, "Chinese") == 0)
				return true;
			if (hasLoadedLanguage && langBuf[0] != '\0')
				return false;
		}

		return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
	}

	std::wstring BuildCrashSummaryText(
		const CrashReportData& data,
		const CrashArtifacts& artifacts,
		bool useChinese)
	{
		std::ostringstream oss;
		if (useChinese)
		{
			oss << "错误类型: " << data.crashSource;
			if (!data.exception.name.empty())
				oss << " / " << data.exception.name;
			if (data.exception.code != 0)
				oss << " (" << FormatHex32(data.exception.code) << ")";
			oss << "\n";

			if (!data.cppExceptionMessage.empty())
				oss << "异常信息: " << data.cppExceptionMessage << "\n";
			else if (!data.exception.extraDetails.empty())
				oss << "附加信息: " << data.exception.extraDetails << "\n";
			else if (!data.exception.description.empty())
				oss << "说明: " << data.exception.description << "\n";

			if (!data.faultLocation.empty())
				oss << "崩溃位置: " << data.faultLocation << "\n";
			if (!data.vulkanLastError.empty())
				oss << "Vulkan 诊断: " << data.vulkanLastError << "\n";

			oss << "操作记录: Op=" << data.lastSucceededOperation
				<< ", LibOp=" << data.lastLibraryOperation << "\n";

			if (!data.stackFrames.empty())
			{
				oss << "\n调用栈 (前 " << (data.stackFrames.size() < 6 ? data.stackFrames.size() : size_t{ 6 }) << " 层):\n";
				const size_t shown = data.stackFrames.size() < 6 ? data.stackFrames.size() : size_t{ 6 };
				for (size_t i = 0; i < shown; ++i)
				{
					oss << "  #" << i << " " << FormatLocation(data.stackFrames[i]) << "\n";
				}
			}

			oss << "\n诊断文件:\n";
			if (artifacts.reportWritten && !artifacts.reportFilePath.empty())
				oss << "  报告: " << WideToUtf8Safe(artifacts.reportFilePath) << "\n";
			if (artifacts.dumpWritten && artifacts.dumpStatus == CrashDumpStatus::Succeeded && !artifacts.dumpFilePath.empty())
				oss << "  转储: " << WideToUtf8Safe(artifacts.dumpFilePath) << "\n";
			else if (artifacts.dumpStatus == CrashDumpStatus::TimedOut)
				oss << "  转储: 未完成 (写入超时)\n";
			else if (artifacts.dumpStatus == CrashDumpStatus::WaitFailed)
				oss << "  转储: 未完成 (等待失败)\n";
			if (artifacts.backupMapSaved && !artifacts.backupMapPath.empty())
				oss << "  应急备份地图: " << WideToUtf8Safe(artifacts.backupMapPath) << "\n";
		}
		else
		{
			oss << "Crash Type: " << data.crashSource;
			if (!data.exception.name.empty())
				oss << " / " << data.exception.name;
			if (data.exception.code != 0)
				oss << " (" << FormatHex32(data.exception.code) << ")";
			oss << "\n";

			if (!data.cppExceptionMessage.empty())
				oss << "Message: " << data.cppExceptionMessage << "\n";
			else if (!data.exception.extraDetails.empty())
				oss << "Details: " << data.exception.extraDetails << "\n";
			else if (!data.exception.description.empty())
				oss << "Description: " << data.exception.description << "\n";

			if (!data.faultLocation.empty())
				oss << "Location: " << data.faultLocation << "\n";
			if (!data.vulkanLastError.empty())
				oss << "Vulkan Error: " << data.vulkanLastError << "\n";

			oss << "Last Operation: Op=" << data.lastSucceededOperation
				<< ", LibOp=" << data.lastLibraryOperation << "\n";

			if (!data.stackFrames.empty())
			{
				const size_t shown = data.stackFrames.size() < 6 ? data.stackFrames.size() : size_t{ 6 };
				oss << "\nTop Call Stack (" << shown << " frames):\n";
				for (size_t i = 0; i < shown; ++i)
				{
					oss << "  #" << i << " " << FormatLocation(data.stackFrames[i]) << "\n";
				}
			}

			oss << "\nDiagnostic Files:\n";
			if (artifacts.reportWritten && !artifacts.reportFilePath.empty())
				oss << "  Report: " << WideToUtf8Safe(artifacts.reportFilePath) << "\n";
			if (artifacts.dumpWritten && artifacts.dumpStatus == CrashDumpStatus::Succeeded && !artifacts.dumpFilePath.empty())
				oss << "  Minidump: " << WideToUtf8Safe(artifacts.dumpFilePath) << "\n";
			else if (artifacts.dumpStatus == CrashDumpStatus::TimedOut)
				oss << "  Minidump: incomplete (timed out)\n";
			else if (artifacts.dumpStatus == CrashDumpStatus::WaitFailed)
				oss << "  Minidump: incomplete (wait failed)\n";
			if (artifacts.backupMapSaved && !artifacts.backupMapPath.empty())
				oss << "  Backup Map: " << WideToUtf8Safe(artifacts.backupMapPath) << "\n";
		}

		return Utf8ToWideSafe(oss.str());
	}

#ifdef _DEBUG
	ScopedDbgHelpLockForTest::ScopedDbgHelpLockForTest()
	{
		g_dbgHelpMutex.lock();
		m_locked = true;
	}

	ScopedDbgHelpLockForTest::~ScopedDbgHelpLockForTest()
	{
		if (m_locked)
		{
			g_dbgHelpMutex.unlock();
			m_locked = false;
		}
	}

	bool RunStackOverflowTaskBoundedForTest(
		const CrashReportData& data,
		const EXCEPTION_POINTERS* exceptionPointers,
		DWORD faultingThreadId,
		DWORD diagnosticsTimeoutMs,
		void(*workerHook)(const CrashReportData&, const EXCEPTION_POINTERS*, void*),
		void* hookContext,
		bool* outTimedOut)
	{
		if (outTimedOut)
			*outTimedOut = false;

		auto task = std::make_shared<StackOverflowCrashTask>();
		task->req.data = data;
		task->req.faultingThreadId = faultingThreadId;
		task->req.allowEmergencySave = false;
		task->req.showDialog = false;
		task->req.fatal = false;
		task->diagnosticsDoneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		task->req.diagnosticsDoneEvent = task->diagnosticsDoneEvent;
		task->customWorkerHook = workerHook;
		task->customWorkerHookContext = hookContext;
		PopulateStackOverflowExceptionSnapshot(exceptionPointers, *task);

		auto* workerParam = new std::shared_ptr<StackOverflowCrashTask>(task);
		HANDLE hThread = CreateThread(nullptr, 0, StackOverflowCrashThreadProc, workerParam, 0, nullptr);
		if (!hThread)
		{
			delete workerParam;
			return false;
		}

		DWORD waitResult = WAIT_FAILED;
		if (task->diagnosticsDoneEvent)
		{
			HANDLE handles[2] = { hThread, task->diagnosticsDoneEvent };
			waitResult = WaitForMultipleObjects(2, handles, FALSE, diagnosticsTimeoutMs);
		}
		else
		{
			waitResult = WaitForSingleObject(hThread, diagnosticsTimeoutMs);
		}
		CloseHandle(hThread);

		if (waitResult == WAIT_TIMEOUT)
		{
			if (outTimedOut)
				*outTimedOut = true;
			return false;
		}
		return waitResult == WAIT_OBJECT_0 || waitResult == WAIT_OBJECT_0 + 1;
	}
#endif
}
