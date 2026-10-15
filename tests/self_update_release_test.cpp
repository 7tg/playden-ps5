#include "update/release.h"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace playden::update;
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #value); std::exit(1); } } while(false)

static const std::string kSha(64, 'a');

static std::string Manifest(const std::string& app_override = {}, const std::string& kit_override = {}) {
  const std::string app = app_override.empty()
      ? R"({"version":"0.3.0","title_id":"FAKE77031","min_from":"0.2.0","asset":"playden-ps5-0.3.0.zip","prefix":"playden-ps5-0.3.0/FAKE77031/"})"
      : app_override;
  const std::string kit = kit_override.empty()
      ? R"({"kit_id":"hl2-fw1360-dev1","revision":"r9","manifest_sha256":")" + kSha +
            R"(","asset":"playden-kit-h-r9.zip","prefix":"playden-kit-h-r9/hl2-fw1360-dev1/"})"
      : kit_override;
  return R"({"format":1,"app":)" + app + R"(,"kit":)" + kit + R"(,"notes":"Faster downloads."})";
}

int main() {
  // A GitHub release as the API returns it (trimmed).
  const std::string release_json = R"({"tag_name":"v0.3.0","html_url":"https://github.com/o/r/releases/tag/v0.3.0","draft":false,"prerelease":false,"body":"x","assets":[
    {"name":"playden-update.json","size":512,"browser_download_url":"https://github.com/o/r/releases/download/v0.3.0/playden-update.json","digest":"sha256:)" +
      kSha + R"("},
    {"name":"playden-ps5-0.3.0.zip","size":48848304,"browser_download_url":"https://github.com/o/r/releases/download/v0.3.0/playden-ps5-0.3.0.zip","digest":null},
    {"name":"broken","browser_download_url":"https://x"}]})";
  std::string error;
  const auto release = ParseRelease(release_json, &error);
  CHECK(release);
  CHECK(release->tag == "v0.3.0" && release->version == (Version{0, 3, 0}) && !release->draft && !release->prerelease);
  CHECK(release->page == "https://github.com/o/r/releases/tag/v0.3.0");
  CHECK(ParseRelease(R"({"tag_name":"v0.3.0","html_url":"https://evil.example/x","assets":[]})", &error)->page.empty());
  CHECK(release->assets.size() == 2);
  CHECK(release->Find("playden-update.json")->sha256 == kSha);
  CHECK(release->Find("playden-ps5-0.3.0.zip")->sha256.empty());
  CHECK(release->Find("playden-ps5-0.3.0.zip")->size == 48848304);
  CHECK(!release->Find("broken"));
  CHECK(ParseRelease(R"({"tag_name":"v0.3.0","prerelease":true,"assets":[]})", &error)->prerelease);
  CHECK(!ParseRelease(R"({"tag_name":"nightly","assets":[]})", &error));
  CHECK(!ParseRelease(R"({"tag_name":"v0.3.0"})", &error));
  CHECK(!ParseRelease("[]", &error));
  CHECK(!ParseRelease("not json", &error));

  const auto manifest = ParseUpdateManifest(Manifest(), &error);
  CHECK(manifest);
  CHECK(manifest->app_version == (Version{0, 3, 0}) && manifest->min_from == (Version{0, 2, 0}));
  CHECK(manifest->title_id == "FAKE77031" && manifest->app_asset == "playden-ps5-0.3.0.zip");
  CHECK(manifest->app_prefix == "playden-ps5-0.3.0/FAKE77031/");
  CHECK(manifest->kit.kit_id == "hl2-fw1360-dev1" && manifest->kit.revision == "r9" && manifest->kit.manifest_sha256 == kSha);
  CHECK(manifest->kit.prefix == "playden-kit-h-r9/hl2-fw1360-dev1/" && manifest->notes == "Faster downloads.");

  // The notes are a plain-text summary: GitHub's line endings and control characters go, blank lines
  // collapse to one, and a long text is cut at a word with an ellipsis.
  auto notes_of = [&](const std::string& notes) {
    std::string text = Manifest();
    text.replace(text.find("Faster downloads."), 17, notes);
    const auto parsed = ParseUpdateManifest(text, &error);
    return parsed ? parsed->notes : std::string("(refused)");
  };
  CHECK(notes_of(R"(Faster downloads.\r\n\r\n\r\n\r\n  Fixes.\t\u0001 \r\n)") == "Faster downloads.\n\nFixes.");
  std::string words;
  while (words.size() < 900) words += "caf\u00e9 ";
  const std::string cut = notes_of(words);
  CHECK(cut.size() <= 603 && cut.size() > 300 && cut.substr(cut.size() - 3) == "\xE2\x80\xA6");
  CHECK(cut.substr(cut.size() - 8, 5) == "caf\xC3\xA9");  // a whole word, not half a character

  // Unsafe or incomplete manifests are refused.
  CHECK(!ParseUpdateManifest(R"({"format":2})", &error));
  std::string text = Manifest();
  CHECK(!ParseUpdateManifest(text.replace(text.find("\"format\":1"), 10, "\"format\":2"), &error));
  const char* bad_apps[] = {
      R"({"version":"0.3","title_id":"FAKE77031","min_from":"0.2.0","asset":"a.zip","prefix":"a/b/"})",
      R"({"version":"0.3.0","title_id":"../x","min_from":"0.2.0","asset":"a.zip","prefix":"a/b/"})",
      R"({"version":"0.3.0","title_id":"FAKE77031","min_from":"0.2.0","asset":"a.zip","prefix":"../b/"})",
      R"({"version":"0.3.0","title_id":"FAKE77031","min_from":"0.2.0","asset":"a.zip","prefix":"a/b"})",
      R"({"version":"0.3.0","title_id":"FAKE77031","min_from":"0.2.0","asset":"a.zip"})"};
  for (const char* app : bad_apps) CHECK(!ParseUpdateManifest(Manifest(app), &error));
  const std::string bad_kits[] = {
      R"({"kit_id":"../k","revision":"r9","manifest_sha256":")" + kSha + R"(","asset":"k.zip","prefix":"a/b/"})",
      R"({"kit_id":"k","revision":".hidden","manifest_sha256":")" + kSha + R"(","asset":"k.zip","prefix":"a/b/"})",
      R"({"kit_id":"k","revision":"r9","manifest_sha256":"ABC","asset":"k.zip","prefix":"a/b/"})",
      R"({"kit_id":"k","revision":"r9","manifest_sha256":")" + kSha + R"(","asset":"k.zip"})"};
  for (const auto& kit : bad_kits) CHECK(!ParseUpdateManifest(Manifest({}, kit), &error));

  CHECK(IsSha256(kSha) && !IsSha256(std::string(63, 'a')) && !IsSha256(std::string(64, 'A')));
  CHECK(SafeFolderName("hl2-fw1360-dev1-r9") && !SafeFolderName("a/b") && !SafeFolderName("..") && !SafeFolderName(""));
  std::puts("ok");
  return 0;
}
