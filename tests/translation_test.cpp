#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

#include "mods/translation_data.h"

using namespace broken_bond::translation;
namespace {
unsigned checks = 0;
void Check(bool value, const char* label) {
  ++checks;
  if (!value)
    throw std::runtime_error(label);
}
void Put32(std::vector<uint8_t>& data, uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8)
    data.push_back(uint8_t(value >> shift));
}
std::vector<uint8_t> Narrow(std::string_view text) {
  std::vector<uint8_t> bytes(text.begin(), text.end());
  bytes.push_back(0);
  return bytes;
}
std::vector<uint8_t> Wide(std::string_view ascii) {
  std::vector<uint8_t> bytes;
  for (char value : ascii) {
    bytes.push_back(uint8_t(value));
    bytes.push_back(0);
  }
  bytes.push_back(0);
  bytes.push_back(0);
  return bytes;
}
// One table entry as the game reads it: its descriptor words and its pool strings.
struct Entry {
  uint32_t id, flags;
  std::vector<uint8_t> text;
  std::vector<uint32_t> attributes;
  std::vector<std::string> names;
};
std::vector<uint8_t> Table(const std::vector<Entry>& entries, uint32_t version = 3) {
  std::vector<uint8_t> pool, descriptors;
  for (const auto& entry : entries) {
    pool.insert(pool.end(), entry.text.begin(), entry.text.end());
    for (const auto& name : entry.names) {
      const auto bytes = Narrow(name);
      pool.insert(pool.end(), bytes.begin(), bytes.end());
    }
    Put32(descriptors, entry.id);
    Put32(descriptors, entry.flags);
    for (uint32_t word : entry.attributes)
      Put32(descriptors, word);
  }
  std::vector<uint8_t> data;
  Put32(data, (version << 24) | uint32_t(entries.size()));
  Put32(data, uint32_t(pool.size()));
  data.insert(data.end(), pool.begin(), pool.end());
  data.insert(data.end(), descriptors.begin(), descriptors.end());
  return data;
}
constexpr uint32_t kFloatOne = 0x3F800000, kFloatTwo = 0x40000000;
// A style with a name after it, a wide text with a style, an entry without
// attributes, and a float range followed by a kind the game skips.
std::vector<uint8_t> Fixture() {
  return Table({{1992, 0x40000000, Narrow("OPTIONS"),
                 {0x80000000, 0xFFFFFFFF, 0x00FF37FE, 0xFFF00000, 0x00000001},
                 {"Control1000"}},
                {555, 0xC0000000, Wide("Old"), {0x00000000, 0x011D8822, 0x68FF37FE, 0xFFF00000}, {}},
                {99, 0x00000000, Narrow("Untouched"), {}, {}},
                {7, 0x40000000, Narrow("Range"), {0x80000002, kFloatOne, kFloatTwo, 0x00000005}, {}}});
}
std::vector<TextRow> Read(const std::vector<uint8_t>& bytes) {
  std::vector<TextRow> rows;
  std::string error;
  Check(ReadTable(bytes, rows, error), error.c_str());
  return rows;
}
std::vector<uint8_t> NarrowTable(std::string_view text) {
  return Table({{1, 0x40000000, Narrow(text), {0x00000000, 0xFFFFFFFF, 0x00FF37FE, 0xFFF00000}, {}}});
}
void CheckRejected(const PatchResult& result, uint32_t id) {
  Check(result.status == PatchStatus::kUnchanged && result.bytes.empty() &&
            result.replacements == 0 && result.error.empty(),
        "invalid entry must leave the original table usable");
  Check(result.rejected.size() == 1 && result.rejected[0].id == id &&
            !result.rejected[0].reason.empty(),
        "rejection must identify the text ID and reason");
}
void JsonTests() {
  Dictionary out{{42, "original"}};
  std::string error;
  for (std::string_view bad :
       {R"([])", R"({"-1":"x"})", R"({"4294967296":"x"})", R"({"1":false})", R"({"1":"a","1":"b"})",
        R"({"01":"a","1":"b"})", R"({"1":"\u0000"})", R"({"1":"\ud800"})", R"({"1":"x",})"}) {
    Check(!ParseDictionary(bad, out, error), "malformed JSON accepted");
    Check(out == Dictionary{{42, "original"}}, "failed JSON mutated dictionary");
  }
  Check(ParseDictionary(R"({"1992":"OP\u00c7\u00d5ES","555":"\u65e5\u672c"})", out, error),
        "valid JSON rejected");
  Check(out.at(1992) ==
            "OP\xC3\x87\xC3\x95"
            "ES",
        "UTF-8 JSON changed");
  Check(ParseDictionary("\xEF\xBB\xBF{\"1992\":\"BOM\"}", out, error) && out.at(1992) == "BOM",
        "UTF-8 BOM rejected");
  Check(!ValidModId("../outside") && !ValidModId("C:\\outside") && ValidModId("pt-br-demo"),
        "mod ID validation");
}
void TableTests() {
  const auto original = Fixture();
  const auto before = original;
  const auto old = Read(original);
  Check(old.size() == 4 && old[0].text == "OPTIONS" && old[1].text == "Old" && old[1].wide &&
            old[2].attributes.empty() && old[3].text == "Range",
        "fixture not read as the game reads it");
  Check(old[0].names == Narrow("Control1000") && old[0].attributes.size() == 20 &&
            old[3].attributes.size() == 16,
        "attributes or names not kept with their entry");
  Dictionary text{{1992,
                   "OP\xC3\x87\xC3\x95"
                   "ES DE JOGO"},
                  {555, "\xE6\x97\xA5\xE6\x9C\xAC"},
                  {777, "unused"}};
  const auto patched = PatchTable(original, text);
  Check(patched.status == PatchStatus::kRebuilt && patched.replacements == 2, "table not rebuilt");
  const auto rows = Read(patched.bytes);
  Check(original == before, "input modified");
  Check(rows.size() == 4, "row count changed");
  for (size_t i = 0; i < rows.size(); ++i) {
    Check(rows[i].id == old[i].id && rows[i].flags == old[i].flags &&
              rows[i].attributes == old[i].attributes && rows[i].names == old[i].names,
          "ID, flags, attributes or names changed");
  }
  Check(rows[0].text == text.at(1992) && !rows[0].wide && rows[0].encoded.size() == 15,
        "Latin-1 translation/longer size");
  Check(rows[0].encoded[2] == 0xC7 && rows[0].encoded[3] == 0xD5, "Portuguese bytes");
  Check(rows[1].text == text.at(555) && rows[1].wide && rows[1].encoded.size() == 6,
        "wide text changed");
  Check(rows[2].encoded == old[2].encoded && rows[3].encoded == old[3].encoded,
        "untranslated data changed");
  // Translating back reproduces the original table byte for byte.
  const auto restored = PatchTable(patched.bytes, {{1992, "OPTIONS"}, {555, "Old"}});
  Check(restored.status == PatchStatus::kRebuilt && restored.bytes == original,
        "round trip did not restore the original bytes");
  Check(PatchTable(original, {{999, "absent"}}).status == PatchStatus::kUnchanged,
        "missing ID did not fall back");
  Check(PatchTable(original, {{1992, "OPTIONS"}}).status == PatchStatus::kUnchanged,
        "same text rebuilt");
  auto unicode = PatchTable(original, {{1992, "\xF0\x9F\x8D\xA5"}});
  const auto wide = Read(unicode.bytes);
  Check(wide[0].wide && (wide[0].flags & 0x80000000) && wide[0].encoded.size() == 6 &&
            wide[0].names == old[0].names,
        "supplementary Unicode units");
  const auto empty = PatchTable(original, {{1992, ""}});
  Check(Read(empty.bytes)[0].encoded == std::vector<uint8_t>{0}, "empty translation rejected");
  std::string error;
  std::vector<TextRow> ignored;
  for (size_t size = 0; size < original.size(); ++size)
    Check(!ReadTable(std::span(original).first(size), ignored, error), "truncated table accepted");
  auto invalid = original;
  invalid.push_back(0);
  Check(!ReadTable(invalid, ignored, error), "trailing data accepted");
  invalid = original;
  invalid[4] += 1;  // One pool byte more than the strings use.
  Check(!ReadTable(invalid, ignored, error), "unaccounted pool bytes accepted");
  for (uint8_t version : {uint8_t(1), uint8_t(4)}) {
    invalid = original;
    invalid[3] = version;
    Check(PatchTable(invalid, text).status == PatchStatus::kInvalid, "unsupported version accepted");
  }
  const auto unterminated = Table({{1, 0x80000000, {'A', 0}, {}, {}}});
  Check(!ReadTable(unterminated, ignored, error), "unterminated wide text accepted");
  const auto duplicate = Table({{1, 0, Narrow("A"), {}, {}}, {1, 0, Narrow("B"), {}, {}}});
  Check(!ReadTable(duplicate, ignored, error), "duplicate ID accepted");
  // Before version 3 a style has two words.
  const auto version2 =
      Table({{1, 0x40000000, Narrow("Two"), {0x80000000, 0xFFFFFFFF, 0x00FF37FE, 0x00000001},
              {"Name"}}},
            2);
  const auto patched2 = PatchTable(version2, {{1, "Dois"}});
  Check(patched2.status == PatchStatus::kRebuilt && patched2.bytes[3] == 2 &&
            Read(patched2.bytes)[0].names == Narrow("Name"),
        "version 2 table not rebuilt");
  CheckRejected(PatchTable(original, {{1992, "<x>"}}), 1992);
  CheckRejected(PatchTable(original, {{1992, "%1"}}), 1992);
}
void SubstitutionTests() {
  const auto source = NarrowTable("Unlock %1 in exchange for %2 Gold Coins?");
  for (const char* bad : {"Unlock item for %2", "%1 %1 %2", "%1 %2 %3", "%1 %20"})
    CheckRejected(PatchTable(source, {{1, bad}}), 1);
  Check(PatchTable(source, {{1, "Por %2 moedas, desbloquear %1?"}}).status == PatchStatus::kRebuilt,
        "reordered numbered arguments rejected");
  CheckRejected(PatchTable(NarrowTable("RAMEN DELIVERY LEVEL %1"), {{1, "NIVEL DE ENTREGA"}}), 1);
  Check(PatchTable(NarrowTable("Make 100% of the villagers happy"),
                   {{1, "Deixe 100% dos moradores felizes"}})
                .status == PatchStatus::kRebuilt,
        "literal percentage rejected");
  const auto printf = NarrowTable("Targets left: %d");
  Check(PatchTable(printf, {{1, "Alvos restantes: %d"}}).status == PatchStatus::kRebuilt,
        "printf conversion kept but rejected");
  CheckRejected(PatchTable(printf, {{1, "Alvos restantes"}}), 1);
  CheckRejected(PatchTable(printf, {{1, "Alvos: %s"}}), 1);
  Check(PatchTable(NarrowTable("%AGE OF PERFECT MATCHES:"), {{1, "PORCENTAGEM DE ACERTOS:"}})
                .status == PatchStatus::kRebuilt,
        "literal percent sign before a word rejected");
}
void EntryFallbackTests() {
  const auto original = Fixture();
  const auto before = Read(original);
  const auto result =
      PatchTable(original, {{1992, "OPCOES"}, {555, "<br>"}, {99, "Changed %1"}});
  Check(result.status == PatchStatus::kRebuilt && result.replacements == 1 &&
            result.rejected.size() == 2 && result.error.empty(),
        "bad entries discarded good translations");
  Check(result.rejected[0].id == 555 && result.rejected[1].id == 99,
        "did not report every rejected ID");
  const auto rows = Read(result.bytes);
  Check(rows[0].text == "OPCOES", "valid translation lost");
  for (size_t index = 1; index < rows.size(); ++index) {
    Check(rows[index].encoded == before[index].encoded && rows[index].wide == before[index].wide &&
              rows[index].flags == before[index].flags &&
              rows[index].attributes == before[index].attributes,
          "rejected entry did not preserve its encoded original");
  }
  CheckRejected(PatchTable(original, {{1992, std::string("a\0b", 3)}}), 1992);
  CheckRejected(PatchTable(original, {{1992, "\xC3"}}), 1992);
  CheckRejected(PatchTable(original, {{1992, std::string(kMaxTextUnits + 1, 'a')}}), 1992);
  CheckRejected(PatchTable(NarrowTable("<orange>%1</orange>"), {{1, "<orange>%1"}}), 1);
}
void TypographyTests() {
  const auto original = NarrowTable("Original");
  // Exact Unicode values, independent of the codec's own decoding assumptions.
  for (const auto& [utf8, unit] : {std::pair{"\xE2\x80\x99", char16_t(0x2019)},
                                   {"\xE2\x80\x9C", char16_t(0x201C)},
                                   {"\xE2\x80\x9D", char16_t(0x201D)},
                                   {"\xE2\x80\x9E", char16_t(0x201E)},
                                   {"\xE2\x80\x93", char16_t(0x2013)},
                                   {"\xE2\x80\x94", char16_t(0x2014)},
                                   {"\xE2\x80\xA6", char16_t(0x2026)},
                                   {"\xC5\x93", char16_t(0x0153)},
                                   {"\xC2\x92", char16_t(0x0092)}}) {
    const auto result = PatchTable(original, {{1, utf8}});
    Check(result.status == PatchStatus::kRebuilt && result.rejected.empty(),
          "Unicode translation rejected");
    const auto row = Read(result.bytes)[0];
    Check(row.wide && (row.flags & 0x80000000) &&
              row.encoded == std::vector<uint8_t>{uint8_t(unit), uint8_t(unit >> 8), 0, 0},
          "Unicode punctuation must use exact UTF-16LE units");
    Check(row.text == utf8, "Unicode text did not round-trip");
  }
  const auto legacy = NarrowTable("\x85");
  Check(PatchTable(legacy, {{1, "\xE2\x80\xA6"}}).status == PatchStatus::kUnchanged,
        "unchanged original ellipsis must retain its original 0x85 byte");
  const auto changed = Read(PatchTable(legacy, {{1, "\xE2\x80\xA6 Novo"}}).bytes)[0];
  Check(changed.wide && changed.encoded[0] == 0x26 && changed.encoded[1] == 0x20,
        "changed legacy text must encode ellipsis as Unicode");
  const auto space = Read(PatchTable(original, {{1, "Ol\xC3\xA1\xC2\xA0!"}}).bytes)[0];
  Check(!space.wide && space.encoded == std::vector<uint8_t>{'O', 'l', 0xE1, 0xA0, '!', 0},
        "Latin-1 letters and the no-break space must stay narrow");
}
void PackageTests(const std::filesystem::path& root) {
  std::filesystem::create_directories(root / "pt-br-test");
  const auto folder = root / "pt-br-test";
  std::ofstream(folder / "mod.toml")
      << "[mod]\ntype='translation'\nbase_language='en'\nname='Test'\n";
  std::ofstream(folder / "textos.json") << R"({"1992":"OPTIONS TEST"})";
  Package package;
  std::string error;
  Check(LoadPackage(folder, package, error) && package.texts.size() == 1, "package rejected");
  std::ofstream(folder / "mod.toml")
      << "[mod]\ntype='translation'\nbase_language='en'\ntexts='../outside.json'\n";
  Check(!LoadPackage(folder, package, error), "external texts path accepted");
}
void RealTables(const std::filesystem::path& directory, const std::filesystem::path& artifacts) {
  Check(std::filesystem::is_directory(directory), "fixture directory does not exist");
  std::filesystem::create_directories(artifacts / "patched");
  const Dictionary demo{{1992, "OP\xC3\x87\xC3\x95" "ES"},
                        {555, "Legendas"},
                        {2023, "Vibra\xC3\xA7\xC3\xA3o"},
                        {1998, "JOGO PAUSADO"}};
  unsigned files = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (entry.path().extension() != ".txu")
      continue;
    std::ifstream stream(entry.path(), std::ios::binary);
    const std::vector<uint8_t> source(std::istreambuf_iterator<char>(stream), {});
    const auto original = Read(source);
    const auto patched = PatchTable(source, demo);
    Check(patched.status != PatchStatus::kInvalid && patched.rejected.empty(),
          "real table rejected");
    if (patched.status == PatchStatus::kRebuilt) {
      const auto rows = Read(patched.bytes);
      Check(rows.size() == original.size(), "real row count changed");
      for (size_t i = 0; i < rows.size(); ++i) {
        Check(rows[i].id == original[i].id && rows[i].flags == original[i].flags &&
                  rows[i].attributes == original[i].attributes && rows[i].names == original[i].names,
              "real ID, flags, attributes or names changed");
        if (!demo.contains(rows[i].id))
          Check(rows[i].encoded == original[i].encoded, "real untranslated bytes changed");
      }
      std::ofstream output(artifacts / "patched" / entry.path().filename(), std::ios::binary);
      output.write(reinterpret_cast<const char*>(patched.bytes.data()), patched.bytes.size());
      // Every text changed and then restored rebuilds the original bytes.
      Dictionary changed, restored;
      for (const auto& row : original) {
        changed.emplace(row.id, row.text + " ~");  // Cannot extend a tag or a conversion.
        restored.emplace(row.id, row.text);
      }
      const auto all = PatchTable(source, changed);
      Check(all.status == PatchStatus::kRebuilt && all.rejected.empty() &&
                all.replacements == original.size(),
            "real table not fully rebuilt");
      Check(PatchTable(all.bytes, restored).bytes == source, "real table round trip changed bytes");
    }
    ++files;
  }
  Check(files == 6, "expected the six localized fixture tables");
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2)
      throw std::runtime_error("Pass an artifacts directory for synthetic mod fixtures");
    JsonTests();
    TableTests();
    SubstitutionTests();
    EntryFallbackTests();
    TypographyTests();
    PackageTests(argv[1]);
    if (argc >= 3)
      RealTables(argv[2], argv[1]);
    std::cout << "translation PASS checks=" << checks << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "translation FAIL after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
