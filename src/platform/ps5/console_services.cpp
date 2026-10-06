// console_services.cpp - Small PS5 console adapters for the runtime host.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include "platform/ps5/console_services.h"

#include <curl/curl.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

extern "C" int sceKernelGetSystemSwVersion(void* version);

namespace playden::ps5 {
namespace {
struct SwVersion {  // SceKernelSwVersion
  std::uint64_t size;
  char text[0x1c];
  std::uint32_t version;  // 0xMMmmrrxx, BCD digits
};
static_assert(sizeof(SwVersion) == 0x28, "SceKernelSwVersion layout");

std::size_t Receive(char* data, std::size_t size, std::size_t count, void* destination) {
  auto& body = *static_cast<std::string*>(destination);
  if (size == 0 || count > (1u << 20) / size || size * count > (1u << 20) - body.size()) return 0;
  body.append(data, size * count);
  return size * count;
}
}  // namespace

std::optional<std::string> ConsoleFirmwareVersion() {
  SwVersion version{};
  version.size = sizeof(version);
  if (sceKernelGetSystemSwVersion(&version) != 0 || version.version == 0) return std::nullopt;
  char text[8];
  std::snprintf(text, sizeof(text), "%x.%02x", unsigned(version.version >> 24), unsigned((version.version >> 16) & 0xff));
  return std::string(text);
}

std::optional<std::string> ShadowMountPost(const std::string& route, const std::string& body) {
  static std::once_flag once;
  static bool ready = false;
  std::call_once(once, [] { ready = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK; });
  if (!ready) return std::nullopt;
  CURL* easy = curl_easy_init();
  if (!easy) return std::nullopt;
  std::string response;
  curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");
  const std::string url = "http://127.0.0.1:10101" + route;
  curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
  curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(easy, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(easy, CURLOPT_PROXY, "");
  curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, 5000L);
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, Receive);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, &response);
  const auto result = curl_easy_perform(easy);
  curl_slist_free_all(headers);
  curl_easy_cleanup(easy);
  // Keep the API's error body (for example EBUSY) for the registry adapter.
  return result == CURLE_OK ? std::optional<std::string>(response) : std::nullopt;
}
}  // namespace playden::ps5
