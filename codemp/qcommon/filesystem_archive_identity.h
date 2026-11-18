// Stable identities for unchanged archive remounts. Mounted archives themselves
// are immutable, as required by the cached ZIP directory/offset implementation.
#ifndef EJK_FILESYSTEM_ARCHIVE_IDENTITY_H
#define EJK_FILESYSTEM_ARCHIVE_IDENTITY_H
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

class MountedArchiveIdentities {
public:
	using Stamp = std::array<uint64_t, 8>;
	static bool snapshot(const char *path, uint32_t directoryChecksum, Stamp &stamp) {
		stamp = {};
#ifdef _WIN32
		HANDLE file = CreateFileA(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE) return false;
		BY_HANDLE_FILE_INFORMATION info;
		FILE_BASIC_INFO basic;
		const bool valid = GetFileInformationByHandle(file, &info) &&
			GetFileInformationByHandleEx(file, FileBasicInfo, &basic, sizeof(basic));
		CloseHandle(file);
		if (!valid) return false;
		stamp[0] = info.dwVolumeSerialNumber;
		stamp[1] = (uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
		stamp[2] = (uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
		stamp[3] = (uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime;
		stamp[4] = uint64_t(basic.ChangeTime.QuadPart);
		stamp[5] = (uint64_t(info.ftCreationTime.dwHighDateTime) << 32) | info.ftCreationTime.dwLowDateTime;
#else
		struct stat info;
		if (stat(path, &info)) return false;
		stamp[0] = uint64_t(info.st_dev);
		stamp[1] = uint64_t(info.st_ino);
		stamp[2] = uint64_t(info.st_size);
#ifdef __APPLE__
		stamp[3] = uint64_t(info.st_mtimespec.tv_sec); stamp[4] = uint64_t(info.st_mtimespec.tv_nsec);
		stamp[5] = uint64_t(info.st_ctimespec.tv_sec); stamp[6] = uint64_t(info.st_ctimespec.tv_nsec);
#else
		stamp[3] = uint64_t(info.st_mtim.tv_sec); stamp[4] = uint64_t(info.st_mtim.tv_nsec);
		stamp[5] = uint64_t(info.st_ctim.tv_sec); stamp[6] = uint64_t(info.st_ctim.tv_nsec);
#endif
#endif
		stamp[7] = directoryChecksum;
		return true;
	}
	uint64_t identify(const char *path, uint32_t directoryChecksum) {
		Stamp stamp;
		if (!snapshot(path, directoryChecksum, stamp)) return 0;
		return identify(path, stamp);
	}
	uint64_t identify(const char *path, const Stamp &stamp) {
		std::string key(path);
		for (char &ch : key) {
			if (ch == '\\') ch = '/';
#ifdef _WIN32
			else if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
#endif
		}
		auto found = records.find(key);
		if (found != records.end() && found->second.stamp == stamp) return found->second.id;
		// Eviction can cause a cache miss, never reuse a retired identity.
		if (records.size() >= 4096) records.clear();
		const uint64_t id = ++nextId;
		records[key] = {stamp, id};
		return id;
	}
private:
	struct Record { Stamp stamp; uint64_t id; };
	std::unordered_map<std::string, Record> records;
	uint64_t nextId = 0;
};
#endif
