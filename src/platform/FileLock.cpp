#include <filesystem>
#include <string>
enum class FileLockResult { Acquired, AlreadyLocked, LockFileError };
class FileLock {
public:
    ~FileLock() { unlock(); }
    FileLockResult tryLock(const std::filesystem::path&) { return FileLockResult::Acquired; }
    void unlock() {}
    bool isLocked() const { return locked_; }
    std::string getLockHolderInfo() const { return ""; }
private:
    bool locked_ = false;
    std::filesystem::path lockFilePath_;
};
