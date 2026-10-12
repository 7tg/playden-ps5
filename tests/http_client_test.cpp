#include "steam/backend/http_client.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

using playden::steam::HttpClient;
using playden::steam::HttpError;
using playden::steam::HttpMethod;
using playden::steam::HttpRequest;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

std::string Text(const std::vector<std::uint8_t>& bytes) {
  return std::string(bytes.begin(), bytes.end());
}

}  // namespace

int main(int argc, char** argv) {
  Check(argc == 4, "expected BASE_URL CA_BUNDLE WRONG_CA_BUNDLE");
  const std::string base_url = argv[1];
  const std::string ca_bundle = argv[2];
  const std::string wrong_ca_bundle = argv[3];
  std::atomic<bool> cancel{false};
  HttpClient client(ca_bundle);

  HttpRequest get;
  get.url = base_url + "/ok";
  auto response = client.Perform(get, cancel);
  Check(response.ok(), "verified GET should succeed");
  Check(response.status_code == 200, "GET should retain HTTP status");
  Check(Text(response.body) == "fixture-ok", "GET should retain response bytes");
  bool found_result = false;
  for (const auto& header : response.headers)
    found_result |= header.first == "X-ErEsUlT" && header.second == "42";
  Check(found_result, "response should retain bounded header names, casing, and values");

  HttpRequest post;
  post.method = HttpMethod::kPost;
  post.url = base_url + "/echo";
  post.headers.emplace_back("X-Playden-Test", "present");
  post.body = {'p', 'a', 'y', 'l', 'o', 'a', 'd'};
  response = client.Perform(post, cancel);
  Check(response.ok() && response.status_code == 201, "verified POST should succeed");
  Check(Text(response.body) == "present:payload", "POST should send headers and bytes");

  HttpRequest timeout;
  timeout.url = base_url + "/slow";
  timeout.timeout_ms = 100;
  auto started = std::chrono::steady_clock::now();
  response = client.Perform(timeout, cancel);
  Check(response.error == HttpError::kTimeout, "slow response should time out");
  Check(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(500),
        "timeout should be enforced near its deadline");

  HttpRequest canceled = timeout;
  canceled.timeout_ms = 3000;
  cancel.store(false);
  std::thread canceler([&cancel] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cancel.store(true);
  });
  started = std::chrono::steady_clock::now();
  response = client.Perform(canceled, cancel);
  canceler.join();
  Check(response.error == HttpError::kCanceled, "cancel flag should abort active request");
  Check(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(500),
        "cancellation should interrupt the multi loop promptly");
  cancel.store(false);

  HttpRequest capped;
  capped.url = base_url + "/large";
  capped.max_response_bytes = 64;
  response = client.Perform(capped, cancel);
  Check(response.error == HttpError::kResponseTooLarge, "response cap should fail closed");
  Check(response.body.empty(), "oversized response bytes should not escape");

  HttpRequest header_capped;
  header_capped.url = base_url + "/large-headers";
  response = client.Perform(header_capped, cancel);
  Check(response.error == HttpError::kResponseHeadersTooLarge,
        "response headers should have an independent count/byte cap");
  Check(response.headers.empty(), "oversized headers should not escape");

  HttpRequest malformed;
  malformed.url = "http://localhost/not-https";
  response = client.Perform(malformed, cancel);
  Check(response.error == HttpError::kInvalidRequest, "non-HTTPS URL should be rejected");
  malformed.url = base_url + std::string("/ok\0ignored", 11);
  response = client.Perform(malformed, cancel);
  Check(response.error == HttpError::kInvalidRequest, "embedded NUL in URL should be rejected");

  HttpRequest redirect;
  redirect.url = base_url + "/redirect";
  response = client.Perform(redirect, cancel);
  Check(response.ok() && response.status_code == 302,
        "redirect should be returned without being followed");

  HttpRequest invalid_bounds = get;
  invalid_bounds.timeout_ms = playden::steam::kMaxHttpTimeoutMs + 1;
  response = client.Perform(invalid_bounds, cancel);
  Check(response.error == HttpError::kInvalidRequest, "timeout should have a hard upper bound");

  HttpRequest invalid_get_body = get;
  invalid_get_body.body = {'x'};
  response = client.Perform(invalid_get_body, cancel);
  Check(response.error == HttpError::kInvalidRequest, "GET body should be rejected explicitly");

  HttpClient untrusted(wrong_ca_bundle);
  response = untrusted.Perform(get, cancel);
  Check(response.error == HttpError::kTlsVerificationFailed, "wrong CA should fail verification");

  HttpRequest invalid_header = get;
  invalid_header.headers.emplace_back("Bad\r\nInjected", "value");
  response = client.Perform(invalid_header, cancel);
  Check(response.error == HttpError::kInvalidRequest, "header injection should be rejected");
  invalid_header = get;
  invalid_header.headers.emplace_back(std::string("Bad\0Name", 8), "value");
  response = client.Perform(invalid_header, cancel);
  Check(response.error == HttpError::kInvalidRequest,
        "embedded NUL in header name should be rejected");
  Check(std::string(playden::steam::HttpErrorName(HttpError::kCanceled)) == "canceled",
        "error names should be stable and safe for diagnostics");

  return 0;
}
