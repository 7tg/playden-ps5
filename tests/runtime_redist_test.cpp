#include "runtime/burn_bundle.h"
#include "runtime/install_script.h"
#include "runtime/msi_package.h"
#include "runtime/redist_install.h"

#include "msi_fixture.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>

#define CHECK(cond)                                                                     \
  do {                                                                                  \
    if (!(cond)) {                                                                      \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);     \
      std::exit(1);                                                                     \
    }                                                                                   \
  } while (0)

using namespace playden::runtime;
using namespace playden::test::msi;

namespace {

// A package like NVIDIA's legacy PhysX one: a 32-bit loader in Vendor\Common (added to PATH),
// an engine in Vendor\Engine\v1 found through a registry value, a 64-bit feature turned on by
// VersionNT64, a feature left off, and files in the two-block cabinet above.
enum class Variant { kNormal, kWrongSize, kUncompressed };

std::string SamplePackage(Variant variant = Variant::kNormal) {
  std::vector<TableSpec> tables;
  tables.push_back({"Property", {{"Property", 's'}, {"Value", 's'}},
                    {{"ProductCode", "{11111111-2222-3333-4444-555555555555}"}, {"ProductName", "Sample Runtime"},
                     {"ProductVersion", "1.2.3"}, {"P_VERSION", "7"}, {"D_FIXED", "C:\\Fixed\\Place\\"}}});
  tables.push_back({"Directory", {{"Directory", 's'}, {"Directory_Parent", 's'}, {"DefaultDir", 's'}},
                    {{"TARGETDIR", "", "SourceDir"}, {"ProgramFilesFolder", "TARGETDIR", "."},
                     {"D_VENDOR", "ProgramFilesFolder", "vendor~1|Vendor Runtime"}, {"D_COMMON", "D_VENDOR", "Common"},
                     {"D_ENGINE", "D_VENDOR", "Engine"}, {"D_V1", "D_ENGINE", "v1~1|v1"},
                     {"D_DESKTOP", "DesktopFolder", "Sample"}, {"D_FIXED", "ProgramFilesFolder", "Wrong"}}});
  tables.push_back({"Feature", {{"Feature", 's'}, {"Feature_Parent", 's'}, {"Level", 'i'}},
                    {{"F_Main", "", "1"}, {"F_64", "", "0"}, {"F_Off", "", "0"}, {"F_Child", "F_Off", "1"}}});
  tables.push_back({"Condition", {{"Feature_", 's'}, {"Level", 'i'}, {"Condition", 's'}},
                    {{"F_64", "1", "VersionNT64"}, {"F_Off", "1", "NOT VersionNT64"}}});
  tables.push_back({"Component",
                    {{"Component", 's'}, {"ComponentId", 's'}, {"Directory_", 's'}, {"Attributes", 'i'}, {"Condition", 's'}, {"KeyPath", 's'}},
                    {{"C_LOADER", "{A}", "D_COMMON", "0", "", "F_LOADER"},
                     {"C_ENGINE", "{B}", "D_V1", "0", "VersionNT >= 501", "F_ENGINE"},
                     {"C_REG", "{C}", "D_COMMON", "4", "", ""},
                     {"C_REG64", "{D}", "D_COMMON", "260", "", ""},
                     {"C_ENV", "{E}", "D_COMMON", "0", "", ""},
                     {"C_OFF", "{F}", "D_COMMON", "0", "", "F_OFF"},
                     {"C_SHORTCUT", "{G}", "D_DESKTOP", "0", "", "F_EMPTY"}}});
  tables.push_back({"FeatureComponents", {{"Feature_", 's'}, {"Component_", 's'}},
                    {{"F_Main", "C_LOADER"}, {"F_Main", "C_ENGINE"}, {"F_Main", "C_REG"}, {"F_64", "C_REG64"},
                     {"F_Main", "C_ENV"}, {"F_Child", "C_OFF"}, {"F_Main", "C_SHORTCUT"}}});
  tables.push_back({"File",
                    {{"File", 's'}, {"Component_", 's'}, {"FileName", 's'}, {"FileSize", 'l'}, {"Version", 's'},
                     {"Language", 's'}, {"Attributes", 'i'}, {"Sequence", 'i'}},
                    {{"first", "C_LOADER", "loader~1.dll|Loader.dll", variant == Variant::kWrongSize ? "9" : "10", "", "",
                      variant == Variant::kUncompressed ? "8192" : "512", "1"},
                     {"second", "C_ENGINE", "Core.dll", "10", "", "", "512", "2"},
                     {"empty", "C_ENGINE", "Empty.dat", "0", "", "", "512", "3"},
                     {"off", "C_OFF", "Off.dll", "10", "", "", "512", "4"},
                     {"link", "C_SHORTCUT", "Link.txt", "0", "", "", "512", "5"}}});
  tables.push_back({"Media", {{"DiskId", 'i'}, {"LastSequence", 'i'}, {"DiskPrompt", 's'}, {"Cabinet", 's'}},
                    {{"1", "5", "", "#data.cab"}}});
  tables.push_back({"Registry",
                    {{"Registry", 's'}, {"Root", 'i'}, {"Key", 's'}, {"Name", 's'}, {"Value", 's'}, {"Component_", 's'}},
                    {{"r1", "2", "SOFTWARE\\Vendor", "Core Path", "[D_ENGINE]", "C_REG"},
                     {"r1b", "2", "SOFTWARE\\Vendor", "Core Path Short", "[D_VENDOR]Engine", "C_REG"},
                     {"r2", "2", "SOFTWARE\\Vendor", "Version", "#[P_VERSION]", "C_REG"},
                     {"r3", "2", "SOFTWARE\\Vendor", "Build", "#1", "C_REG64"},
                     {"r4", "2", "SOFTWARE\\Vendor", "+", "", "C_REG"},
                     {"r5", "2", "SOFTWARE\\Vendor", "Blob", "#x0102", "C_REG"},
                     {"r6", "2", "SOFTWARE\\Vendor\\Engines", "1.0", "#5", "C_ENGINE"},
                     {"r7", "1", "Software\\Vendor", "User", "[#first]", "C_REG"},
                     {"r8", "2", "SOFTWARE\\Vendor", "Hidden", "x", "C_OFF"},
                     {"r9", "2", "SOFTWARE\\Vendor", "Fixed", "[D_FIXED]", "C_REG"},
                     {"r10", "2", "SOFTWARE\\Vendor", "Bad\"Name", "x", "C_REG"}}});
  tables.push_back({"Environment", {{"Environment", 's'}, {"Name", 's'}, {"Value", 's'}, {"Component_", 's'}},
                    {{"e1", "=-*PATH", "[D_COMMON];[~]", "C_ENV"}, {"e2", "=-*OTHER", "x", "C_ENV"}}});
  tables.push_back({"CustomAction", {{"Action", 's'}, {"Type", 'i'}, {"Source", 's'}, {"Target", 's'}},
                    {{"CA1", "1", "Binary1", "Run"}}});
  return Package(tables, {{"data.cab", TwoBlockCabinet()}});
}

const RegistryValue* FindValue(const std::vector<RegistryValue>& values, const std::string& key, const std::string& name) {
  for (const auto& value : values)
    if (value.key == key && value.name == name) return &value;
  return nullptr;
}

std::string ReadText(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void WriteText(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

void TestConditions() {
  const std::map<std::string, std::string> properties = {{"VersionNT", "601"}, {"VersionNT64", "601"}, {"Name", "Abc"}};
  CHECK(EvaluateMsiCondition("", properties) == true);
  CHECK(EvaluateMsiCondition("VersionNT64", properties) == true);
  CHECK(EvaluateMsiCondition("NOT VersionNT64", properties) == false);
  CHECK(EvaluateMsiCondition("Installed", properties) == false);
  CHECK(EvaluateMsiCondition("VersionNT >= 501 AND (Missing OR VersionNT64)", properties) == true);
  CHECK(EvaluateMsiCondition("VersionNT < 600 OR Name = \"Abc\"", properties) == true);
  CHECK(EvaluateMsiCondition("Name ~= \"abc\"", properties) == true);
  CHECK(EvaluateMsiCondition("Name = \"abc\"", properties) == false);
  CHECK(EvaluateMsiCondition("Name >< \"b\" AND Name << \"A\" AND Name >> \"c\"", properties) == true);
  CHECK(EvaluateMsiCondition("&Feature = 3", properties) == false);
  CHECK(!EvaluateMsiCondition("VersionNT >", properties).has_value());
  CHECK(!EvaluateMsiCondition("(VersionNT", properties).has_value());
  // A property set to "0" is set; an integer against missing text compares false (but <>).
  CHECK(EvaluateMsiCondition("Zero", {{"Zero", "0"}}) == true);
  CHECK(EvaluateMsiCondition("0", properties) == false);
  CHECK(EvaluateMsiCondition("Missing < 1", properties) == false);
  CHECK(EvaluateMsiCondition("Missing <> 1", properties) == true);
  CHECK(EvaluateMsiCondition("Name = 1", properties) == false);
  // Deep nesting is refused rather than recursing without bound.
  std::string deep;
  for (int index = 0; index < 1000; ++index) deep += "NOT ";
  CHECK(!EvaluateMsiCondition(deep + "VersionNT", properties).has_value());
  CHECK(!EvaluateMsiCondition(std::string(1000, '(') + "VersionNT" + std::string(1000, ')'), properties).has_value());
  CHECK(EvaluateMsiCondition("((NOT (VersionNT = 0)))", properties) == true);
}

void TestCabinet() {
  std::map<std::string, std::string> members;
  std::vector<std::string> order;
  std::string reason;
  CHECK(ExtractCabinet(TwoBlockCabinet(), [&](const std::string& name, std::string_view data) {
    members[name] = std::string(data);
    order.push_back(name);
    return true;
  }, &reason));
  CHECK(members["first"] == "0123456789");
  CHECK(members["second"] == "0123456789");  // copied from the previous block's history
  CHECK(members.count("empty") && members["empty"].empty());
  CHECK((order == std::vector<std::string>{"first", "second", "empty"}));
  // LZX is refused by name; a sink that stops ends extraction.
  auto lzx = TwoBlockCabinet();
  lzx[36 + 6] = 3;
  CHECK(!ExtractCabinet(lzx, [](const std::string&, std::string_view) { return true; }, &reason));
  CHECK(reason == "LZX cabinets are not supported");
  CHECK(!ExtractCabinet(TwoBlockCabinet(), [](const std::string&, std::string_view) { return false; }, &reason));
  CHECK(!ExtractCabinet("MSCF", [](const std::string&, std::string_view) { return true; }, &reason));
  // Members that overlap are refused: "second" moved to offset 5.
  auto overlapping = TwoBlockCabinet();
  const auto second = overlapping.find("second") - 16 + 4;
  overlapping[second] = 5;
  CHECK(!ExtractCabinet(overlapping, [](const std::string&, std::string_view) { return true; }, &reason));
  CHECK(reason == "damaged cabinet: members overlap");
}

void TestPlan() {
  MsiInstallPlan plan;
  std::string reason;
  CHECK(!ReadMsiPackage("not a package", &plan, &reason));
  CHECK(ReadMsiPackage(SamplePackage(), &plan, &reason));
  CHECK(plan.product_code == "{11111111-2222-3333-4444-555555555555}" && plan.product_name == "Sample Runtime");
  CHECK(plan.custom_actions == 1);
  CHECK(plan.cabinets.size() == 1 && plan.cabinets[0].name == "data.cab" && plan.cabinets[0].embedded);
  // The off feature's file and the desktop file stay out; the rest go under Program Files (x86).
  CHECK(plan.files.size() == 3);
  CHECK(plan.files[0].target == "Program Files (x86)/Vendor Runtime/Common/Loader.dll" && plan.files[0].member == "first");
  CHECK(plan.files[1].target == "Program Files (x86)/Vendor Runtime/Engine/v1/Core.dll");
  CHECK(plan.files[2].target == "Program Files (x86)/Vendor Runtime/Engine/v1/Empty.dat");
  CHECK(plan.skipped_files == 1);  // Link.txt, under the per-user desktop
  const auto* path = FindValue(plan.machine, "Software\\Wow6432Node\\Vendor", "Core Path");
  CHECK(path && path->data == "\"C:\\\\Program Files (x86)\\\\Vendor Runtime\\\\Engine\\\\\"");
  const auto* short_path = FindValue(plan.machine, "Software\\Wow6432Node\\Vendor", "Core Path Short");
  CHECK(short_path && short_path->data == "\"C:\\\\Program Files (x86)\\\\Vendor Runtime\\\\Engine\"");
  const auto* version = FindValue(plan.machine, "Software\\Wow6432Node\\Vendor", "Version");
  CHECK(version && version->data == "dword:00000007");
  const auto* build = FindValue(plan.machine, "SOFTWARE\\Vendor", "Build");  // a 64-bit component
  CHECK(build && build->data == "dword:00000001");
  CHECK(FindValue(plan.machine, "Software\\Wow6432Node\\Vendor\\Engines", "1.0"));
  CHECK(!FindValue(plan.machine, "Software\\Wow6432Node\\Vendor", "Hidden"));
  CHECK(!FindValue(plan.machine, "Software\\Wow6432Node\\Vendor", "Blob"));
  const auto* user = FindValue(plan.user, "Software\\Vendor", "User");
  CHECK(user && user->data == "\"C:\\\\Program Files (x86)\\\\Vendor Runtime\\\\Common\\\\Loader.dll\"");
  // A directory the Property table sets keeps that place.
  const auto* fixed = FindValue(plan.machine, "Software\\Wow6432Node\\Vendor", "Fixed");
  CHECK(fixed && fixed->data == "\"C:\\\\Fixed\\\\Place\\\\\"");
  CHECK(plan.unsupported_files == 0);
  CHECK(plan.skipped_registry == 3);  // the binary value, the quoted name and the OTHER variable
  CHECK((plan.path_prepend == std::vector<std::string>{"C:\\Program Files (x86)\\Vendor Runtime\\Common\\"}));
  CHECK(plan.path_append.empty());
}

void TestBurnBundle() {
  std::string reason;
  for (const bool x64 : {true, false})
    for (const auto& [signed_form, with_section] : {std::pair{true, true}, std::pair{false, true}, std::pair{true, false}}) {
      BurnMsiPackage package;
      const auto id = std::string("vcRuntimeAdditional_") + (x64 ? "x64" : "x86");
      const bool ok = ReadBurnMsiPackage(MfcBundle(x64, signed_form, with_section), id, &package, &reason);
      if (!ok) std::fprintf(stderr, "bundle: %s\n", reason.c_str());
      CHECK(ok);
      CHECK(package.msi_name == std::string("vc_runtimeAdditional_") + (x64 ? "x64" : "x86") + ".msi");
      CHECK(package.msi == MfcPackage(x64));
      CHECK(package.beside.size() == 1 && package.beside.at("cab1.cab") == MfcCabinet(x64));
      // The other package of the chain, by its own payloads.
      CHECK(ReadBurnMsiPackage(MfcBundle(x64, signed_form, with_section), std::string("vcRuntimeMinimum_") + (x64 ? "x64" : "x86"),
                               &package, &reason));
      CHECK(package.msi == "minimum-msi" && package.beside.at("cab1.cab") == StoredCabinet({{"vcruntime140.dll", "CRT"}}));
    }
  BurnMsiPackage package;
  CHECK(!ReadBurnMsiPackage(MfcBundle(true), "vcRuntimeAdditional_arm64", &package, &reason));
  CHECK(reason == "the bundle has no package vcRuntimeAdditional_arm64");
  CHECK(!ReadBurnMsiPackage("MZ not a bundle", "x", &package, &reason));
  CHECK(reason == "not a setup bundle Playden can read");
  // A payload that isn't the size the manifest lists is refused.
  auto bundle = MfcBundle(true);
  const auto listed = "FileSize=\"" + std::to_string(MfcCabinet(true).size()) + "\"";
  // The manifest is stored, so its text is in the bundle as is; same length keeps the cabinet valid.
  auto wrong = std::to_string(MfcCabinet(true).size() + 1);
  wrong.resize(std::to_string(MfcCabinet(true).size()).size(), '9');
  const auto at = bundle.find(listed);
  CHECK(at != std::string::npos);
  bundle.replace(at, listed.size(), "FileSize=\"" + wrong + "\"");
  CHECK(!ReadBurnMsiPackage(bundle, "vcRuntimeAdditional_x64", &package, &reason));
  CHECK(reason == "the bundle's copy of cab1.cab is not the size its manifest lists");
  // A cut bundle has no attached container to read.
  const auto whole = MfcBundle(true);
  CHECK(!ReadBurnMsiPackage(whole.substr(0, whole.size() - 200), "vcRuntimeAdditional_x64", &package, &reason));
}

void TestPath() {
  CHECK(ExtendPath("A;B", {"C:\\X"}, {}) == "C:\\X;A;B");
  CHECK(ExtendPath("A;c:\\x\\", {"C:\\X"}, {"D"}) == "A;c:\\x\\;D");
  CHECK(ExtendPath("", {}, {"D"}) == "D");
}

void TestScripts() {
  const std::string script =
      "\"InstallScript\"\n{\n\t\"Run Process\"\n\t{\n\t\t\"PhysX\"\n\t\t{\n"
      "\t\t\t\"HasRunKey\"\t\t\"HKEY_LOCAL_MACHINE\\\\Software\\\\Valve\\\\Steam\\\\Apps\\\\696180\"\n"
      "\t\t\t\"process 1\"\t\t\"msiexec\"\n"
      "\t\t\t\"command 1\"\t\t\"/i \\\"%INSTALLDIR%\\\\_CommonRedist\\\\PhysX\\\\9.13.0604\\\\PhysX.msi\\\" /quiet /norestart\"\n"
      "\t\t}\n\t\t\"DirectX\"\n\t\t{\n\t\t\t\"process 1\"\t\"%INSTALLDIR%\\\\DirectX\\\\DXSETUP.exe\"\n\t\t}\n"
      "\t\t\"Launcher\"\n\t\t{\n\t\t\t\"process 1\"\t\"%INSTALLDIR%\\\\Setup.exe\"\n\t\t}\n"
      "\t\t\"Lang\"\n\t\t{\n\t\t\t\"process 1\"\t\"%LANGUAGE%.exe\"\n\t\t}\n\t}\n}\n"
      "\"kvsignatures\"\n{\n\t\"InstallScript\"\t\t\"8ed8c256\"\n}\n";
  std::vector<RedistEntry> entries;
  std::string reason;
  CHECK(ParseRunProcess(script, "Z:\\data\\games\\696180", &entries, &reason));
  CHECK(entries.size() == 3);
  CHECK(entries[0].name == "PhysX" && entries[0].process == "msiexec");
  CHECK(entries[0].command == "/i \"Z:\\data\\games\\696180\\_CommonRedist\\PhysX\\9.13.0604\\PhysX.msi\" /quiet /norestart");
  CHECK(entries[0].has_run_key == "HKEY_LOCAL_MACHINE\\Software\\Valve\\Steam\\Apps\\696180");
  std::string package;
  CHECK(ClassifyRedist(entries[0], "Z:\\data\\games\\696180", &package) == RedistAction::kMsi);
  CHECK(package == "_CommonRedist/PhysX/9.13.0604/PhysX.msi");
  CHECK(ClassifyRedist(entries[1], "Z:\\data\\games\\696180", &package) == RedistAction::kCovered);
  CHECK(ClassifyRedist(entries[2], "Z:\\data\\games\\696180", &package) == RedistAction::kNotRun);
  // A package outside the game folder is not taken.
  RedistEntry outside{"X", "msiexec", "/i C:\\other\\x.msi", ""};
  CHECK(ClassifyRedist(outside, "Z:\\data\\games\\696180", &package) == RedistAction::kNotRun);
  RedistEntry climbing{"X", "msiexec", "/i Z:\\data\\games\\696180\\..\\x.msi", ""};
  CHECK(ClassifyRedist(climbing, "Z:\\data\\games\\696180", &package) == RedistAction::kNotRun);
  // Steam's signature block after the script doesn't hide its sections.
  InstallScriptValues values;
  CHECK(ParseInstallScript("\"InstallScript\" { \"Registry\" { \"HKEY_LOCAL_MACHINE\\\\Software\\\\Studio\" { \"string\" { "
                           "\"Path\" \"%INSTALLDIR%\" } } } } \"kvsignatures\" { \"InstallScript\" \"00\" }",
                           "Z:\\games\\1", &values, &reason));
  CHECK(values.machine.size() == 2);
  RedistEntry rooted{"X", "msiexec", "/i Z:\\data\\games\\696180\\\\tmp\\x.msi", ""};
  CHECK(ClassifyRedist(rooted, "Z:\\data\\games\\696180", &package) == RedistAction::kNotRun);
  RedistEntry dotted{"X", "msiexec", "/i Z:\\data\\games\\696180\\.\\x.msi", ""};
  CHECK(ClassifyRedist(dotted, "Z:\\data\\games\\696180", &package) == RedistAction::kNotRun);
  RedistEntry direct{"X", "Z:\\data\\games\\696180\\redist\\x.msi", "", ""};
  CHECK(ClassifyRedist(direct, "Z:\\data\\games\\696180", &package) == RedistAction::kMsi && package == "redist/x.msi");
}

void TestInstall() {
  char pattern[] = "/tmp/playden-redist-XXXXXX";
  CHECK(::mkdtemp(pattern));
  const auto base = std::filesystem::canonical(pattern);  // /tmp is a symlink on macOS
  const auto game = base / "game", prefix = base / "prefix";
  WriteText(game / "_CommonRedist/Sample/1.0/installscript.vdf",
            "\"InstallScript\" { \"Run Process\" { \"Sample\" { "
            "\"HasRunKey\" \"HKEY_LOCAL_MACHINE\\\\Software\\\\Valve\\\\Steam\\\\Apps\\\\42\" "
            "\"process 1\" \"msiexec\" "
            "\"command 1\" \"/i \\\"%INSTALLDIR%\\\\_CommonRedist\\\\Sample\\\\1.0\\\\sample.msi\\\" /quiet\" } "
            "\"VC\" { \"process 1\" \"%INSTALLDIR%\\\\_CommonRedist\\\\vcredist_x86.exe\" } } } "
            "\"kvsignatures\" { \"InstallScript\" \"00\" }");
  WriteText(game / "_CommonRedist/Sample/1.0/sample.msi", SamplePackage());
  WriteText(game / "data/deep/installscript.vdf", "\"InstallScript\" { \"Run Process\" { \"Ignored\" { \"process 1\" \"x.exe\" } } }");
  WriteText(prefix / "system.reg",
            "WINE REGISTRY Version 2\n;; All keys relative to \\\\Machine\n\n#arch=win64\n\n"
            "[System\\\\CurrentControlSet\\\\Control\\\\Session Manager\\\\Environment] 1700000000\n"
            "\"PATH\"=str(2):\"%SystemRoot%\\\\system32;%SystemRoot%\"\n\"TEMP\"=str(2):\"%SystemRoot%\\\\temp\"\n");
  WriteText(prefix / "user.reg", "WINE REGISTRY Version 2\n;; All keys relative to \\\\User\\\\S-1-5-21-0-0-0-1000\n\n#arch=win64\n");
  std::filesystem::create_directories(prefix / "drive_c/program files (x86)");
  // A bottle is private to Playden, as on the console.
  std::filesystem::permissions(prefix, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
  for (const auto* hive : {"system.reg", "user.reg"})
    std::filesystem::permissions(prefix / hive, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace);
  const auto result = InstallRedistributables(game.string(), "Z:\\games\\42", prefix.string(), 1700000100);
  CHECK(result.outcomes.size() == 2);
  CHECK(result.outcomes[0].script == "_CommonRedist/Sample/1.0/installscript.vdf");
  CHECK(result.outcomes[0].name == "Sample" && result.outcomes[0].result == "installed" && result.outcomes[0].files == 3);
  CHECK(result.outcomes[1].name == "VC" && result.outcomes[1].result == "covered");
  // The existing folder is reused whatever its case.
  CHECK(ReadText(prefix / "drive_c/program files (x86)/Vendor Runtime/Common/Loader.dll") == "0123456789");
  CHECK(ReadText(prefix / "drive_c/program files (x86)/Vendor Runtime/Engine/v1/Core.dll") == "0123456789");
  CHECK(std::filesystem::exists(prefix / "drive_c/program files (x86)/Vendor Runtime/Engine/v1/Empty.dat"));
  std::vector<std::string> drive;
  for (const auto& child : std::filesystem::directory_iterator(prefix / "drive_c")) drive.push_back(child.path().filename().string());
  CHECK((drive == std::vector<std::string>{"program files (x86)"}));
  const auto system = ReadText(prefix / "system.reg");
  CHECK(system.find("[Software\\\\Wow6432Node\\\\Vendor]") != std::string::npos);
  CHECK(system.find("\"Core Path\"=\"C:\\\\Program Files (x86)\\\\Vendor Runtime\\\\Engine\\\\\"") != std::string::npos);
  CHECK(system.find("\"PATH\"=str(2):\"C:\\\\Program Files (x86)\\\\Vendor Runtime\\\\Common\\\\;%SystemRoot%\\\\system32;%SystemRoot%\"") !=
        std::string::npos);
  CHECK(system.find("\"TEMP\"=str(2):\"%SystemRoot%\\\\temp\"") != std::string::npos);
  CHECK(system.find("[Software\\\\Wow6432Node\\\\Valve\\\\Steam\\\\Apps\\\\42]") != std::string::npos);
  CHECK(system.find("\"Sample\"=dword:00000001") != std::string::npos);
  CHECK(ReadText(prefix / "user.reg").find("\"User\"=") != std::string::npos);
  CHECK(ReadText(prefix / ".playden-redist").find(" _CommonRedist/Sample/1.0/sample.msi\n") != std::string::npos);
  // Installed once: the next Play leaves the bottle alone.
  const auto again = InstallRedistributables(game.string(), "Z:\\games\\42", prefix.string(), 1700000200);
  CHECK(again.outcomes.size() == 2 && again.outcomes[0].result == "already");
  CHECK(ReadText(prefix / "system.reg") == system);
  // A package whose cabinet doesn't match its file list, or that keeps files beside it, fails and isn't recorded.
  for (const auto variant : {Variant::kWrongSize, Variant::kUncompressed}) {
    std::filesystem::remove(prefix / ".playden-redist");
    WriteText(game / "_CommonRedist/Sample/1.0/sample.msi", SamplePackage(variant));
    const auto refused = InstallRedistributables(game.string(), "Z:\\games\\42", prefix.string(), 1700000250);
    CHECK(refused.outcomes.size() == 2 && refused.outcomes[0].result == "failed");
    CHECK(!std::filesystem::exists(prefix / ".playden-redist"));
  }
  // A damaged package fails alone.
  std::filesystem::remove(prefix / ".playden-redist");
  WriteText(game / "_CommonRedist/Sample/1.0/sample.msi", "broken");
  const auto broken = InstallRedistributables(game.string(), "Z:\\games\\42", prefix.string(), 1700000300);
  CHECK(broken.outcomes.size() == 2 && broken.outcomes[0].result == "failed" && broken.outcomes[1].result == "covered");
  CHECK(broken.outcomes[0].detail.find("not a Windows Installer package") != std::string::npos);
  std::filesystem::remove_all(base);
}

// NVIDIA's legacy PhysX package (Bullet Witch's), when PLAYDEN_TEST_PHYSX_MSI names a copy.
void TestPhysxPackage() {
  const char* path = std::getenv("PLAYDEN_TEST_PHYSX_MSI");
  if (!path || !*path) return;
  const auto package = ReadText(path);
  MsiInstallPlan plan;
  std::string reason;
  CHECK(ReadMsiPackage(package, &plan, &reason));
  CHECK(plan.product_name == "NVIDIA PhysX (Legacy)");
  const auto has = [&](const std::string& target) {
    return std::any_of(plan.files.begin(), plan.files.end(), [&](const MsiFile& file) { return file.target == target; });
  };
  CHECK(has("Program Files (x86)/NVIDIA Corporation/PhysX/Common/PhysXLoader.dll"));
  CHECK(has("Program Files (x86)/NVIDIA Corporation/PhysX/Common/PhysXLoader64.dll"));
  CHECK(has("Program Files (x86)/NVIDIA Corporation/PhysX/Engine/v2.6.4/PhysXCore.dll"));
  const auto* core = FindValue(plan.machine, "Software\\Wow6432Node\\AGEIA Technologies", "PhysXCore Path");
  CHECK(core && core->data == "\"C:\\\\Program Files (x86)\\\\NVIDIA Corporation\\\\PhysX\\\\Engine\"");
  CHECK((plan.path_prepend == std::vector<std::string>{"C:\\Program Files (x86)\\NVIDIA Corporation\\PhysX\\Common"}));
  const auto cabinet = ReadMsiStream(package, plan.cabinets.at(0).name);
  CHECK(cabinet);
  // PLAYDEN_TEST_PHYSX_FILES may name a folder with the members another tool extracted.
  const char* reference = std::getenv("PLAYDEN_TEST_PHYSX_FILES");
  std::map<std::string, std::uint32_t> listed;
  for (const auto& file : plan.files) listed[file.member] = file.size;
  std::size_t count = 0, loader = 0, compared = 0;
  CHECK(ExtractCabinet(*cabinet, [&](const std::string& name, std::string_view data) {
    ++count;
    CHECK(!listed.count(name) || listed[name] == data.size());  // the installer checks this
    if (name == "FILE_COMMON_PhysXLoader_dll") loader = data.size();
    if (reference && *reference) {
      CHECK(ReadText(std::filesystem::path(reference) / name) == data);
      ++compared;
    }
    return true;
  }, &reason));
  CHECK(!reference || !*reference || compared == 43);
  CHECK(count == 43 && loader == 57224);
  std::fprintf(stderr, "PhysX package: %zu files, %zu machine values\n", plan.files.size(), plan.machine.size());
}

}  // namespace

int main() {
  TestConditions();
  TestCabinet();
  TestPlan();
  TestPath();
  TestBurnBundle();
  TestScripts();
  TestInstall();
  TestPhysxPackage();
  return 0;
}
