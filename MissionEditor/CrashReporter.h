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

#pragma once

#include <Windows.h>
#include <string>
#include <vector>

class CException;

struct CrashExceptionSummary
{
	DWORD code = 0;
	std::string name;
	std::string description;
	std::string extraDetails;
};

struct CrashStackFrame
{
	DWORD64 address = 0;
	std::string moduleName;
	DWORD64 moduleOffset = 0;
	std::string symbolName;
	DWORD64 symbolDisplacement = 0;
	std::string sourceFile;
	DWORD sourceLine = 0;
};

struct CrashReportData
{
	std::string timestamp;
	std::string editorVariant;
	std::string buildConfig;
	std::string osVersion;
	DWORD processId = 0;
	DWORD threadId = 0;

	std::string crashSource;
	CrashExceptionSummary exception;
	DWORD64 faultAddress = 0;
	std::string faultLocation;

	std::string cppExceptionType;
	std::string cppExceptionMessage;

	int lastSucceededOperation = 0;
	int lastLibraryOperation = 0;
	std::string currentMapFile;
	std::string appPath;
	std::string appDataPath;
	std::string logFilePath;
	std::string vulkanLastError;

	bool hasRegisters = false;
	CONTEXT registers = {};

	std::vector<CrashStackFrame> stackFrames;
};

enum class CrashDumpStatus
{
	NotAttempted,
	Succeeded,
	Failed,
	TimedOut,
	WaitFailed,
};

struct CrashArtifacts
{
	std::wstring crashDirectory;
	std::wstring reportFilePath;
	std::wstring dumpFilePath;
	std::wstring backupMapPath;
	bool reportWritten = false;
	bool dumpWritten = false;
	CrashDumpStatus dumpStatus = CrashDumpStatus::NotAttempted;
	bool backupMapSaved = false;
	std::string backupMapError;
	std::string dumpError;
};

namespace CrashReporter
{
	struct MiniDumpExecutionOptions
	{
		DWORD timeoutMs = 15000;
		DWORD dbgHelpLockTimeoutMs = 2000;
		std::wstring precomputedReportPath;
		std::wstring precomputedDumpPath;
		using WriterFn = bool(*)(
			const std::wstring& dumpPath,
			EXCEPTION_POINTERS* exceptionPointers,
			DWORD faultingThreadId,
			DWORD* outLastError,
			void* userContext);
		using WaitFn = DWORD(*)(HANDLE handle, DWORD timeoutMs, void* userContext);
		WriterFn customWriter = nullptr;
		void* customWriterContext = nullptr;
		WaitFn customWait = nullptr;
		void* customWaitContext = nullptr;
	};
	// Installs process-wide crash handlers (Win32 SEH unhandled exception filter,
	// std::terminate, CRT invalid parameter, pure virtual call, SIGABRT, and thread
	// stack guarantee). Safe to call multiple times to re-assert the filter after
	// third-party DLLs load.
	void Install();

	// Reports an uncaught C++ exception at a top-level boundary (e.g. InitInstance/Run),
	// writes crash report + minidump, attempts emergency map backup, and shows the crash dialog.
	void ReportUnhandledCppException(
		const char* source,
		const char* exceptionType,
		const char* message,
		bool fatal = true);

	// Reports an MFC CException caught in CWinApp::ProcessWndProcException.
	void ReportMfcException(CException* e, const MSG* pMsg);

	// Testable pure/diagnostic helpers.
	CrashExceptionSummary DescribeExceptionCode(
		DWORD code,
		const ULONG_PTR* exceptionInfo = nullptr,
		DWORD numberParameters = 0);

	std::vector<CrashStackFrame> CaptureStackFrames(
		const CONTEXT* context = nullptr,
		int skipFrames = 0,
		int maxFrames = 64);

	std::vector<CrashStackFrame> CaptureStackFramesBounded(
		const CONTEXT* context,
		int skipFrames,
		int maxFrames,
		DWORD lockTimeoutMs,
		bool* outSymbolized = nullptr);

	std::string FormatStackFrame(size_t index, const CrashStackFrame& frame);

	std::string FormatCrashReport(
		const CrashReportData& data,
		const CrashArtifacts* artifacts = nullptr);

	CrashArtifacts WriteCrashArtifacts(
		const std::wstring& crashDirectory,
		const CrashReportData& data,
		EXCEPTION_POINTERS* exceptionPointers = nullptr,
		DWORD faultingThreadId = 0);

	CrashArtifacts WriteCrashArtifactsEx(
		const std::wstring& crashDirectory,
		const CrashReportData& data,
		EXCEPTION_POINTERS* exceptionPointers,
		DWORD faultingThreadId,
		const MiniDumpExecutionOptions& options);

	bool ShouldUseChineseUI();

	std::wstring BuildCrashSummaryText(
		const CrashReportData& data,
		const CrashArtifacts& artifacts,
		bool useChinese);

#ifdef _DEBUG
	class ScopedDbgHelpLockForTest
	{
	public:
		ScopedDbgHelpLockForTest();
		~ScopedDbgHelpLockForTest();

		ScopedDbgHelpLockForTest(const ScopedDbgHelpLockForTest&) = delete;
		ScopedDbgHelpLockForTest& operator=(const ScopedDbgHelpLockForTest&) = delete;

		bool IsLocked() const { return m_locked; }

	private:
		bool m_locked = false;
	};

	bool RunStackOverflowTaskBoundedForTest(
		const CrashReportData& data,
		const EXCEPTION_POINTERS* exceptionPointers,
		DWORD faultingThreadId,
		DWORD diagnosticsTimeoutMs,
		void(*workerHook)(const CrashReportData&, const EXCEPTION_POINTERS*, void*),
		void* hookContext,
		bool* outTimedOut);
#endif
}
