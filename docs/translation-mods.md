# Translation mods

[Documentation](README.md) · [Configuration](configuration.md)

Translation mods replace individual text IDs when the game loads its English text table. The loader builds a temporary table in guest memory; the game copies it into its own text pool. `naruto.bf` stays unchanged, and IDs missing from the mod keep their original text.

No complete translation is bundled yet. The repository has a small Portuguese demo to try the system.

## Try the Portuguese demo

Copy [examples/mods/pt-br-demo](../examples/mods/pt-br-demo) into `mods/pt-br-demo` in the data folder (`data\` beside `broken_bond.exe`, `build/data/` for a build in the repository, `~/.local/share/broken_bond/` for the AppImage; **Settings → Advanced → Open data folder** opens it):

```text
data/
  broken_bond.toml
  mods/
    pt-br-demo/
      mod.toml
      textos.json
```

Open **Settings** on the setup screen, or in the game menu (F1 or Back + Start), and choose the demo in **Settings → Game → Game language**. Valid packages in `mods/` appear by their manifest name, without added labels. Copying a package makes it available; only selecting it activates it. The list is read each time the page opens.

Selecting a translation automatically uses English as its base. Selecting any official language disables the translation. The selection applies at the next start. If the selected package is missing or invalid, it is not listed and the menu shows English; the game keeps its original texts and logs why. Restoring the package makes it available again. The menus check the package structure and JSON, while engine tags and arguments are checked by the game when it loads the table.

For manual configuration, close the game and set these keys in the data folder's `broken_bond.toml`:

```toml
user_language = 1
translation_mod = "pt-br-demo"
```

Restart the game after editing either the configuration or the mod. Set `translation_mod = ""` to disable it. The demo translates ten IDs covering four labels: **OPTIONS → OPÇÕES**, **Subtitles → Legendas**, **Vibration → Vibração** and **GAME PAUSED → JOGO PAUSADO**, in both their capitalized and title-case entries. It is a small test package, not a complete Portuguese translation. Its accented letters check the game's fonts: `ç` appears in the official French table, but `ã` and `õ` appear in no official table.

## Create a package

Use a folder name containing only letters, digits, hyphens or underscores, with at most 80 characters. Only one translation package can be selected at a time. Its `mod.toml` declares the English base and the JSON filename:

```toml
[mod]
type = "translation"
name = "My translation"
version = "0.1.0"
base_language = "en"
texts = "textos.json"
```

Save `textos.json` as UTF-8, with or without a BOM. Keys are decimal text IDs, and values are translated strings:

```json
{
  "1992": "OPÇÕES",
  "555": "Legendas",
  "1998": "JOGO PAUSADO"
}
```

The same label often has several IDs, one per screen; translate each one you want changed.

Preserve engine commands such as `<br>`, `<white>`, `<lt>`, `<@ItemName>` and their closing tags. Preserve the numbered arguments `%1`, `%2`, etc., including repeated occurrences, and printf conversions such as `%d`; their order may change to suit the translation. For example, `Unlock %1 in exchange for %2 Gold Coins?` can become `Por %2 moedas de ouro, desbloquear %1?`. A percentage such as `100%` remains ordinary text.

Strings may grow: the table stores no lengths, and the loader recalculates the size of its string pool. ASCII and printable Latin-1, including Portuguese accents, use one byte where possible. Typographic quotes, dashes, ellipses and other characters outside that range use UTF-16, as the official tables do; existing wide entries remain wide. Unchanged original text retains its exact bytes. Glyph availability and text fitting still depend on the game's fonts and layout. A JSON string cannot contain a NUL character or exceed 65,535 UTF-16 units. Files and rebuilt tables are limited to 16 MiB.

The loader preserves text IDs, flags, the style and name attributes of each entry, table order, and the encoded bytes of untranslated entries. Invalid JSON or a missing package leaves the original texts active. An entry with changed engine tags or arguments keeps only that ID's original text; other valid translations in the table are applied. Each rejected ID and its reason are logged. Structural table errors or a rebuilt table exceeding the byte limit still retain the entire original table.

## Validate a translation

Compare the finished dictionary with the original ID/text catalog before packaging:

```powershell
python -I tools/localization/validate-translation.py path/to/english-texts.json path/to/mod/textos.json --report out/translation-validation.json
```

The validator rejects missing or unexpected IDs, duplicate JSON keys, empty translations, invalid Unicode, oversized strings, and changed engine tokens or arguments. It also reports unchanged strings and translations that differ for identical original text. Those candidates require contextual judgment; names and vocalizations can legitimately remain unchanged. A passing structural check does not establish translation quality or text fit in the game.

To validate a package with the same table reader and encoder as the game, build and run the CMake validator:

```powershell
cmake --build build/native --target broken_bond_validate_translation_package
build/native/broken_bond_validate_translation_package.exe mods/my-translation path/to/fixtures out/package-validation
```

The fixture directory must contain `ab00018a.txu`, the payload of the English table `AB00018A` (in the archive bundle `_fd190232.bin` of `naruto.bf`). The validator checks every ID and the rebuilt bytes, writes the rebuilt table and `txu-validation.json`, and fails if any entry is rejected or if the dictionary has IDs the table lacks. By default it also requires every ID of the table, as for a complete translation; add `--partial` for a package that translates only some texts, such as the demo. Game fixtures are not shipped in the repository.

The JSON parser is pinned in `third_party/nlohmann_json` and shared through the `broken_bond::json` CMake target, independently of the SDK's private inja headers.

## Scope and diagnostics

The loader targets the English table `AB00018A` (14,001 entries), the only English text table in the archive. Set the game language to English (`user_language = 1`); another language leaves the mod inactive. Text baked into textures, voices and fonts need separate support.

Logs prefixed with `[translation]` report package loading, each rejected ID, the table replacement count, and release of the temporary input. Loading a package alone does not prove that every supplied ID was found or that every translated screen has been visited.

The table reader can be checked without launching the game:

```powershell
cmake --build build/native --target broken_bond_translation_test
build/native/broken_bond_translation_test.exe out/translation-tests
```

This runs synthetic tests for table rebuilding, encoding, invalid input, package paths and engine arguments. An optional second argument supplies a directory with the extracted tables (`ab00018a.txu` to `ab00018f.txu`, the five languages and the empty sixth table) for byte-preservation checks. Game assets and these fixtures are not distributed with the mod.
