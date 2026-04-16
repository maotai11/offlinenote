// SPDX-License-Identifier: GPL-2.0-or-later

#include "FileLock.h"

#include <fstream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

std::filesystem::path makeLockPath(const std::filesystem::path& targetPath) {
    return std::filesystem::path(targetPath.string() + ".lock");
}

std::string currentProcessIdString() {
#if defined(_WIN32)
    return std::to_string(GetCurrentProcessId());
#else
    return std::to_string(static_cast<long long>(getpid()));
#endif
}

} // namespace

FileLock::~FileLock() {
    unlock();
}

FileLockResult FileLock::tryLock(const std::filesystem::path& targetPath) {
    if (targetPath.empty()) {
        return FileLockResult::LockFileError;
    }

    if (locked_) {
        return FileLockResult::AlreadyLocked;
    }

    lockFilePath_ = makeLockPath(targetPath);
    const std::string holderInfo = currentProcessIdString();

#if defined(_WIN32)
    HANDLE handle = CreateFileW(
        lockFilePath_.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS || error == ERROR_SHARING_VIOLATION) {
            return FileLockResult::AlreadyLocked;
        }
        return FileLockResult::LockFileError;
    }

    DWORD bytesWritten = 0;
    if (!WriteFile(handle, holderInfo.data(), static_cast<DWORD>(holderInfo.size()), &bytesWritten, nullptr) ||
        bytesWritten != holderInfo.size()) {
        CloseHandle(handle);
        DeleteFileW(lockFilePath_.c_str());
        handle_ = INVALID_HANDLE_VALUE;
        lockFilePath_.clear();
        return FileLockResult::LockFileError;
    }

    handle_ = handle;
#else
    const int fd = open(lockFilePath_.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd == -1) {
        if (errno == EEXIST) {
            return FileLockResult::AlreadyLocked;
        }
        return FileLockResult::LockFileError;
    }

    const ssize_t written = write(fd, holderInfo.data(), holderInfo.size());
    if (written != static_cast<ssize_t>(holderInfo.size())) {
        close(fd);
        unlink(lockFilePath_.c_str());
        lockFilePath_.clear();
        return FileLockResult::LockFileError;
    }

    fd_ = fd;
#endif

    locked_ = true;
    return FileLockResult::Acquired;
}

void FileLock::unlock() {
    if (!locked_) {
        return;
    }

#if defined(_WIN32)
    HANDLE handle = static_cast<HANDLE>(handle_);
    if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
    }
    DeleteFileW(lockFilePath_.c_str());
    handle_ = INVALID_HANDLE_VALUE;
#else
    if (fd_ >= 0) {
        close(fd_);
    }
    unlink(lockFilePath_.c_str());
    fd_ = -1;
#endif

    lockFilePath_.clear();
    locked_ = false;
}

std::string FileLock::getLockHolderInfo() const {
    if (lockFilePath_.empty()) {
        return {};
    }

    std::ifstream ifs(lockFilePath_, std::ios::binary);
    if (!ifs) {
        return {};
    }

    return std::string((std::istreambuf_iterator<char>(ifs)),
                       std::istreambuf_iterator<char>());
}
