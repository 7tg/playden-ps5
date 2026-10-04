#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace playden {

using Bytes = std::vector<std::uint8_t>;

enum class StorageError {
  kNone,
  kInvalidPath,
  kNotFound,
  kOpenFailed,
  kReadFailed,
  kWriteFailed,
  kRenameFailed,
  kInvalidData,
  kPermissionFailed,
  kRemoveFailed,
};

template <typename T>
class Result {
 public:
  static Result Success(T value) {
    return Result(std::move(value), StorageError::kNone, std::string());
  }

  static Result Failure(StorageError error, std::string message) {
    return Result(std::nullopt, error, std::move(message));
  }

  bool ok() const { return value_.has_value(); }
  const T& value() const { return value_.value(); }
  T& value() { return value_.value(); }
  StorageError error() const { return error_; }
  const std::string& message() const { return message_; }

 private:
  Result(T value, StorageError error, std::string message)
      : value_(std::move(value)), error_(error), message_(std::move(message)) {}

  Result(std::nullopt_t, StorageError error, std::string message)
      : value_(std::nullopt), error_(error), message_(std::move(message)) {}

  std::optional<T> value_;
  StorageError error_;
  std::string message_;
};

template <>
class Result<void> {
 public:
  static Result Success() {
    return Result(true, StorageError::kNone, std::string());
  }

  static Result Failure(StorageError error, std::string message) {
    return Result(false, error, std::move(message));
  }

  bool ok() const { return ok_; }
  StorageError error() const { return error_; }
  const std::string& message() const { return message_; }

 private:
  Result(bool ok, StorageError error, std::string message)
      : ok_(ok), error_(error), message_(std::move(message)) {}

  bool ok_;
  StorageError error_;
  std::string message_;
};

class Storage {
 public:
  // The platform supplies an app-owned root. Relative path validation does not
  // defend against symlinks created by another writer inside that root.
  explicit Storage(std::string root);

  // Reads are bounded so a damaged file cannot consume unbounded memory.
  static constexpr std::size_t kMaxReadBytes = 1024 * 1024;
  Result<Bytes> Read(const std::string& relative_path,
                     std::size_t max_bytes = kMaxReadBytes) const;

  // A single app writer is assumed. Rename makes replacement atomic for
  // readers, but this does not promise persistence across sudden power loss.
  Result<void> WriteAtomic(const std::string& relative_path,
                           const Bytes& bytes) const;

  // Creates the temporary file with owner-only permissions before writing any
  // bytes. Platforms without a supported permission primitive report failure.
  Result<void> WriteAtomicPrivate(const std::string& relative_path,
                                  const Bytes& bytes) const;

  // Missing files are already removed and therefore succeed.
  Result<void> Remove(const std::string& relative_path) const;

 private:
  Result<std::string> Resolve(const std::string& relative_path) const;
  Result<void> WriteAtomicImpl(const std::string& relative_path,
                               const Bytes& bytes, bool private_file) const;

  std::string root_;
};

}  // namespace playden
