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
#include "MapSaveUtil.h"
#include "IniFile.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>

namespace MapSaveUtil
{
	namespace
	{
		std::atomic<uint64_t> g_tempFileCounter{ 0 };
		std::atomic<bool> g_mapSaveInProgress{ false };
		std::timed_mutex g_snapshotMutex;
		std::shared_ptr<const CommittedMapSnapshot> g_committedSnapshot;

		bool WriteAllBytesToHandle(HANDLE file, std::string_view bytes, DWORD* outWin32Error)
		{
			size_t totalWritten = 0;
			constexpr size_t kMaxChunk = 16u * 1024u * 1024u;
			while (totalWritten < bytes.size())
			{
				const size_t remaining = bytes.size() - totalWritten;
				const DWORD toWrite = static_cast<DWORD>(remaining > kMaxChunk ? kMaxChunk : remaining);
				DWORD written = 0;
				if (!WriteFile(file, bytes.data() + totalWritten, toWrite, &written, nullptr) || written == 0)
				{
					if (outWin32Error)
					{
						const DWORD err = GetLastError();
						*outWin32Error = (err != 0) ? err : ERROR_WRITE_FAULT;
					}
					return false;
				}
				totalWritten += written;
			}
			return true;
		}

		bool CommitTempFileDefault(
			const std::filesystem::path& tempPath,
			const std::filesystem::path& destPath,
			bool allowOverwrite,
			DWORD* outWin32Error)
		{
			if (!allowOverwrite)
			{
				if (MoveFileExW(tempPath.c_str(), destPath.c_str(), MOVEFILE_WRITE_THROUGH))
					return true;
				if (outWin32Error)
					*outWin32Error = GetLastError();
				return false;
			}

			const DWORD destAttrs = GetFileAttributesW(destPath.c_str());
			if (destAttrs != INVALID_FILE_ATTRIBUTES)
			{
				if ((destAttrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
				{
					if (outWin32Error)
						*outWin32Error = ERROR_ACCESS_DENIED;
					return false;
				}

				if (ReplaceFileW(
					destPath.c_str(),
					tempPath.c_str(),
					nullptr,
					REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS,
					nullptr,
					nullptr))
				{
					return true;
				}

				const DWORD replaceErr = GetLastError();
				if (replaceErr != ERROR_FILE_NOT_FOUND && replaceErr != ERROR_NOT_SUPPORTED)
				{
					if (outWin32Error)
						*outWin32Error = replaceErr;
					return false;
				}
			}

			if (MoveFileExW(tempPath.c_str(), destPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
				return true;

			if (outWin32Error)
				*outWin32Error = GetLastError();
			return false;
		}

		bool CreateDirectoryRecursiveW(const std::wstring& dirPath)
		{
			if (dirPath.empty())
				return false;

			const DWORD attrs = GetFileAttributesW(dirPath.c_str());
			if (attrs != INVALID_FILE_ATTRIBUTES)
				return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;

			std::error_code ec;
			if (std::filesystem::create_directories(std::filesystem::path(dirPath), ec) && !ec)
				return true;

			const DWORD afterAttrs = GetFileAttributesW(dirPath.c_str());
			return afterAttrs != INVALID_FILE_ATTRIBUTES && (afterAttrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
		}

		std::wstring SanitizeBackupExtension(const std::wstring& ext)
		{
			if (_wcsicmp(ext.c_str(), L".yrm") == 0)
				return L".yrm";
			if (_wcsicmp(ext.c_str(), L".mpr") == 0)
				return L".mpr";
			return L".map";
		}

		void AppendSectionText(
			std::string& out,
			const CString& sectionName,
			const CIniFileSection& section,
			const std::function<void(const CString&, int)>& onProgress)
		{
			out += '[';
			out.append(sectionName.GetString(), sectionName.GetLength());
			out += "]\n";

			const size_t totalEntries = section.values.size();
			size_t entryIndex = 0;
			for (const auto& entry : section.values)
			{
				out.append(entry.first.GetString(), entry.first.GetLength());
				out += '=';
				out.append(entry.second.GetString(), entry.second.GetLength());
				out += '\n';

				if (onProgress && totalEntries > 0 && (entryIndex % 500 == 0))
				{
					const int percent = static_cast<int>((entryIndex * 100) / totalEntries);
					onProgress(sectionName, percent);
				}
				++entryIndex;
			}
			out += '\n';
		}

		void AppendNamedSectionIfPresent(std::string& out, const CIniFile& ini, const char* name)
		{
			out += '[';
			out += name;
			out += "]\n";
			const auto it = ini.sections.find(name);
			if (it != ini.sections.end())
			{
				for (const auto& entry : it->second.values)
				{
					out.append(entry.first.GetString(), entry.first.GetLength());
					out += '=';
					out.append(entry.second.GetString(), entry.second.GetLength());
					out += '\n';
				}
			}
			out += '\n';
		}
	}

	std::filesystem::path ResolveMapOutputPath(
		const std::filesystem::path& requestedPath,
		bool isMultiplayer,
		bool isYRMap,
		bool isRA2Mode)
	{
		if (requestedPath.empty() || !isRA2Mode || !isMultiplayer)
			return requestedPath;

		const std::wstring ext = requestedPath.extension().wstring();
		if (_wcsicmp(ext.c_str(), L".mmx") == 0)
			return requestedPath;

		std::filesystem::path resolved = requestedPath;
		if (_wcsicmp(ext.c_str(), L".map") == 0)
		{
			resolved.replace_extension(isYRMap ? L".yrm" : L".mpr");
		}
		else if (_wcsicmp(ext.c_str(), L".mpr") == 0 && isYRMap)
		{
			resolved.replace_extension(L".yrm");
		}
		return resolved;
	}

	bool WriteBytesAtomically(
		const std::filesystem::path& destPath,
		std::string_view bytes,
		bool allowOverwrite,
		DWORD* outWin32Error,
		const AtomicWriteHooks* hooks)
	{
		if (outWin32Error)
			*outWin32Error = 0;

		if (destPath.empty() || !destPath.has_filename())
		{
			if (outWin32Error)
				*outWin32Error = ERROR_INVALID_NAME;
			return false;
		}

		const std::filesystem::path parentDir = destPath.has_parent_path()
			? destPath.parent_path()
			: std::filesystem::path(L".");
		const DWORD parentAttrs = GetFileAttributesW(parentDir.c_str());
		if (parentAttrs == INVALID_FILE_ATTRIBUTES || (parentAttrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
		{
			if (outWin32Error)
				*outWin32Error = ERROR_PATH_NOT_FOUND;
			return false;
		}

		const std::wstring baseName = destPath.filename().wstring();
		const DWORD pid = GetCurrentProcessId();
		const DWORD tid = GetCurrentThreadId();

		HANDLE file = INVALID_HANDLE_VALUE;
		std::filesystem::path tempPath;
		DWORD lastErr = 0;

		for (int attempt = 0; attempt < 32; ++attempt)
		{
			const uint64_t seq = g_tempFileCounter.fetch_add(1, std::memory_order_relaxed) + 1;
			wchar_t suffix[96];
			std::swprintf(
				suffix,
				_countof(suffix),
				L".%lu.%lu.%llu.tmp",
				static_cast<unsigned long>(pid),
				static_cast<unsigned long>(tid),
				static_cast<unsigned long long>(seq));
			tempPath = parentDir / (baseName + suffix);

			file = CreateFileW(
				tempPath.c_str(),
				GENERIC_WRITE,
				0,
				nullptr,
				CREATE_NEW,
				FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN,
				nullptr);
			if (file != INVALID_HANDLE_VALUE)
				break;

			lastErr = GetLastError();
			if (lastErr != ERROR_FILE_EXISTS && lastErr != ERROR_ALREADY_EXISTS)
			{
				if (outWin32Error)
					*outWin32Error = lastErr;
				return false;
			}
		}

		if (file == INVALID_HANDLE_VALUE)
		{
			if (outWin32Error)
				*outWin32Error = lastErr != 0 ? lastErr : ERROR_FILE_EXISTS;
			return false;
		}

		DWORD ioErr = 0;
		const bool wroteAll = (hooks && hooks->writeFn)
			? hooks->writeFn(file, bytes, &ioErr, hooks->writeContext)
			: WriteAllBytesToHandle(file, bytes, &ioErr);

		bool flushed = false;
		if (wroteAll)
		{
			flushed = (FlushFileBuffers(file) != FALSE);
			if (!flushed)
				ioErr = GetLastError();
		}

		const bool closed = (CloseHandle(file) != FALSE);
		if (wroteAll && flushed && !closed)
			ioErr = GetLastError();

		if (!wroteAll || !flushed || !closed)
		{
			DeleteFileW(tempPath.c_str());
			if (outWin32Error)
				*outWin32Error = ioErr != 0 ? ioErr : ERROR_WRITE_FAULT;
			return false;
		}

		DWORD commitErr = 0;
		const bool committed = (hooks && hooks->commitFn)
			? hooks->commitFn(tempPath, destPath, allowOverwrite, &commitErr, hooks->commitContext)
			: CommitTempFileDefault(tempPath, destPath, allowOverwrite, &commitErr);
		if (!committed)
		{
			DeleteFileW(tempPath.c_str());
			if (outWin32Error)
				*outWin32Error = commitErr != 0 ? commitErr : ERROR_ACCESS_DENIED;
			return false;
		}

		return true;
	}

	std::string SerializeMapIniToUtf8(
		const CIniFile& ini,
		const std::function<bool(const CString&)>& isRulesSection,
		bool hidePreview,
		bool isTSMode,
		const std::function<void(const CString&, int)>& onProgress)
	{
		size_t estimatedSize = 4096;
		for (const auto& section : ini.sections)
		{
			estimatedSize += static_cast<size_t>(section.first.GetLength()) + 4 + section.second.values.size() * 64;
		}

		std::string out;
		out.reserve(estimatedSize);

		if (isTSMode)
			out += "; Map created with FinalSun Mission Editor\n";
		else
			out += "; Map created with FinalAlert 2 Mission Editor\n";
		out += "; note that all comments were truncated\n";
		out += "; https://github.com/ahasasjeb/ra2map\n";
		out += "; Encoding=UTF-8\n";

		std::map<CString, BOOL> writtenSpecialSections;

		// Header at top
		for (const auto& section : ini.sections)
		{
			if (section.first == "Header")
			{
				writtenSpecialSections[section.first] = TRUE;
				AppendSectionText(out, section.first, section.second, onProgress);
			}
		}

		// Rules sections next
		for (const auto& section : ini.sections)
		{
			if (isRulesSection && isRulesSection(section.first))
			{
				writtenSpecialSections[section.first] = TRUE;
				AppendSectionText(out, section.first, section.second, onProgress);
			}
		}

		// Preview before map data
		if (hidePreview)
		{
			out += "; this is a hidden preview\n";
			out += "[Preview]\n";
			out += "Size=0,0,106,61\n\n";
			out += "[PreviewPack]\n";
			out += "2=BIACcgAEwBtAMnRABAAaQCSANMAVQASAAnIABMAbQDJ0QAQAGkAkgDTAFUAEgAJyAATAG0\n";
			out += "1=yAsAIAXQ5PDQ5PDQ6JQATAEE6PDQ4PDI4JgBTAFEAkgAJyAATAG0AydEAEABpAJIA0wBVA\n\n";
		}
		else
		{
			AppendNamedSectionIfPresent(out, ini, "Preview");
			AppendNamedSectionIfPresent(out, ini, "PreviewPack");
		}

		// Remaining sections
		for (const auto& section : ini.sections)
		{
			if (writtenSpecialSections.find(section.first) != writtenSpecialSections.end() ||
				section.first == "Digest" || section.first == "PreviewPack" ||
				section.first == "Preview" || section.first == "Header" ||
				section.first.IsEmpty())
			{
				continue;
			}
			AppendSectionText(out, section.first, section.second, onProgress);
		}

		out += '\n';
		AppendNamedSectionIfPresent(out, ini, "Digest");

		return out;
	}

	ScopedMapSaveGuard::ScopedMapSaveGuard()
	{
		bool expected = false;
		m_acquired = g_mapSaveInProgress.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
	}

	ScopedMapSaveGuard::~ScopedMapSaveGuard()
	{
		if (m_acquired)
		{
			g_mapSaveInProgress.store(false, std::memory_order_release);
		}
	}

	bool IsMapSaveInProgress()
	{
		return g_mapSaveInProgress.load(std::memory_order_acquire);
	}

	void PublishCommittedMapSnapshot(
		std::string serializedUtf8,
		std::wstring extension,
		std::wstring sourcePath)
	{
		auto snapshot = std::make_shared<CommittedMapSnapshot>();
		snapshot->serializedUtf8 = std::move(serializedUtf8);
		snapshot->extension = SanitizeBackupExtension(extension);
		snapshot->sourcePath = std::move(sourcePath);

		std::lock_guard<std::timed_mutex> lock(g_snapshotMutex);
		g_committedSnapshot = std::move(snapshot);
	}

	void ClearCommittedMapSnapshot()
	{
		std::lock_guard<std::timed_mutex> lock(g_snapshotMutex);
		g_committedSnapshot.reset();
	}

	std::shared_ptr<const CommittedMapSnapshot> TryGetCommittedMapSnapshot()
	{
		std::unique_lock<std::timed_mutex> lock(g_snapshotMutex, std::try_to_lock);
		if (!lock.owns_lock())
			return nullptr;
		return g_committedSnapshot;
	}

	EmergencyBackupResult WriteEmergencyMapBackup(
		const std::wstring& backupDirectory,
		const std::wstring& fileStamp,
		DWORD processId,
		const AtomicWriteHooks* hooks)
	{
		EmergencyBackupResult result;

		const auto snapshot = TryGetCommittedMapSnapshot();
		if (!snapshot || snapshot->serializedUtf8.empty())
		{
			result.status = EmergencyBackupStatus::NoSnapshotAvailable;
			result.error = "No committed map snapshot available; existing autosaves preserved";
			return result;
		}

		if (backupDirectory.empty() || !CreateDirectoryRecursiveW(backupDirectory))
		{
			result.status = EmergencyBackupStatus::WriteFailed;
			result.error = "Backup directory is invalid or not writable";
			return result;
		}

		const std::wstring ext = SanitizeBackupExtension(snapshot->extension);
		const std::wstring stamp = fileStamp.empty() ? L"unknown" : fileStamp;
		const DWORD pid = processId != 0 ? processId : GetCurrentProcessId();
		const std::filesystem::path dirPath(backupDirectory);

		DWORD lastErr = 0;
		for (unsigned int suffixIdx = 0; suffixIdx < 32; ++suffixIdx)
		{
			wchar_t nameBuf[192];
			if (suffixIdx == 0)
			{
				std::swprintf(
					nameBuf,
					_countof(nameBuf),
					L"fcrash_backup_%ls_pid%lu%ls",
					stamp.c_str(),
					static_cast<unsigned long>(pid),
					ext.c_str());
			}
			else
			{
				std::swprintf(
					nameBuf,
					_countof(nameBuf),
					L"fcrash_backup_%ls_pid%lu_%u%ls",
					stamp.c_str(),
					static_cast<unsigned long>(pid),
					suffixIdx,
					ext.c_str());
			}

			const std::filesystem::path candidate = dirPath / nameBuf;
			if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
				continue;

			if (WriteBytesAtomically(candidate, snapshot->serializedUtf8, /*allowOverwrite=*/false, &lastErr, hooks))
			{
				result.status = EmergencyBackupStatus::Saved;
				result.actualPath = candidate.wstring();
				return result;
			}

			if (lastErr != ERROR_FILE_EXISTS && lastErr != ERROR_ALREADY_EXISTS)
				break;
		}

		result.status = EmergencyBackupStatus::WriteFailed;
		char errBuf[96];
		std::snprintf(
			errBuf,
			sizeof(errBuf),
			"Failed to write emergency backup (Win32 error %lu)",
			static_cast<unsigned long>(lastErr));
		result.error = errBuf;
		return result;
	}
}
