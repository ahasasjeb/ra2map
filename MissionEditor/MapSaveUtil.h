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
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

class CIniFile;

namespace MapSaveUtil
{
	struct AtomicWriteHooks
	{
		// Optional override for writing bytes to an open Win32 handle.
		// Returns true only if all bytes were written.
		using WriteFn = bool(*)(HANDLE file, std::string_view bytes, DWORD* outWin32Error, void* userContext);
		// Optional override for committing tempPath -> destPath.
		using CommitFn = bool(*)(const std::filesystem::path& tempPath, const std::filesystem::path& destPath, bool allowOverwrite, DWORD* outWin32Error, void* userContext);

		WriteFn writeFn = nullptr;
		void* writeContext = nullptr;
		CommitFn commitFn = nullptr;
		void* commitContext = nullptr;
	};

	// Resolves only the final file extension for RA2/YR multiplayer maps without
	// mutating directory segments or filename stems.
	std::filesystem::path ResolveMapOutputPath(
		const std::filesystem::path& requestedPath,
		bool isMultiplayer,
		bool isYRMap,
		bool isRA2Mode);

	// Writes all bytes to a unique same-volume temporary file in destPath.parent_path(),
	// flushes and closes the handle with full error checking, then commits to destPath.
	// On any error, cleans up the temporary file and leaves any existing destPath untouched.
	bool WriteBytesAtomically(
		const std::filesystem::path& destPath,
		std::string_view bytes,
		bool allowOverwrite,
		DWORD* outWin32Error = nullptr,
		const AtomicWriteHooks* hooks = nullptr);

	// Serializes a map INI to UTF-8 text without mutating the CIniFile instance.
	std::string SerializeMapIniToUtf8(
		const CIniFile& ini,
		const std::function<bool(const CString&)>& isRulesSection,
		bool hidePreview,
		bool isTSMode,
		const std::function<void(const CString&, int)>& onProgress = nullptr);

	// Non-reentrant guard protecting live map save operations.
	class ScopedMapSaveGuard
	{
	public:
		ScopedMapSaveGuard();
		~ScopedMapSaveGuard();

		ScopedMapSaveGuard(const ScopedMapSaveGuard&) = delete;
		ScopedMapSaveGuard& operator=(const ScopedMapSaveGuard&) = delete;

		bool IsAcquired() const { return m_acquired; }

	private:
		bool m_acquired = false;
	};

	bool IsMapSaveInProgress();

	struct CommittedMapSnapshot
	{
		std::string serializedUtf8;
		std::wstring extension;
		std::wstring sourcePath;
	};

	// Stores an immutable serialized snapshot of the last cleanly committed map save.
	void PublishCommittedMapSnapshot(
		std::string serializedUtf8,
		std::wstring extension,
		std::wstring sourcePath);

	void ClearCommittedMapSnapshot();

	// Non-blocking snapshot retrieval safe for crash-handler threads.
	std::shared_ptr<const CommittedMapSnapshot> TryGetCommittedMapSnapshot();

	enum class EmergencyBackupStatus
	{
		NotAttempted,
		Saved,
		NoSnapshotAvailable,
		WriteFailed,
	};

	struct EmergencyBackupResult
	{
		EmergencyBackupStatus status = EmergencyBackupStatus::NotAttempted;
		std::wstring actualPath;
		std::string error;
	};

	// Non-UI, non-reentrant emergency backup writer. Uses only the last committed
	// immutable snapshot, writes to a unique filename with the snapshot's actual
	// extension, never overwrites existing backups, and never shows dialogs.
	EmergencyBackupResult WriteEmergencyMapBackup(
		const std::wstring& backupDirectory,
		const std::wstring& fileStamp,
		DWORD processId,
		const AtomicWriteHooks* hooks = nullptr);

	// Computes a stable 16-hex-character identifier for a map's canonical source path
	// (case- and separator-normalized on Windows) so same-named maps in different
	// directories do not collide in shared autosave folders.
	std::wstring ComputeMapSourceId(const std::filesystem::path& sourcePath);

	struct AutoSavePathPlan
	{
		std::wstring sourceId;
		std::wstring versionedPrefix;
		std::filesystem::path destination;
	};

	AutoSavePathPlan BuildAutoSavePlan(
		const std::filesystem::path& sourcePath,
		const std::filesystem::path& directory,
		bool versioned,
		const SYSTEMTIME* nowOverride = nullptr);

	// Rotates versioned autosaves strictly matching versionedPrefix + timestamp + extension,
	// leaving other maps' backups and legacy-format backups untouched.
	void PruneVersionedAutoSaves(
		const std::filesystem::path& directory,
		const std::wstring& versionedPrefix,
		const std::wstring& extension,
		size_t maxCount,
		const std::filesystem::path& protectedPath = {});
}
