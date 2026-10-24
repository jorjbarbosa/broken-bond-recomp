// Load a translation package and rebuild the English TXU fixture with the same
// parser/encoder used by the executable, without launching the game.
#include <cstring>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

#include "mods/translation_data.h"

using namespace broken_bond::translation;
namespace {
// The payload of resource AB00018A, the only English table, as extracted from naruto.bf.
constexpr const char* kEnglishTable = "ab00018a.txu";

void Require(bool value, const std::string& message) {
  if (!value)
    throw std::runtime_error(message);
}
std::vector<uint8_t> Read(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  Require(bool(stream), "Cannot open fixture: " + path.string());
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), {});
}
}  // namespace
int main(int argc, char** argv) {
  try {
    // A partial package (a demo, a translation in progress) may leave IDs untranslated.
    const bool partial = argc == 5 && std::strcmp(argv[4], "--partial") == 0;
    Require(argc == 4 || partial,
            "Usage: broken_bond_validate_translation_package <mod folder> <fixture folder> "
            "<artifact folder> [--partial]");
    Package package;
    std::string error;
    Require(LoadPackage(argv[1], package, error), error);
    std::filesystem::create_directories(argv[3]);
    nlohmann::json result;
    result["package"] = package.id;
    result["dictionary_ids"] = package.texts.size();
    result["partial"] = partial;
    const auto source = Read(std::filesystem::path(argv[2]) / kEnglishTable);
    std::vector<TextRow> original;
    Require(ReadTable(source, original, error), error);
    const auto patched = PatchTable(source, package.texts);
    Require(patched.status != PatchStatus::kInvalid, patched.error);
    for (const auto& rejected : patched.rejected)
      std::cerr << "Rejected ID " << rejected.id << ": " << rejected.reason << '\n';
    Require(patched.rejected.empty(), "Package contains rejected translations");
    const auto& bytes = patched.status == PatchStatus::kRebuilt ? patched.bytes : source;
    std::vector<TextRow> rows;
    Require(ReadTable(bytes, rows, error), error);
    Require(rows.size() == original.size(), "Changed row count");
    size_t wide = 0, translated = 0;
    for (size_t index = 0; index < rows.size(); ++index) {
      const auto& row = rows[index];
      const auto& before = original[index];
      const auto found = package.texts.find(row.id);
      Require(partial || found != package.texts.end(),
              "Missing translated ID " + std::to_string(row.id));
      if (found != package.texts.end()) {
        Require(row.text == found->second,
                "Translated text round-trip mismatch " + std::to_string(row.id));
        ++translated;
      }
      Require(row.id == before.id && row.source_index == before.source_index &&
                  (row.flags & 0x7FFFFFFF) == (before.flags & 0x7FFFFFFF) &&
                  row.attributes == before.attributes && row.names == before.names,
              "Changed ID, flags, attributes, names or order");
      if (row.text == before.text)
        Require(row.encoded == before.encoded, "Changed unchanged text bytes");
      wide += row.wide;
    }
    Require(translated == package.texts.size(), "Dictionary contains unused IDs");
    const auto output = std::filesystem::path(argv[3]) / kEnglishTable;
    std::ofstream stream(output, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    Require(bool(stream), "Cannot write rebuilt fixture");
    result["tables"][kEnglishTable] = {{"entries", rows.size()},
                                       {"translated_ids", translated},
                                       {"replacements", patched.replacements},
                                       {"original_bytes", source.size()},
                                       {"translated_bytes", bytes.size()},
                                       {"wide_texts", wide}};
    result["valid"] = true;
    std::ofstream report(std::filesystem::path(argv[3]) / "txu-validation.json");
    report << result.dump(2) << '\n';
    Require(bool(report), "Cannot write validation report");
    std::cout << result.dump(2) << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Translation package FAIL: " << error.what() << '\n';
    return 1;
  }
}
