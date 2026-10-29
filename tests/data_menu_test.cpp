// The Data folder rows of Advanced over the real menu documents and a CPU
// drawer: edits go to the active .toml under a Unicode path, the mods come
// from the active data folder, the location actions ask for their mode, a
// pending location shows beside the active one without moving any write, an
// explicit saves folder shows on its own row, and a session override blocks
// every change; the texts and Advanced's section headings exist in every
// menu language.
#include "menu/ui.h"
#include "menu/settings.h"
#include "menu/game_choice.h"
#include "menu/text.h"
#include "data_paths.h"
#include <RmlUi/Core.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/ui_event.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <toml++/toml.hpp>
#include <SDL3/SDL.h>
namespace fs = std::filesystem;
namespace {
int checks = 0;
void Check(bool value, const char* message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
void Write(const fs::path& path, const std::string& value) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << value;
  if (!output) throw std::runtime_error("Fixture write failed");
}
std::string Read(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Fixture read failed");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
struct Texture : rex::ui::ImmediateTexture {
  Texture(uint32_t w, uint32_t h) : ImmediateTexture(w, h) {}
};
struct CpuDrawer : rex::ui::ImmediateDrawer {
  std::unique_ptr<rex::ui::ImmediateTexture> CreateTexture(uint32_t w, uint32_t h, rex::ui::ImmediateTextureFilter,
                                                           bool, const uint8_t*) override {
    return std::make_unique<Texture>(w, h);
  }
  void BeginDrawBatch(const rex::ui::ImmediateDrawBatch&) override {}
  void Draw(const rex::ui::ImmediateDraw&) override {}
  void EndDrawBatch() override {}
};
Rml::Element* Element(const char* id) {
  auto* context = Rml::GetContext("game");
  for (int i = 0; context && i < context->GetNumDocuments(); ++i) {
    auto* document = context->GetDocument(i);
    if (document->IsVisible())
      if (auto* element = document->GetElementById(id)) return element;
  }
  return nullptr;
}
}  // namespace
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const auto root = fs::absolute(fs::path(std::u8string(argv[1], argv[1] + std::char_traits<char>::length(argv[1]))));
    Check(!fs::exists(root), "Fixture must be new");
    for (const char* hint : {SDL_HINT_JOYSTICK_HIDAPI, SDL_HINT_JOYSTICK_DIRECTINPUT, SDL_HINT_JOYSTICK_RAWINPUT,
                             SDL_HINT_JOYSTICK_WGI, SDL_HINT_JOYSTICK_GAMEINPUT, SDL_HINT_XINPUT_ENABLED})
      SDL_SetHintWithPriority(hint, "0", SDL_HINT_OVERRIDE);
    const auto install = root / "install";
    const auto active = root / fs::path(u8"data 日本語/broken_bond.toml");
    const std::string legacy = "unknown_option = 42\nshow_startup_menu = false\n";
    Write(install / "broken_bond.toml", legacy);
    Write(active, "unknown_option = 84\nshow_startup_menu = false\nlauncher_language = 'en'\n");
    CpuDrawer drawer;
    broken_bond::menu::Ui ui;
    Check(ui.Initialize(drawer, nullptr), "Real menu documents initialize without a GPU");
    rex::ui::AppUIDrawContext context(1280, 720);
    const auto frame = [&] { ui.Draw(context); };
    auto* rml = Rml::GetContext("game");
    const auto press = [&](Rml::Input::KeyIdentifier key) {
      rml->ProcessKeyDown(key, 0);
      rml->ProcessKeyUp(key, 0);
      frame();
    };
    broken_bond::menu::Settings settings;
    settings.Initialize(install, false, active);
    settings.Open(ui, 4, false, {}, {}, {});
    frame();
    auto* startup = Element("show_startup_menu");
    Check(startup && startup->Focus(), "Advanced option receives keyboard focus");
    press(Rml::Input::KI_RIGHT);
    toml::table table;
    { std::ifstream input(active, std::ios::binary); table = toml::parse(input); }
    Check(table["show_startup_menu"].value<bool>() == true && table["unknown_option"].value<int>() == 84,
          "UI edit writes the active Unicode .toml and keeps unknown keys");
    Check(Read(install / "broken_bond.toml") == legacy, "The executable-side .toml stays byte-identical");
    settings.Reload();
    Check(settings.config().show_startup_menu, "Reload reads the same active .toml");
    std::string save_error;
    Check(broken_bond::menu::SaveGameRoot(active, root / "external game.iso", save_error),
          "The game choice writes the active Unicode .toml");
    settings.Reload();
    Check(!settings.config().custom_game_root.empty() && Read(install / "broken_bond.toml") == legacy,
          "The game choice's reload keeps the executable-side .toml");

    broken_bond::data::DataPaths storage;
    storage.data_root = active.parent_path();
    storage.distribution_dir = install;
    storage.mods_dir = storage.data_root / "mods";
    Write(storage.mods_dir / "pt-br/mod.toml", "[mod]\ntype='translation'\nbase_language='en'\nname='Active translation'\n");
    Write(storage.mods_dir / "pt-br/textos.json", R"({"1992":"ACTIVE MOD"})");
    Write(install / "mods/pt-br/mod.toml", "[mod]\ntype='translation'\nbase_language='en'\nname='Installation decoy'\n");
    Write(install / "mods/pt-br/textos.json", R"({"1992":"WRONG MOD"})");
    int location_requests = 0;
    auto last_mode = broken_bond::data::Mode::kDefault;
    auto choose = [&](broken_bond::data::Mode mode) { ++location_requests; last_mode = mode; };
    settings.SetDataFolder(storage, {}, choose);
    settings.Open(ui, 0, false, {}, {}, {});
    frame();
    auto* language = Element("user_language");
    Check(language && language->Focus(), "Game language receives focus");
    for (int i = 0; i < 8 && settings.config().translation_mod.empty(); ++i) press(Rml::Input::KI_RIGHT);
    Check(settings.config().translation_mod == "pt-br" &&
          Element("user_language")->GetInnerRML().find("Active translation") != std::string::npos &&
          Element("user_language")->GetInnerRML().find("Installation decoy") == std::string::npos,
          "The menu lists only the active data folder's mod, not the installation's");
    settings.Reload();
    { std::ifstream input(active, std::ios::binary); table = toml::parse(input); }
    Check(table["translation_mod"].value<std::string>() == "pt-br" && table["unknown_option"].value<int>() == 84 &&
          settings.config().translation_mod == "pt-br",
          "The selected translation persists in the active .toml and its reload");
    settings.SetPendingDataLocation(root / "next location");
    settings.Open(ui, 0, false, {}, {}, {});
    frame();
    Check(Element("user_language")->GetInnerRML().find("Active translation") != std::string::npos,
          "A pending location keeps the menu's mods on the session's folder");

    settings.SetPendingDataLocation({});
    settings.Open(ui, 4, false, {}, {}, {});
    frame();
    Check(Element("data-active") && Element("data-open") && !Element("data-guest") && !Element("data-pending"),
          "Advanced shows the active folder and its Open action, no saves folder of its own and nothing pending");
    const auto activate = [&](const char* id) {
      auto* option = Element(id);
      Check(option && option->Focus(), "Storage action is keyboard-focusable");
      press(Rml::Input::KI_RETURN);
    };
    activate("data-choose");
    Check(location_requests == 1 && last_mode == broken_bond::data::Mode::kCustom, "Choose asks for a custom folder");
    activate("data-portable");
    Check(location_requests == 2 && last_mode == broken_bond::data::Mode::kPortable, "Portable asks for the portable folder");
    activate("data-default");
    Check(location_requests == 3 && last_mode == broken_bond::data::Mode::kDefault, "Restore asks for the default folder");
    const auto next = root / "next location";
    settings.SetPendingDataLocation(next);
    frame();
    Check(Element("data-pending") && Element("data-pending")->IsClassSet("modified") &&
          Element("data-pending")->GetInnerRML().find("next location") != std::string::npos &&
          Element("data-active")->GetInnerRML().find("data") != std::string::npos,
          "The pending and active locations show separately");
    auto* toggle = Element("show_startup_menu");
    Check(toggle && toggle->Focus(), "Settings stay editable while a location is pending");
    press(Rml::Input::KI_RIGHT);
    { std::ifstream input(active, std::ios::binary); table = toml::parse(input); }
    Check(table["show_startup_menu"].value<bool>() == false && !fs::exists(next / "broken_bond.toml"),
          "A pending location keeps the writes in the active .toml until the restart");

    storage.user_source = broken_bond::data::Source::kConfig;
    storage.user_data_root = root / "explicit saves";
    settings.SetDataFolder(storage, {}, choose);
    settings.Open(ui, 4, false, {}, {}, {});
    frame();
    Check(Element("data-guest") && Element("data-guest")->GetInnerRML().find("explicit saves") != std::string::npos,
          "An explicit saves folder shows on its own row");
    storage.session_override = true;
    settings.SetDataFolder(storage, {}, choose);
    settings.Open(ui, 4, false, {}, {}, {});
    frame();
    activate("data-choose");
    activate("data-portable");
    activate("data-default");
    Check(location_requests == 3 && !Element("data-pending"), "A session override blocks every location action");

    const auto sections = [&] {
      std::string text;
      for (const char* id : {"show_startup_menu", "show_fps_overlay", "max_frame_latency", "data-active"})
        if (auto* row = Element(id); row && row->GetPreviousSibling()) text += row->GetPreviousSibling()->GetInnerRML() + "|";
      return text;
    };
    for (const auto& option : Localization::GetLanguages()) {
      Localization::SetLanguage(option.language);
      settings.SetPendingDataLocation({});
      frame();
      const auto expected = Rml::StringUtilities::EncodeRml(broken_bond::menu::Text("storage-choose"));
      Check(!expected.empty() && Element("data-choose")->GetInnerRML().find(expected) != std::string::npos &&
            *broken_bond::menu::Text("storage-error") && *broken_bond::menu::Text("storage-error-help") &&
            *broken_bond::menu::Text("storage-override") && *broken_bond::menu::Text("storage-guest-help") &&
            *broken_bond::menu::Text("storage-pending-help"),
            "The location actions and errors exist in this language");
      const auto headings = sections();
      for (const char* key : {"general-section", "overlay-section", "performance-section", "storage-section"})
        Check(*broken_bond::menu::Text(key) &&
              headings.find(Rml::StringUtilities::EncodeRml(broken_bond::menu::Text(key))) != std::string::npos,
              "Advanced's sections head their rows in this language");
    }
    Localization::SetLanguage(Localization::Language::English);
    ui.HidePanel();
    ui.Shutdown();
    std::cout << "PASS: " << checks << " checks; the active .toml, the active mods, the location actions, "
                 "pending, saves folder, override, texts\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
