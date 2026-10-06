#include "platform/ps5/title_probe_contract.h"
#include <cstdio>
#include <cstdlib>
#include <string>
using playden::ps5::ValidateBootstrapProbeSource;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
int main() {
  const std::string owner = R"({"schema":1,"install_id":"acceptance-hl2-fresh-v16","title_id":"FAKE78001","purpose":"isolated bootstrap acceptance"})";
  const std::string descriptor = R"({"schema_version":1,"install_id":"acceptance-hl2-fresh-v16","title_id":"FAKE78001","app_id":220,"data_root":"/data/playden/games/acceptance-hl2-fresh-v16","default_profile_id":"game"})";
  const std::string param = R"({"titleId":"FAKE78001","contentId":"UP9000-FAKE78001_00-PROSPEROWIN00001"})";
  CHECK(ValidateBootstrapProbeSource(owner, descriptor, param));
  const auto changed = [](std::string text, const std::string& before, const std::string& after) {
    text.replace(text.find(before), before.size(), after); return text;
  };
  CHECK(!ValidateBootstrapProbeSource(changed(owner,"FAKE78001","FAKE78002"), descriptor, param));
  CHECK(!ValidateBootstrapProbeSource(changed(owner,"isolated bootstrap acceptance","unrelated fixture"), descriptor, param));
  CHECK(!ValidateBootstrapProbeSource(owner, changed(descriptor,"fresh-v16","fresh-v17"), param));
  CHECK(!ValidateBootstrapProbeSource(owner, changed(descriptor,"/data/playden/games/","/data/playden/games/../"), param));
  CHECK(!ValidateBootstrapProbeSource(owner, changed(descriptor,"220","221"), param));
  CHECK(!ValidateBootstrapProbeSource(owner, changed(descriptor,"\"game\"","\"other\""), param));
  CHECK(!ValidateBootstrapProbeSource(owner, descriptor, changed(param,"FAKE78001","FAKE78002")));
  CHECK(!ValidateBootstrapProbeSource(owner, descriptor, changed(param,"PROSPEROWIN00001","UNRELATED000001")));
  CHECK(!ValidateBootstrapProbeSource("{}", descriptor, param));
  CHECK(!ValidateBootstrapProbeSource(owner, "not JSON", param));
  CHECK(!ValidateBootstrapProbeSource(owner, changed(descriptor,"\"app_id\":220","\"app_id\":220,\"app_id\":221"), param));
  CHECK(!ValidateBootstrapProbeSource(owner + std::string(65536, ' '), descriptor, param));
}
