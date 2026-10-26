#include "homebrew/artwork_backend.hpp"

#include "platform/managed_directory.h"
#include "steam/backend/http_client.h"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

namespace {

struct DecodeBudget {
  std::size_t limit = 0;
  std::size_t outstanding = 0;
};

struct alignas(std::max_align_t) AllocationHeader {
  std::size_t size = 0;
  DecodeBudget* owner = nullptr;
};

thread_local DecodeBudget* g_decode_budget = nullptr;

void* ArtworkAllocate(std::size_t size) {
  DecodeBudget* budget = g_decode_budget;
  if (!budget || size > budget->limit -
                            std::min(budget->outstanding, budget->limit) ||
      size > std::numeric_limits<std::size_t>::max() - sizeof(AllocationHeader))
    return nullptr;
  auto* header = static_cast<AllocationHeader*>(
      std::malloc(sizeof(AllocationHeader) + size));
  if (!header) return nullptr;
  header->size = size;
  header->owner = budget;
  budget->outstanding += size;
  return header + 1;
}

void ArtworkFree(void* allocation) {
  if (!allocation) return;
  auto* header = static_cast<AllocationHeader*>(allocation) - 1;
  if (header->owner)
    header->owner->outstanding -=
        std::min(header->size, header->owner->outstanding);
  std::free(header);
}

void* ArtworkReallocate(void* allocation, std::size_t size) {
  if (!allocation) return ArtworkAllocate(size);
  if (size == 0) {
    ArtworkFree(allocation);
    return nullptr;
  }
  auto* old_header = static_cast<AllocationHeader*>(allocation) - 1;
  DecodeBudget* budget = old_header->owner;
  if (!budget ||
      (size > old_header->size &&
       size - old_header->size >
           budget->limit - std::min(budget->outstanding, budget->limit)) ||
      size > std::numeric_limits<std::size_t>::max() - sizeof(AllocationHeader))
    return nullptr;
  auto* header = static_cast<AllocationHeader*>(
      std::realloc(old_header, sizeof(AllocationHeader) + size));
  if (!header) return nullptr;
  if (size >= header->size) budget->outstanding += size - header->size;
  else budget->outstanding -= header->size - size;
  header->size = size;
  header->owner = budget;
  return header + 1;
}

class DecodeBudgetScope {
 public:
  explicit DecodeBudgetScope(std::size_t limit)
      : prior_(g_decode_budget), budget_{limit, 0} {
    g_decode_budget = &budget_;
  }
  ~DecodeBudgetScope() { g_decode_budget = prior_; }

 private:
  DecodeBudget* prior_;
  DecodeBudget budget_;
};

}  // namespace

#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MALLOC(size) ArtworkAllocate(size)
#define STBI_REALLOC(pointer, size) ArtworkReallocate(pointer, size)
#define STBI_FREE(pointer) ArtworkFree(pointer)
#define STB_IMAGE_IMPLEMENTATION
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb_image.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace playden::homebrew {
namespace {

constexpr std::uint32_t kArtworkTimeoutMs = 10'000;
// Cached artwork expires after 14 days so updated Steam covers refresh.
constexpr std::time_t kArtworkCacheTtlSeconds = 14 * 24 * 3600;

bool CheckedImageBytes(int width, int height, std::size_t& bytes) {
  if (width <= 0 || height <= 0 ||
      width > static_cast<int>(kMaxArtworkDimension) ||
      height > static_cast<int>(kMaxArtworkDimension)) return false;
  const std::uint64_t value = static_cast<std::uint64_t>(width) *
                              static_cast<std::uint64_t>(height) * 4u;
  if (value > kMaxArtworkImageBytes ||
      value > std::numeric_limits<std::size_t>::max()) return false;
  bytes = static_cast<std::size_t>(value);
  return true;
}

// The game's own CDN art: its cover, its library hero and its logo, and nothing else.
bool CanonicalKey(const ArtworkKey& key) {
  if (key.account_generation == 0 || !key.game_id.valid() ||
      key.source.size() > kMaxArtworkSourceBytes) return false;
  const std::string base = "https://cdn.cloudflare.steamstatic.com/steam/apps/" +
                           ToKey(key.game_id) + "/";
  for (const char* name : {"library_600x900.jpg", "library_hero.jpg", "logo.png"})
    if (key.source == base + name) return true;
  return false;
}

std::string CacheFileName(const ArtworkKey& key) {
  if (key.account_generation == 0 || !key.game_id.valid() || key.source.empty()) return {};
  const std::string id = ToKey(key.game_id);
  if (id.empty()) return {};
  if (key.source.find("library_600x900.jpg") != std::string::npos) return id + "_600x900.jpg";
  if (key.source.find("library_hero.jpg") != std::string::npos) return id + "_hero.jpg";
  if (key.source.find("logo.png") != std::string::npos) return id + "_logo.png";
  return {};
}

ArtworkImage FrameToPortrait(const ArtworkImage& src, std::uint32_t target_w = 600, std::uint32_t target_h = 900) {
  if (src.width == 0 || src.height == 0 || src.width <= src.height) return src;
  ArtworkImage out;
  out.width = target_w;
  out.height = target_h;
  out.rgba.resize(target_w * target_h * 4, 0);

  const int banner_h = static_cast<int>(target_w * src.height / src.width);
  const int offset_y = static_cast<int>((target_h - banner_h) / 2);
  const int bottom_margin = static_cast<int>(target_h) - (offset_y + banner_h);

  for (std::uint32_t y = 0; y < target_h; ++y) {
    for (std::uint32_t x = 0; x < target_w; ++x) {
      const std::size_t out_idx = (y * target_w + x) * 4;
      if (static_cast<int>(y) >= offset_y && static_cast<int>(y) < offset_y + banner_h) {
        const std::uint32_t sx = std::min(x * src.width / target_w, src.width - 1);
        const std::uint32_t sy = std::min((y - offset_y) * src.height / banner_h, src.height - 1);
        const std::size_t in_idx = (sy * src.width + sx) * 4;
        out.rgba[out_idx] = src.rgba[in_idx];
        out.rgba[out_idx + 1] = src.rgba[in_idx + 1];
        out.rgba[out_idx + 2] = src.rgba[in_idx + 2];
        out.rgba[out_idx + 3] = 255;
      } else {
        const std::uint32_t sx = std::min(x * src.width / target_w, src.width - 1);
        const std::uint32_t sy = (static_cast<int>(y) < offset_y) ? 0 : (src.height - 1);
        const std::size_t in_idx = (sy * src.width + sx) * 4;
        const float factor = (static_cast<int>(y) < offset_y)
            ? (offset_y > 0 ? (0.4f * static_cast<float>(y) / static_cast<float>(offset_y)) : 0.0f)
            : (bottom_margin > 0 ? (0.4f * static_cast<float>(target_h - 1 - y) / static_cast<float>(bottom_margin)) : 0.0f);
        out.rgba[out_idx] = static_cast<std::uint8_t>(src.rgba[in_idx] * factor);
        out.rgba[out_idx + 1] = static_cast<std::uint8_t>(src.rgba[in_idx + 1] * factor);
        out.rgba[out_idx + 2] = static_cast<std::uint8_t>(src.rgba[in_idx + 2] * factor);
        out.rgba[out_idx + 3] = 255;
      }
    }
  }
  return out;
}

}  // namespace

struct HttpArtworkBackend::Cache {
  std::mutex mutex;
  std::optional<platform::ManagedDirectory> dir;

  bool EnsureDir(const std::string& path) {
    if (dir && dir->Validate()) return true;
    if (path.empty()) return false;
    const std::filesystem::path target = std::filesystem::path(path).lexically_normal();
    dir = platform::ManagedDirectory::Open(target, platform::DirectoryPolicy::kPrivate);
    if (dir) return true;

    const auto parent_path = target.parent_path();
    const auto child_name = target.filename().string();
    auto parent = platform::ManagedDirectory::Open(parent_path, platform::DirectoryPolicy::kApprovedTree);
    if (!parent) parent = platform::ManagedDirectory::Open(parent_path, platform::DirectoryPolicy::kPrivateTraversable);
    if (!parent) parent = platform::ManagedDirectory::Open(parent_path, platform::DirectoryPolicy::kPrivate);
    if (parent) {
      dir = parent->OpenChild(child_name, platform::DirectoryPolicy::kPrivate, /*create=*/true, 0700);
      if (dir) return true;
    }
    return false;
  }
};

std::optional<ArtworkImage> artwork_internal::DecodeArtworkWithAllocationBudget(
    const std::vector<std::uint8_t>& encoded, std::size_t allocation_limit) {
  if (encoded.empty() || encoded.size() > kMaxArtworkTransferBytes ||
      encoded.size() > static_cast<std::size_t>(INT_MAX) || allocation_limit == 0 ||
      allocation_limit > kMaxArtworkDecodeAllocationBytes) return std::nullopt;
  DecodeBudgetScope budget(allocation_limit);

  int width = 0;
  int height = 0;
  int components = 0;
  if (!stbi_info_from_memory(encoded.data(), static_cast<int>(encoded.size()),
                             &width, &height, &components)) return std::nullopt;
  std::size_t bytes = 0;
  if (!CheckedImageBytes(width, height, bytes)) return std::nullopt;

  int decoded_width = 0;
  int decoded_height = 0;
  int decoded_components = 0;
  stbi_uc* decoded = stbi_load_from_memory(
      encoded.data(), static_cast<int>(encoded.size()), &decoded_width,
      &decoded_height, &decoded_components, 4);
  if (!decoded) return std::nullopt;
  std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(decoded,
                                                              stbi_image_free);
  std::size_t decoded_bytes = 0;
  if (decoded_width != width || decoded_height != height ||
      !CheckedImageBytes(decoded_width, decoded_height, decoded_bytes) ||
      decoded_bytes != bytes) return std::nullopt;

  ArtworkImage image;
  image.width = static_cast<std::uint32_t>(decoded_width);
  image.height = static_cast<std::uint32_t>(decoded_height);
  image.rgba.assign(pixels.get(), pixels.get() + decoded_bytes);
  return image;
}

std::optional<ArtworkImage> DecodeArtwork(
    const std::vector<std::uint8_t>& encoded) {
  return artwork_internal::DecodeArtworkWithAllocationBudget(
      encoded, kMaxArtworkDecodeAllocationBytes);
}

HttpArtworkBackend::HttpArtworkBackend(std::string ca_bundle, std::string cache_dir)
    : ca_bundle_(std::move(ca_bundle)),
      cache_dir_(std::move(cache_dir)),
      http_(std::make_unique<steam::HttpClient>(ca_bundle_)),
      cache_(cache_dir_.empty() ? nullptr : std::make_unique<Cache>()) {}

HttpArtworkBackend::~HttpArtworkBackend() = default;

std::optional<ArtworkImage> HttpArtworkBackend::Load(
    const ArtworkKey& key, const std::atomic<bool>& cancel) {
  if (!CanonicalKey(key) || cancel.load(std::memory_order_acquire))
    return std::nullopt;
  if (!http_) http_ = std::make_unique<steam::HttpClient>(ca_bundle_);

  const std::string file_name = CacheFileName(key);
  bool cache_expired = false;

  // 1. Check persistent on-disk cache under ManagedDirectory
  if (!file_name.empty() && cache_) {
    std::lock_guard<std::mutex> lock(cache_->mutex);
    if (cache_->EnsureDir(cache_dir_)) {
      struct stat st{};
      if (cache_->dir->InspectChild(file_name, &st) && S_ISREG(st.st_mode) &&
          st.st_size > 0 && static_cast<std::size_t>(st.st_size) <= kMaxArtworkTransferBytes) {
        const std::time_t now = std::time(nullptr);
        cache_expired = (now >= st.st_mtime && (now - st.st_mtime) >= kArtworkCacheTtlSeconds);
        if (!cache_expired) {
          const int fd = cache_->dir->OpenRegularFile(file_name);
          if (fd >= 0) {
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(st.st_size));
            std::size_t offset = 0;
            bool read_ok = true;
            while (offset < bytes.size()) {
              const auto count = read(fd, bytes.data() + offset, bytes.size() - offset);
              if (count < 0 && errno == EINTR) continue;
              if (count <= 0) { read_ok = false; break; }
              offset += static_cast<std::size_t>(count);
            }
            close(fd);
            if (read_ok && offset == bytes.size()) {
              auto image = DecodeArtwork(bytes);
              if (image) {
                if (key.source.find("library_600x900.jpg") != std::string::npos &&
                    image->width > image->height) {
                  image = FrameToPortrait(*image);
                }
                return image;
              }
            }
          }
        }
      }
    }
  }

  if (cancel.load(std::memory_order_acquire)) return std::nullopt;

  // 2. Fetch from Steam CDN
  steam::HttpRequest request;
  request.method = steam::HttpMethod::kGet;
  request.url = key.source;
  request.timeout_ms = kArtworkTimeoutMs;
  request.max_response_bytes = kMaxArtworkTransferBytes;
  auto response = http_->Perform(request, cancel);

  // If library_600x900.jpg returned 404, fallback to header.jpg (standard Steam banner)
  if ((!response.ok() || response.status_code != 200) &&
      key.source.find("library_600x900.jpg") != std::string::npos &&
      !cancel.load(std::memory_order_acquire)) {
    const std::string base = "https://cdn.cloudflare.steamstatic.com/steam/apps/" +
                             ToKey(key.game_id) + "/";
    steam::HttpRequest fallback_req = request;
    fallback_req.url = base + "header.jpg";
    auto fallback_resp = http_->Perform(fallback_req, cancel);
    if (fallback_resp.ok() && fallback_resp.status_code == 200) {
      response = std::move(fallback_resp);
    }
  }

  if (!response.ok() || response.status_code != 200) {
    // Offline / network failure fallback to existing cached image if expired
    if (cache_expired && !file_name.empty() && cache_) {
      std::lock_guard<std::mutex> lock(cache_->mutex);
      if (cache_->EnsureDir(cache_dir_)) {
        struct stat st{};
        if (cache_->dir->InspectChild(file_name, &st) && st.st_size > 0 &&
            static_cast<std::size_t>(st.st_size) <= kMaxArtworkTransferBytes) {
          const int fd = cache_->dir->OpenRegularFile(file_name);
          if (fd >= 0) {
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(st.st_size));
            std::size_t offset = 0;
            bool read_ok = true;
            while (offset < bytes.size()) {
              const auto count = read(fd, bytes.data() + offset, bytes.size() - offset);
              if (count < 0 && errno == EINTR) continue;
              if (count <= 0) { read_ok = false; break; }
              offset += static_cast<std::size_t>(count);
            }
            close(fd);
            if (read_ok && offset == bytes.size()) {
              auto img = DecodeArtwork(bytes);
              if (img) {
                if (key.source.find("library_600x900.jpg") != std::string::npos &&
                    img->width > img->height) {
                  img = FrameToPortrait(*img);
                }
                return img;
              }
            }
          }
        }
      }
    }
    return std::nullopt;
  }

  // 3. Atomically write downloaded cover to cache via ManagedDirectory
  if (!file_name.empty() && cache_ && !response.body.empty() &&
      response.body.size() <= kMaxArtworkTransferBytes) {
    std::lock_guard<std::mutex> lock(cache_->mutex);
    if (cache_->EnsureDir(cache_dir_)) {
      static std::atomic<std::uint64_t> sequence{0};
      const std::string temp_name = "." + file_name + "." + std::to_string(getpid()) + "." +
                                    std::to_string(sequence.fetch_add(1)) + ".tmp";
      const int fd = cache_->dir->CreateExclusiveFile(temp_name, 0600);
      if (fd >= 0) {
        std::size_t offset = 0;
        bool write_ok = true;
        while (offset < response.body.size()) {
          const auto count = write(fd, response.body.data() + offset, response.body.size() - offset);
          if (count < 0 && errno == EINTR) continue;
          if (count <= 0) { write_ok = false; break; }
          offset += static_cast<std::size_t>(count);
        }
        const bool synced = write_ok && (fsync(fd) == 0);
        const bool closed = (close(fd) == 0);
        if (synced && closed) {
          cache_->dir->RenameChild(temp_name, *cache_->dir, file_name, /*replace_existing=*/true);
          cache_->dir->Sync();
        } else {
          if (!closed) close(fd);
          cache_->dir->RemoveRegularFile(temp_name);
        }
      }
    }
  }

  auto image = DecodeArtwork(response.body);
  if (image && key.source.find("library_600x900.jpg") != std::string::npos && image->width > image->height) {
    image = FrameToPortrait(*image);
  }
  return image;
}

}  // namespace playden::homebrew
