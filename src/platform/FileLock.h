// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <filesystem>
#include <string>

enum class FileLockResult {
    Acquired,
    AlreadyLocked,
    LockFileError
};

class FileLock {
public:
    FileLock() = default;
    ~FileLock();

    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

    FileLockResult tryLock(const std::filesystem::path& targetPath);
    void unlock();
    bool isLocked() const { return locked_; }
    std::string getLockHolderInfo() const;

private:
    std::filesystem::path lockFilePath_;
    bool locked_ = false;

#if defined(_WIN32)
    void* handle_ = reinterpret_cast<void*>(-1);
#else
    int fd_ = -1;
#endif
};
