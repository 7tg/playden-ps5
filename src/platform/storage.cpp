#include "platform/storage.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace playden {
namespace {

std::string ErrnoMessage(const std::string& action,
                         const std::string& relative_path,
                         int error_number) {
  return action + " '" + relative_path + "': " + std::strerror(error_number);
}

bool IsSafeRelativePath(const std::string& path) {
  if (path.empty() || path.front() == '/' || path.find('\\') != std::string::npos ||
      path.find('\0') != std::string::npos ||
      (path.size() >= 2 && path[1] == ':')) {
    return false;
  }

  std::size_t segment_start = 0;
  while (segment_start <= path.size()) {
    const std::size_t separator = path.find('/', segment_start);
    const std::size_t segment_end =
        separator == std::string::npos ? path.size() : separator;
    const std::string segment =
        path.substr(segment_start, segment_end - segment_start);
    if (segment.empty() || segment == "." || segment == "..") {
      return false;
    }
    if (separator == std::string::npos) {
      break;
    }
    segment_start = separator + 1;
  }
  return true;
}

}  // namespace

Storage::Storage(std::string root) : root_(std::move(root)) {
  while (root_.size() > 1 && root_.back() == '/') {
    root_.pop_back();
  }
}

Result<std::string> Storage::Resolve(const std::string& relative_path) const {
  if (root_.empty()) {
    return Result<std::string>::Failure(StorageError::kInvalidPath,
                                        "storage root is empty");
  }
  if (!IsSafeRelativePath(relative_path)) {
    return Result<std::string>::Failure(
        StorageError::kInvalidPath,
        "storage path must be a normalized relative path: '" + relative_path + "'");
  }
  return Result<std::string>::Success(root_ + "/" + relative_path);
}

Result<Bytes> Storage::Read(const std::string& relative_path, std::size_t max_bytes) const {
  if (max_bytes == 0 || max_bytes > 32 * 1024 * 1024)
    return Result<Bytes>::Failure(StorageError::kReadFailed, "invalid bounded read size");
  const auto resolved = Resolve(relative_path);
  if (!resolved.ok()) {
    return Result<Bytes>::Failure(resolved.error(), resolved.message());
  }

  errno = 0;
  std::FILE* file = std::fopen(resolved.value().c_str(), "rb");
  if (file == nullptr) {
    const int open_error = errno;
    const StorageError error =
        open_error == ENOENT ? StorageError::kNotFound : StorageError::kOpenFailed;
    return Result<Bytes>::Failure(
        error, ErrnoMessage("could not open", relative_path, open_error));
  }

  Bytes bytes;
  std::uint8_t buffer[4096];
  while (true) {
    const std::size_t count = std::fread(buffer, 1, sizeof(buffer), file);
    if (count > max_bytes - bytes.size()) {
      std::fclose(file);
      return Result<Bytes>::Failure(
          StorageError::kReadFailed,
          "could not read '" + relative_path +
              "': file exceeds the " + std::to_string(max_bytes) + "-byte limit");
    }
    bytes.insert(bytes.end(), buffer, buffer + count);
    if (count < sizeof(buffer)) {
      if (std::ferror(file) != 0) {
        const int read_error = errno;
        std::fclose(file);
        return Result<Bytes>::Failure(
            StorageError::kReadFailed,
            ErrnoMessage("could not read", relative_path, read_error));
      }
      break;
    }
  }
  std::fclose(file);
  return Result<Bytes>::Success(std::move(bytes));
}

Result<void> Storage::WriteAtomic(const std::string& relative_path,
                                  const Bytes& bytes) const {
  const auto resolved = Resolve(relative_path);
  if (!resolved.ok()) {
    return Result<void>::Failure(resolved.error(), resolved.message());
  }

  const std::string temporary_path = resolved.value() + ".tmp";
  errno = 0;
  std::FILE* file = std::fopen(temporary_path.c_str(), "wb");
  if (file == nullptr) {
    return Result<void>::Failure(
        StorageError::kOpenFailed,
        ErrnoMessage("could not open temporary file for", relative_path, errno));
  }

  if (!bytes.empty() &&
      std::fwrite(bytes.data(), 1, bytes.size(), file) != bytes.size()) {
    const int write_error = errno;
    std::fclose(file);
    std::remove(temporary_path.c_str());
    return Result<void>::Failure(
        StorageError::kWriteFailed,
        ErrnoMessage("could not write", relative_path, write_error));
  }
  if (std::fflush(file) != 0) {
    const int flush_error = errno;
    std::fclose(file);
    std::remove(temporary_path.c_str());
    return Result<void>::Failure(
        StorageError::kWriteFailed,
        ErrnoMessage("could not flush", relative_path, flush_error));
  }
  if (std::fclose(file) != 0) {
    const int close_error = errno;
    std::remove(temporary_path.c_str());
    return Result<void>::Failure(
        StorageError::kWriteFailed,
        ErrnoMessage("could not close", relative_path, close_error));
  }

  errno = 0;
  if (std::rename(temporary_path.c_str(), resolved.value().c_str()) != 0) {
    const int rename_error = errno;
    std::remove(temporary_path.c_str());
    return Result<void>::Failure(
        StorageError::kRenameFailed,
        ErrnoMessage("could not replace", relative_path, rename_error));
  }
  return Result<void>::Success();
}

}  // namespace playden
