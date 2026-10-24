// TXU on disk, as sub_8275D500 reads it (all little-endian):
//   u32 version << 24 | count      version 0, 2 or 3 (the game rejects 1 and above 3)
//   u32 pool bytes, then the pool: every string, terminated, in descriptor order
//   count descriptors: u32 id, u32 flags (bit 31 wide, bit 30 attributes), then,
//   when bit 30 is set, attribute words (kind | more << 31) until one lacks bit 31:
//     kind 0: two words, three from version 3; kind 1: the next narrow pool
//     string; kind 2: two floats; other kinds carry nothing.
// The game sizes its own pool from the header and walks it: each entry's text,
// then the strings of its name attributes. Strings have no stored length, so a
// rebuilt table only needs the new pool size.
//
// Keep unchanged/rejected entries, every descriptor word and every name byte for
// byte. Narrow text is decoded as Latin-1 (CP1252 for 0x80-0x9F) for dictionary
// matching only. New text uses ASCII/printable Latin-1 or UTF-16LE, as the
// official tables store typographic punctuation and ligatures as wide strings.
// Do not introduce CP1252 punctuation bytes whose interpretation by the game's
// fonts is unclear.
#include "mods/translation_data.h"

#include <utf8cpp/utf8.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string_view>
#include <toml++/toml.hpp>

namespace broken_bond::translation {
namespace {
constexpr size_t kHeaderBytes = 8;
constexpr uint32_t kCountMask = 0x00FFFFFF;
constexpr uint32_t kWideFlag = 0x80000000;
constexpr uint32_t kAttributesFlag = 0x40000000;
constexpr uint32_t kMoreAttributes = 0x80000000;
constexpr uint32_t kStyleAttribute = 0, kNameAttribute = 1, kRangeAttribute = 2;
constexpr uint32_t kMaxEntries = 65535;

constexpr std::array<char16_t, 32> kCp1252 = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};

uint32_t LE32(std::span<const uint8_t> bytes, size_t at) {
  return uint32_t(bytes[at]) | (uint32_t(bytes[at + 1]) << 8) | (uint32_t(bytes[at + 2]) << 16) |
         (uint32_t(bytes[at + 3]) << 24);
}
void Put32(std::vector<uint8_t>& bytes, uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8)
    bytes.push_back(uint8_t(value >> shift));
}
std::u16string Utf16(std::string_view text) {
  std::u16string out;
  utf8::utf8to16(text.begin(), text.end(), std::back_inserter(out));
  return out;
}
std::string Utf8(const std::u16string& text) {
  std::string out;
  utf8::utf16to8(text.begin(), text.end(), std::back_inserter(out));
  return out;
}
std::string Decode(const TextRow& row) {
  std::u16string text;
  if (row.wide) {
    for (size_t at = 0; at + 2 < row.encoded.size(); at += 2)
      text.push_back(char16_t(row.encoded[at] | (uint16_t(row.encoded[at + 1]) << 8)));
  } else {
    for (size_t at = 0; at + 1 < row.encoded.size(); ++at) {
      const uint8_t value = row.encoded[at];
      text.push_back(value >= 0x80 && value < 0xA0 ? kCp1252[value - 0x80] : char16_t(value));
    }
  }
  return Utf8(text);
}
void Encode(TextRow& row, std::string_view translated, const std::u16string& unicode) {
  std::vector<uint8_t> narrow;
  bool fits = !row.wide;
  if (fits) {
    for (char16_t value : unicode) {
      if (value < 0x80 || (value >= 0xA0 && value <= 0xFF)) {
        narrow.push_back(uint8_t(value));
      } else {
        // Also store C1 controls as wide, avoiding the legacy CP1252 aliases.
        fits = false;
        break;
      }
    }
  }
  row.wide = !fits;
  row.flags = (row.flags & ~kWideFlag) | (row.wide ? kWideFlag : 0);
  row.encoded.clear();
  if (fits) {
    row.encoded = std::move(narrow);
    row.encoded.push_back(0);
  } else {
    for (char16_t value : unicode) {
      row.encoded.push_back(uint8_t(value));
      row.encoded.push_back(uint8_t(value >> 8));
    }
    row.encoded.push_back(0);
    row.encoded.push_back(0);
  }
  row.text = translated;
}
// Keep engine commands and substitutions verbatim. Reordering commands in a
// translation is possible, but their names and multiplicities cannot change.
std::multiset<std::string> Tags(std::string_view text) {
  std::multiset<std::string> tags;
  for (size_t at = 0; (at = text.find('<', at)) != std::string_view::npos;) {
    const size_t end = text.find('>', at + 1);
    if (end == std::string_view::npos)
      break;
    tags.emplace(text.substr(at, end - at + 1));
    at = end + 1;
  }
  return tags;
}
std::multiset<std::string> Substitutions(std::string_view text) {
  std::multiset<std::string> formats;
  // The game's numbered arguments (%1, %2, ...) can change order in a
  // translation, and so can its printf conversions ("Targets left: %d"). A
  // literal percentage ("100% of ...", "%AGE") is not an argument.
  for (size_t at = 0; at + 1 < text.size(); ++at) {
    if (text[at] != '%')
      continue;
    const char next = text[at + 1];
    if (std::string_view("diusfxXc").find(next) != std::string_view::npos) {
      formats.emplace(text.substr(at, 2));
      ++at;
      continue;
    }
    if (next < '1' || next > '9')
      continue;
    size_t end = at + 2;
    while (end < text.size() && text[end] >= '0' && text[end] <= '9')
      ++end;
    formats.emplace(text.substr(at, end - at));
    at = end - 1;
  }
  return formats;
}
bool ReadFile(const std::filesystem::path& path, std::string& text, std::string& error) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec || size > kMaxTableBytes) {
    error = "Cannot read bounded mod file: " + path.string();
    return false;
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    error = "Cannot open mod file: " + path.string();
    return false;
  }
  text.assign(std::istreambuf_iterator<char>(stream), {});
  if (!stream.eof() && stream.fail()) {
    error = "Cannot finish reading mod file";
    return false;
  }
  if (text.size() != size) {
    error = "Mod file changed while being read";
    return false;
  }
  return true;
}

// Reads the next terminated pool string at `at`, as the game's strlen/wcslen do.
std::vector<uint8_t> PoolString(std::span<const uint8_t> pool, size_t& at, bool wide) {
  size_t end = at;
  if (wide) {
    while (end + 1 < pool.size() && (pool[end] | pool[end + 1]))
      end += 2;
    if (end + 1 >= pool.size())
      throw std::runtime_error("TXU wide text is not terminated");
    end += 2;
  } else {
    while (end < pool.size() && pool[end])
      ++end;
    if (end >= pool.size())
      throw std::runtime_error("TXU text is not terminated");
    end += 1;
  }
  std::vector<uint8_t> bytes(pool.begin() + at, pool.begin() + end);
  at = end;
  return bytes;
}

void ReadRows(std::span<const uint8_t> input, std::vector<TextRow>& out, uint32_t& version) {
  if (input.size() < kHeaderBytes || input.size() > kMaxTableBytes)
    throw std::runtime_error("Invalid TXU byte count");
  const uint32_t header = LE32(input, 0);
  const uint32_t count = header & kCountMask;
  const uint32_t pool_bytes = LE32(input, 4);
  version = header >> 24;
  if ((version != 0 && version != 2 && version != 3) || count > kMaxEntries ||
      pool_bytes > input.size() - kHeaderBytes)
    throw std::runtime_error("Unsupported or truncated TXU header");
  const auto pool = input.subspan(kHeaderBytes, pool_bytes);
  size_t pool_at = 0;
  size_t cursor = kHeaderBytes + pool_bytes;
  auto require = [&](size_t bytes) {
    if (bytes > input.size() - cursor)
      throw std::runtime_error("Truncated TXU descriptor");
  };
  std::vector<TextRow> rows;
  std::set<uint32_t> ids;
  rows.reserve(count);
  for (uint32_t index = 0; index < count; ++index) {
    require(8);
    TextRow row;
    row.source_index = index;
    row.id = LE32(input, cursor);
    row.flags = LE32(input, cursor + 4);
    row.wide = (row.flags & kWideFlag) != 0;
    cursor += 8;
    if (!ids.insert(row.id).second)
      throw std::runtime_error("Duplicate TXU text ID");
    row.encoded = PoolString(pool, pool_at, row.wide);
    if (row.flags & kAttributesFlag) {
      const size_t start = cursor;
      for (bool more = true; more;) {
        require(4);
        const uint32_t word = LE32(input, cursor);
        cursor += 4;
        more = (word & kMoreAttributes) != 0;
        switch (word & ~kMoreAttributes) {
          case kStyleAttribute:
            require(version >= 3 ? 12 : 8);
            cursor += version >= 3 ? 12 : 8;
            break;
          case kNameAttribute: {
            const auto name = PoolString(pool, pool_at, false);
            row.names.insert(row.names.end(), name.begin(), name.end());
            break;
          }
          case kRangeAttribute:
            require(8);
            cursor += 8;
            break;
          default:
            break;  // The game skips other kinds without reading any data.
        }
      }
      row.attributes.assign(input.begin() + start, input.begin() + cursor);
    }
    row.text = Decode(row);
    rows.push_back(std::move(row));
  }
  if (pool_at != pool.size())
    throw std::runtime_error("Unaccounted bytes in the TXU pool");
  if (cursor != input.size())
    throw std::runtime_error("Unaccounted bytes after TXU descriptors");
  out = std::move(rows);
}
}  // namespace

bool ValidModId(std::string_view id) {
  return !id.empty() && id.size() <= 80 && std::all_of(id.begin(), id.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '_';
  });
}

bool ParseDictionary(std::string_view text, Dictionary& out, std::string& error) {
  try {
    if (text.size() > kMaxTableBytes)
      throw std::runtime_error("Translation JSON is too large");
    std::set<std::string> keys;
    bool duplicate = false;
    auto callback = [&](int, nlohmann::json::parse_event_t event, nlohmann::json& value) {
      if (event == nlohmann::json::parse_event_t::key &&
          !keys.insert(value.get<std::string>()).second)
        duplicate = true;
      return true;
    };
    auto json = nlohmann::json::parse(text.begin(), text.end(), callback);
    if (duplicate)
      throw std::runtime_error("Duplicate translation ID in JSON");
    if (!json.is_object() || json.size() > kMaxEntries)
      throw std::runtime_error("Expected an object with at most 65535 text IDs");
    Dictionary parsed;
    for (auto it = json.begin(); it != json.end(); ++it) {
      const auto& key = it.key();
      uint32_t id = 0;
      const auto conversion = std::from_chars(key.data(), key.data() + key.size(), id, 10);
      if (conversion.ec != std::errc{} || conversion.ptr != key.data() + key.size() ||
          key.empty() || !it.value().is_string())
        throw std::runtime_error("Expected decimal uint32 IDs and string values");
      const auto value = it.value().get<std::string>();
      if (value.find('\0') != std::string::npos || Utf16(value).size() > kMaxTextUnits)
        throw std::runtime_error("Translation contains NUL or exceeds the text limit");
      if (!parsed.emplace(id, value).second)
        throw std::runtime_error("Text IDs must be unique after decimal conversion");
    }
    out = std::move(parsed);
    return true;
  } catch (const std::exception& exception) {
    error = exception.what();
    return false;
  }
}

bool ReadTable(std::span<const uint8_t> input, std::vector<TextRow>& out, std::string& error) {
  try {
    uint32_t version = 0;
    ReadRows(input, out, version);
    return true;
  } catch (const std::exception& exception) {
    error = exception.what();
    return false;
  }
}

PatchResult PatchTable(std::span<const uint8_t> input, const Dictionary& translations) {
  PatchResult result;
  if (translations.empty())
    return result;
  std::vector<TextRow> rows;
  uint32_t version = 0;
  try {
    ReadRows(input, rows, version);
  } catch (const std::exception& exception) {
    result.status = PatchStatus::kInvalid;
    result.error = exception.what();
    return result;
  }
  try {
    for (auto& row : rows) {
      const auto found = translations.find(row.id);
      if (found == translations.end() || found->second == row.text)
        continue;
      const auto& translated = found->second;
      std::u16string unicode;
      try {
        unicode = Utf16(translated);
      } catch (const utf8::exception&) {
        result.rejected.push_back({row.id, "Invalid UTF-8"});
        continue;
      }
      if (translated.find('\0') != std::string::npos || unicode.size() > kMaxTextUnits) {
        result.rejected.push_back({row.id, "Text contains NUL or exceeds the text limit"});
        continue;
      }
      if (Tags(row.text) != Tags(translated)) {
        result.rejected.push_back({row.id, "Engine tags changed"});
        continue;
      }
      if (Substitutions(row.text) != Substitutions(translated)) {
        result.rejected.push_back({row.id, "Numbered arguments changed"});
        continue;
      }
      Encode(row, translated, unicode);
      ++result.replacements;
    }
    if (result.replacements == 0)
      return result;
    size_t pool_bytes = 0;
    for (const auto& row : rows)
      pool_bytes += row.encoded.size() + row.names.size();
    if (pool_bytes > kMaxTableBytes)
      throw std::runtime_error("Translated TXU exceeds the byte limit");
    Put32(result.bytes, (version << 24) | uint32_t(rows.size()));
    Put32(result.bytes, uint32_t(pool_bytes));
    for (const auto& row : rows) {
      result.bytes.insert(result.bytes.end(), row.encoded.begin(), row.encoded.end());
      result.bytes.insert(result.bytes.end(), row.names.begin(), row.names.end());
    }
    for (const auto& row : rows) {
      Put32(result.bytes, row.id);
      Put32(result.bytes, row.flags);
      result.bytes.insert(result.bytes.end(), row.attributes.begin(), row.attributes.end());
    }
    if (result.bytes.size() > kMaxTableBytes)
      throw std::runtime_error("Translated TXU exceeds the byte limit");
    result.status = PatchStatus::kRebuilt;
  } catch (const std::exception& exception) {
    result.status = PatchStatus::kInvalid;
    result.error = exception.what();
    result.bytes.clear();
    result.replacements = 0;
  }
  return result;
}

bool LoadPackage(const std::filesystem::path& directory, Package& out, std::string& error) {
  try {
    if (!ValidModId(directory.filename().string()))
      throw std::runtime_error("Invalid translation mod folder name");
    std::string manifest;
    if (!ReadFile(directory / "mod.toml", manifest, error))
      return false;
    const auto config = toml::parse(manifest);
    if (config["mod"]["type"].value_or(std::string{}) != "translation" ||
        config["mod"]["base_language"].value_or(std::string{}) != "en")
      throw std::runtime_error("This loader requires type=translation and base_language=en");
    const auto file = config["mod"]["texts"].value_or(std::string("textos.json"));
    if (file.empty() || std::filesystem::path(file).filename() != std::filesystem::path(file) ||
        file == "." || file == "..")
      throw std::runtime_error("The texts file must be inside the mod folder");
    Package parsed;
    parsed.id = directory.filename().string();
    parsed.name = config["mod"]["name"].value_or(parsed.id);
    parsed.version = config["mod"]["version"].value_or(std::string{});
    std::string dictionary;
    if (!ReadFile(directory / file, dictionary, error) ||
        !ParseDictionary(dictionary, parsed.texts, error))
      return false;
    out = std::move(parsed);
    return true;
  } catch (const std::exception& exception) {
    error = exception.what();
    return false;
  }
}
}  // namespace broken_bond::translation
