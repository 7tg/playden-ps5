#include "runtime/install_script.h"
#include "runtime/msi_package.h"
#include "runtime/redist_install.h"

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

namespace {

void Put16(std::string* out, unsigned value) {
  out->push_back(static_cast<char>(value & 0xff));
  out->push_back(static_cast<char>((value >> 8) & 0xff));
}

void Put32(std::string* out, std::uint32_t value) {
  Put16(out, value & 0xffff);
  Put16(out, value >> 16);
}

// Deflate bits, least significant first; Huffman codes go in most significant bit first.
struct Bits {
  std::string bytes;
  unsigned current = 0, count = 0;
  void Put(unsigned value, unsigned width) {
    for (unsigned bit = 0; bit < width; ++bit) Push((value >> bit) & 1);
  }
  void Code(unsigned value, unsigned width) {
    for (unsigned bit = width; bit-- > 0;) Push((value >> bit) & 1);
  }
  void Push(unsigned bit) {
    current |= bit << count;
    if (++count == 8) Flush();
  }
  void Flush() {
    if (count) bytes.push_back(static_cast<char>(current));
    current = count = 0;
  }
};

// A cabinet with one MSZIP folder of two blocks: a stored block "0123456789", then a fixed
// Huffman block that copies those ten bytes back (length 10, distance 10) — only right when
// the second block sees the first one's output. Members: "first" (block 1), "second" (block 2),
// "empty" (no bytes).
std::string TwoBlockCabinet() {
  std::string block1 = "CK";
  {
    Bits bits;
    bits.Put(1, 1);  // final
    bits.Put(0, 2);  // stored
    bits.Flush();
    block1 += bits.bytes;
    Put16(&block1, 10);
    Put16(&block1, 0xffff ^ 10);
    block1 += "0123456789";
  }
  std::string block2 = "CK";
  {
    Bits bits;
    bits.Put(1, 1);           // final
    bits.Put(1, 2);           // fixed Huffman
    bits.Code(264 - 256, 7);  // length 10
    bits.Code(6, 5);          // distance 9-12
    bits.Put(1, 2);           // + 1 = 10
    bits.Code(0, 7);          // end of block
    bits.Flush();
    block2 += bits.bytes;
  }
  struct Member { const char* name; std::uint32_t size, offset; };
  const Member members[] = {{"first", 10, 0}, {"second", 10, 10}, {"empty", 0, 20}};
  std::string files;
  for (const auto& member : members) {
    Put32(&files, member.size);
    Put32(&files, member.offset);
    Put16(&files, 0);  // folder
    Put16(&files, 0);
    Put16(&files, 0);
    Put16(&files, 0x20);
    files += member.name;
    files.push_back('\0');
  }
  const std::uint32_t header = 36, folder = 8;
  const std::uint32_t files_at = header + folder;
  const std::uint32_t data_at = files_at + static_cast<std::uint32_t>(files.size());
  std::string data;
  for (const auto* block : {&block1, &block2}) {
    Put32(&data, 0);
    Put16(&data, static_cast<unsigned>(block->size()));
    Put16(&data, 10);
    data += *block;
  }
  std::string cabinet = "MSCF";
  Put32(&cabinet, 0);
  Put32(&cabinet, data_at + static_cast<std::uint32_t>(data.size()));
  Put32(&cabinet, 0);
  Put32(&cabinet, files_at);
  Put32(&cabinet, 0);
  cabinet.push_back(3);
  cabinet.push_back(1);
  Put16(&cabinet, 1);  // folders
  Put16(&cabinet, 3);  // files
  Put16(&cabinet, 0);  // flags
  Put16(&cabinet, 0);
  Put16(&cabinet, 0);
  Put32(&cabinet, data_at);
  Put16(&cabinet, 2);  // blocks
  Put16(&cabinet, 1);  // MSZIP
  return cabinet + files + data;
}

// ---------------------------------------------------------------------------------------------
// A minimal Windows Installer package: a compound file (512-byte sectors, every stream in
// ordinary sectors) holding the string pool, _Columns and a few tables.

std::vector<std::uint16_t> EncodeStreamName(const std::string& name) {
  static const std::string kAlphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz._";
  std::vector<std::uint16_t> out;
  std::size_t at = 0;
  if (!name.empty() && name[0] == '!') {
    out.push_back(0x4840);
    at = 1;
  }
  while (at < name.size()) {
    const auto first = kAlphabet.find(name[at]);
    if (first == std::string::npos) {
      out.push_back(static_cast<unsigned char>(name[at++]));
      continue;
    }
    const auto second = at + 1 < name.size() ? kAlphabet.find(name[at + 1]) : std::string::npos;
    if (second != std::string::npos) {
      out.push_back(static_cast<std::uint16_t>(0x3800 + first + (second << 6)));
      at += 2;
    } else {
      out.push_back(static_cast<std::uint16_t>(0x4800 + first));
      ++at;
    }
  }
  return out;
}

std::string CompoundFile(const std::vector<std::pair<std::string, std::string>>& streams) {
  constexpr std::uint32_t kSector = 512, kEnd = 0xfffffffe, kFree = 0xffffffff;
  std::vector<std::uint32_t> stream_sectors;
  std::uint32_t data_sectors = 0;
  for (const auto& stream : streams) {
    const auto count = static_cast<std::uint32_t>((stream.second.size() + kSector - 1) / kSector);
    stream_sectors.push_back(count);
    data_sectors += count;
  }
  const auto directory_sectors = static_cast<std::uint32_t>(((streams.size() + 1) * 128 + kSector - 1) / kSector);
  std::uint32_t fat_sectors = 1;
  while (fat_sectors * (kSector / 4) < fat_sectors + directory_sectors + data_sectors) ++fat_sectors;
  const std::uint32_t total = fat_sectors + directory_sectors + data_sectors;
  std::vector<std::uint32_t> fat(fat_sectors * (kSector / 4), kFree);
  for (std::uint32_t index = 0; index < fat_sectors; ++index) fat[index] = 0xfffffffd;
  const auto chain = [&](std::uint32_t first, std::uint32_t count) {
    for (std::uint32_t index = 0; index < count; ++index) fat[first + index] = index + 1 < count ? first + index + 1 : kEnd;
  };
  chain(fat_sectors, directory_sectors);
  std::string directory;
  const auto entry = [&](const std::vector<std::uint16_t>& name, int type, std::uint32_t start, std::uint32_t size) {
    std::string out;
    for (const auto unit : name) Put16(&out, unit);
    out.resize(64, '\0');
    Put16(&out, static_cast<unsigned>((name.size() + 1) * 2));
    out.push_back(static_cast<char>(type));
    out.push_back(1);
    Put32(&out, 0xffffffff);
    Put32(&out, 0xffffffff);
    Put32(&out, type == 5 ? 1 : 0xffffffff);
    out.resize(0x74, '\0');
    Put32(&out, start);
    Put32(&out, size);
    Put32(&out, 0);
    directory += out;
  };
  entry({'R', 'o', 'o', 't', ' ', 'E', 'n', 't', 'r', 'y'}, 5, kEnd, 0);
  std::uint32_t next = fat_sectors + directory_sectors;
  std::string body;
  for (std::size_t index = 0; index < streams.size(); ++index) {
    const auto& [name, data] = streams[index];
    entry(EncodeStreamName(name), 2, stream_sectors[index] ? next : kEnd, static_cast<std::uint32_t>(data.size()));
    chain(next, stream_sectors[index]);
    next += stream_sectors[index];
    std::string padded = data;
    padded.resize(stream_sectors[index] * kSector, '\0');
    body += padded;
  }
  directory.resize(directory_sectors * kSector, '\0');
  std::string header = "\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1";
  header.resize(0x18, '\0');
  Put16(&header, 0x3e);
  Put16(&header, 3);
  Put16(&header, 0xfffe);
  Put16(&header, 9);
  Put16(&header, 6);
  header.resize(0x2c, '\0');
  Put32(&header, fat_sectors);
  Put32(&header, fat_sectors);  // first directory sector
  Put32(&header, 0);
  Put32(&header, 0);  // mini stream cutoff: nothing in the mini stream
  Put32(&header, kEnd);
  Put32(&header, 0);
  Put32(&header, kEnd);
  Put32(&header, 0);
  for (std::uint32_t index = 0; index < 109; ++index) Put32(&header, index < fat_sectors ? index : kFree);
  std::string fat_bytes;
  for (const auto value : fat) Put32(&fat_bytes, value);
  (void)total;
  return header + fat_bytes + directory + body;
}

struct Column {
  std::string name;
  char kind;  // 's' string, 'i' 2-byte integer, 'l' 4-byte integer
};

struct TableSpec {
  std::string name;
  std::vector<Column> columns;
  std::vector<std::vector<std::string>> rows;  // "" is null for strings; integers as text, "" null
};

std::string Package(const std::vector<TableSpec>& tables, const std::vector<std::pair<std::string, std::string>>& extra) {
  std::vector<std::string> strings{""};
  std::map<std::string, std::uint16_t> ids;
  const auto id = [&](const std::string& text) -> std::uint16_t {
    if (text.empty()) return 0;
    const auto found = ids.find(text);
    if (found != ids.end()) return found->second;
    strings.push_back(text);
    return ids[text] = static_cast<std::uint16_t>(strings.size() - 1);
  };
  std::vector<std::pair<std::string, std::string>> streams;
  std::vector<std::string> column_table, column_name;
  std::vector<unsigned> column_number, column_type;
  for (const auto& table : tables) {
    for (std::size_t index = 0; index < table.columns.size(); ++index) {
      column_table.push_back(table.name);
      column_name.push_back(table.columns[index].name);
      column_number.push_back(static_cast<unsigned>(index + 1));
      const char kind = table.columns[index].kind;
      column_type.push_back(kind == 's' ? 0x1dff : kind == 'i' ? 0x1502 : 0x1104);
    }
    std::string data;
    for (std::size_t column = 0; column < table.columns.size(); ++column) {
      for (const auto& row : table.rows) {
        const auto& cell = row[column];
        const char kind = table.columns[column].kind;
        if (kind == 's') Put16(&data, id(cell));
        else if (kind == 'i') Put16(&data, cell.empty() ? 0 : static_cast<unsigned>(std::atoi(cell.c_str()) + 0x8000));
        else Put32(&data, cell.empty() ? 0 : static_cast<std::uint32_t>(std::atoll(cell.c_str()) + 0x80000000ll));
      }
    }
    streams.emplace_back("!" + table.name, data);
  }
  std::string columns;
  for (const auto& name : column_table) Put16(&columns, id(name));
  for (const auto number : column_number) Put16(&columns, number + 0x8000);
  for (const auto& name : column_name) Put16(&columns, id(name));
  for (const auto type : column_type) Put16(&columns, type | 0x8000);
  streams.emplace_back("!_Columns", columns);
  std::string pool, pool_data;
  Put32(&pool, 1252);
  for (std::size_t index = 1; index < strings.size(); ++index) {
    Put16(&pool, static_cast<unsigned>(strings[index].size()));
    Put16(&pool, 1);
    pool_data += strings[index];
  }
  streams.emplace_back("!_StringPool", pool);
  streams.emplace_back("!_StringData", pool_data);
  for (const auto& stream : extra) streams.push_back(stream);
  return CompoundFile(streams);
}

// A package like NVIDIA's legacy PhysX one: a 32-bit loader in Vendor\Common (added to PATH),
// an engine in Vendor\Engine\v1 found through a registry value, a 64-bit feature turned on by
// VersionNT64, a feature left off, and files in the two-block cabinet above.
std::string SamplePackage() {
  std::vector<TableSpec> tables;
  tables.push_back({"Property", {{"Property", 's'}, {"Value", 's'}},
                    {{"ProductCode", "{11111111-2222-3333-4444-555555555555}"}, {"ProductName", "Sample Runtime"},
                     {"ProductVersion", "1.2.3"}, {"P_VERSION", "7"}}});
  tables.push_back({"Directory", {{"Directory", 's'}, {"Directory_Parent", 's'}, {"DefaultDir", 's'}},
                    {{"TARGETDIR", "", "SourceDir"}, {"ProgramFilesFolder", "TARGETDIR", "."},
                     {"D_VENDOR", "ProgramFilesFolder", "vendor~1|Vendor Runtime"}, {"D_COMMON", "D_VENDOR", "Common"},
                     {"D_ENGINE", "D_VENDOR", "Engine"}, {"D_V1", "D_ENGINE", "v1~1|v1"},
                     {"D_DESKTOP", "DesktopFolder", "Sample"}}});
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
                    {{"first", "C_LOADER", "loader~1.dll|Loader.dll", "10", "", "", "512", "1"},
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
                     {"r8", "2", "SOFTWARE\\Vendor", "Hidden", "x", "C_OFF"}}});
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
  CHECK(plan.skipped_registry == 2);  // the binary value and the OTHER variable
  CHECK((plan.path_prepend == std::vector<std::string>{"C:\\Program Files (x86)\\Vendor Runtime\\Common\\"}));
  CHECK(plan.path_append.empty());
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
      "\t\t\"Lang\"\n\t\t{\n\t\t\t\"process 1\"\t\"%LANGUAGE%.exe\"\n\t\t}\n\t}\n}\n";
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
            "\"VC\" { \"process 1\" \"%INSTALLDIR%\\\\_CommonRedist\\\\vcredist_x86.exe\" } } }");
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
  std::size_t count = 0, loader = 0, compared = 0;
  CHECK(ExtractCabinet(*cabinet, [&](const std::string& name, std::string_view data) {
    ++count;
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
  TestScripts();
  TestInstall();
  TestPhysxPackage();
  return 0;
}
