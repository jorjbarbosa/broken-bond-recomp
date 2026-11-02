// The configuration file (src/menu/config_manager.h): load and save, the
// settings that survive a save, malformed files, the renderer migrations and
// the API under Wine.
#include "menu/config_manager.h"
#include "wine.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <toml++/toml.hpp>

namespace fs = std::filesystem;

void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void Write(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path);
  out << text;
  Require(out.good(), "Could not write test fixture");
}

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const auto root = fs::absolute(argv[1]) /
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto path = root / "settings" / "broken_bond.toml";
    Write(path, "user_language = 4\nupdate_data_root = 'D:/Title Update'\n");
    GameConfig config;
    Require(ConfigManager::Load(path, config), "Configuration failed to load");
    Require(config.user_language == 4, "Game language was not loaded");
    config.mnk_mode = true;
    config.fullscreen = false;
    config.resolution_scale = 2;
    config.anisotropic_override = -1;
    config.custom_game_root = "D:/Game Files/Broken Bond";
    Require(ConfigManager::Save(path, config), "Configuration failed to save");
    GameConfig reloaded;
    Require(ConfigManager::Load(path, reloaded), "Configuration failed to reload");
    Require(reloaded.user_language == 4 && reloaded.mnk_mode && !reloaded.fullscreen &&
                reloaded.resolution_scale == 2 && reloaded.anisotropic_override == -1 &&
                reloaded.custom_game_root == config.custom_game_root,
            "Settings did not persist");
    auto table = toml::parse_file(path.string());
    Require(table["update_data_root"].value<std::string>() == "D:/Title Update",
            "Saving erased an unmanaged setting");
    Require(table["draw_resolution_scale_x"].value<int>() == 2 &&
                table["draw_resolution_scale_y"].value<int>() == 2,
            "Resolution axes do not match");
    config.custom_game_root.clear();
    Require(ConfigManager::Save(path, config), "Path reset failed");
    Require(!toml::parse_file(path.string()).contains("custom_game_root"),
            "Path reset did not remove the override");
    const std::string malformed = "user_language = [\n";
    Write(path, malformed);
    Require(!ConfigManager::Load(path, config), "Malformed TOML was accepted");
    Require(!ConfigManager::Save(path, config), "Malformed user config was overwritten");
    std::ifstream preserved(path);
    Require(std::string(std::istreambuf_iterator<char>(preserved), {}) == malformed,
            "Malformed config was not preserved");
    preserved.close();
    Require(!ConfigManager::Save(root / "absent" / "settings.toml", config),
            "An unwritable destination reported success");

    // Crash reports: off by default and in older files, then the player's
    // choice survives a save, and so does turning them off.
    const auto reporting_path = root / "settings" / "reporting.toml";
    Write(reporting_path, "personal_setting = 'keep'\n");
    GameConfig reporting;
    Require(!reporting.crash_reporting, "Crash reporting requires explicit consent");
    Require(ConfigManager::Load(reporting_path, reporting) && !reporting.crash_reporting,
            "Existing configurations must not opt players into reporting");
    reporting.crash_reporting = true;
    Require(ConfigManager::Save(reporting_path, reporting), "Could not persist reporting consent");
    GameConfig opted_in;
    Require(ConfigManager::Load(reporting_path, opted_in) && opted_in.crash_reporting,
            "Reporting consent did not survive reload");
    Require(toml::parse_file(reporting_path.string())["personal_setting"].value<std::string>() == "keep",
            "Saving the reporting consent erased an unmanaged setting");
    opted_in.crash_reporting = false;
    Require(ConfigManager::Save(reporting_path, opted_in) && ConfigManager::Load(reporting_path, opted_in) &&
                !opted_in.crash_reporting,
            "Reporting revocation did not survive reload");
    Require(!opted_in.crash_reporting_asked, "The first-start question must not count as answered");
    opted_in.crash_reporting_asked = true;
    Require(ConfigManager::Save(reporting_path, opted_in) && ConfigManager::Load(reporting_path, opted_in) &&
                opted_in.crash_reporting_asked && !opted_in.crash_reporting,
            "The answer to the first-start question did not survive reload");

    Write(path, "renderer = 'native'\nuser_data_root = 'my saves'\nkeybind_a = 'J,K,L'\n"
                "ultrawide_target_aspect = 2.3889\n");
    GameConfig options;
    Require(options.renderer == "native" && options.frame_rate == 30 && options.aspect_ratio == -1.0,
            "Native defaults changed");
    Require(ConfigManager::Load(path, options), "Extended settings failed to load");
    Require(options.key_binds[0] == "J,K,L", "Loading truncated hand-written key alternatives");
    options.graphics_device = "Test graphics adapter";
    options.native_backend = kNativeVulkanBuilt ? "vulkan" : "d3d12";
    options.launcher_language = "pt_BR";
    options.mnk_mouse = false;
    options.mnk_sensitivity = 1.75;
    options.skip_intro_videos = false;
    options.show_fps_overlay = true;
    options.key_binds[1] = ",Q";
    options.msaa_samples = 8;
    options.frame_rate = 120;
    options.brightness = 0.6;
    options.internal_resolution_scale = 1.5;
    options.max_frame_latency = 3;
    options.triple_buffering = true;
    options.skip_pipeline_draws = true;
    options.transparency_antialiasing = true;
    options.aspect_ratio = 2.3889;
    Require(ConfigManager::Save(path, options), "Extended settings failed to save");
    GameConfig restored;
    Require(ConfigManager::Load(path, restored), "Extended settings failed to reload");
    Require(restored.graphics_device == options.graphics_device && restored.launcher_language == "pt_BR" &&
                !restored.mnk_mouse && restored.mnk_sensitivity == 1.75 && !restored.skip_intro_videos &&
                restored.show_fps_overlay && restored.key_binds[1] == ",Q" && restored.key_binds[0] == "J,K,L" &&
                restored.user_data_root == "my saves", "Extended settings did not persist");
    const auto extended = toml::parse_file(path.string());
    Require(!extended.contains("ultrawide_target_aspect"), "Saving kept the disabled aspect option");
    if (kNativeRendererBuilt) {
      Require(restored.msaa_samples == 8 && restored.frame_rate == 120 && restored.brightness == 0.6 &&
                  restored.native_backend == options.native_backend &&
                  restored.internal_resolution_scale == 1.5 && restored.max_frame_latency == 3 &&
                  restored.triple_buffering && restored.skip_pipeline_draws &&
                  restored.transparency_antialiasing && restored.aspect_ratio == 2.3889,
              "Native renderer options did not persist");
    }
    Write(path, "renderer = 'xenos'\ngpu_plugin = 'xenos'\nnative_backend = 'vulkan'\nuser_language = 4\nuser_data_root = 'my saves'\n");
    GameConfig migrated;
    Require(ConfigManager::Load(path, migrated) && migrated.renderer == "native" &&
                migrated.native_backend == (kNativeVulkanBuilt ? "vulkan" : "d3d12"),
            "Legacy renderer selection was not migrated to a compiled native API");
    Require(ConfigManager::Save(path, migrated), "Migrated settings failed to save");
    const auto migrated_table = toml::parse_file(path.string());
    Require(migrated_table["renderer"].value<std::string>() == "native" &&
                !migrated_table.contains("gpu_plugin") && migrated.user_language == 4 &&
                migrated.user_data_root == "my saves", "Migration lost settings or retained the GPU plugin");
    Write(path, "renderer = 'xenos'\nnative_backend = 'invalid'\ngraphics_device = 'stale device'\n");
    Require(ConfigManager::Load(path, migrated) && migrated.graphics_device.empty() &&
                migrated.native_backend == (kNativeD3d12Built ? "d3d12" : "vulkan"),
            "An unavailable API or its device selection survived migration");
    migrated.renderer = "xenos";
    migrated.native_backend = "invalid";
    Require(ConfigManager::Save(path, migrated), "Saving stale settings failed");
    Require(toml::parse_file(path.string())["renderer"].value<std::string>() == "native",
            "Save accepted a non-native renderer");

    Write(path, "renderer = 'native'\nnative_backend = 'vulkan'\ngraphics_device = 'Vulkan adapter'\n"
                "user_language = 4\nuser_data_root = 'my saves'\n");
    Require(ConfigManager::Load(path, migrated), "Previous native configuration failed to load");
    Require(migrated.native_backend == (kNativeVulkanBuilt ? "vulkan" : "d3d12") &&
                migrated.graphics_device == (kNativeVulkanBuilt ? "Vulkan adapter" : "") &&
                migrated.user_language == 4 && migrated.user_data_root == "my saves",
            "A saved Vulkan choice was not kept, or migrated, with the user preferences");
    Require(ConfigManager::Save(path, migrated), "Saved Vulkan settings failed to save");
    Require(toml::parse_file(path.string())["native_backend"].value<std::string>() ==
                (kNativeVulkanBuilt ? "vulkan" : "d3d12"),
            "The saved Vulkan choice was not written back as the built API allows");

    Require(broken_bond::WineVersion() == nullptr, "Wine detected on Windows");
    GameConfig wine_config;
    wine_config.native_backend = "d3d12";
    wine_config.graphics_device = "D3D12 adapter";
    ConfigManager::UseVulkanUnderWine(wine_config, false);
    Require(wine_config.native_backend == "d3d12" && wine_config.graphics_device == "D3D12 adapter",
            "The API changed without Wine");
    ConfigManager::UseVulkanUnderWine(wine_config, true);
    Require(wine_config.native_backend == (kNativeVulkanBuilt ? "vulkan" : "d3d12") &&
                wine_config.graphics_device == (kNativeVulkanBuilt ? "" : "D3D12 adapter"),
            "Under Wine the game did not select Vulkan or kept the D3D12 card");
    if (kNativeVulkanBuilt) {
      wine_config.graphics_device = "Vulkan adapter";
      ConfigManager::UseVulkanUnderWine(wine_config, true);
      Require(wine_config.graphics_device == "Vulkan adapter", "Under Wine a saved Vulkan card was cleared");
    }
    std::cout << "Configuration load, save, migrations and the API under Wine passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
