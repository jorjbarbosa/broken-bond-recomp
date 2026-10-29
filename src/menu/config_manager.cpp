#include "config_manager.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <algorithm>
#include <cmath>
#include <fstream>
#include <toml++/toml.hpp>

void ConfigManager::NormalizeRenderer(GameConfig& config) {
  config.renderer = "native";
  if ((config.native_backend == "d3d12" && kNativeD3d12Built) ||
      (config.native_backend == "vulkan" && kNativeVulkanBuilt)) return;
  config.native_backend = kNativeD3d12Built ? "d3d12" : "vulkan";
  config.graphics_device.clear();
}

void ConfigManager::UseVulkanUnderWine(GameConfig& config, bool under_wine) {
  if (!kNativeVulkanBuilt || !under_wine || config.native_backend == "vulkan") return;
  config.native_backend = "vulkan";
  config.graphics_device.clear();  // Device names are enumerated separately by each API.
}

bool ConfigManager::Load(const std::filesystem::path& toml_path, GameConfig& out) {
  // Loaded as a whole: a refused or partial file keeps the last effective state.
  GameConfig config = out;
  try {
    std::ifstream input(toml_path);
    if (!input) return false;
    const auto tbl = toml::parse(input);
    if (auto v = tbl["graphics_device"].value<std::string>()) config.graphics_device = *v;
    if (auto v = tbl["input_backend"].value<std::string>()) config.input_backend = *v;
    if (auto v = tbl["swap_post_effect"].value<std::string>()) config.swap_post_effect = *v;
    if (auto v = tbl["present_effect"].value<std::string>()) config.present_effect = *v;
    if (auto v = tbl["custom_game_root"].value<std::string>()) config.custom_game_root = *v;
    if (auto v = tbl["launcher_language"].value<std::string>()) config.launcher_language = *v;
    if (auto v = tbl["user_language"].value<std::uint32_t>()) config.user_language = *v;
    if (auto v = tbl["translation_mod"].value<std::string>()) config.translation_mod = *v;
    if (auto v = tbl["skip_intro_videos"].value<bool>()) config.skip_intro_videos = *v;
    if (auto v = tbl["mnk_mode"].value<bool>()) config.mnk_mode = *v;
    if (auto v = tbl["mnk_mouse"].value<bool>()) config.mnk_mouse = *v;
    if (auto v = tbl["fullscreen"].value<bool>()) config.fullscreen = *v;
    if (auto v = tbl["vsync"].value<bool>()) config.vsync = *v;
    if (auto v = tbl["d3d12_host_vsync"].value<bool>()) config.d3d12_host_vsync = *v;
    if (auto v = tbl["d3d12_allow_variable_refresh_rate_and_tearing"].value<bool>()) config.d3d12_allow_variable_refresh_rate_and_tearing = *v;
    if (auto v = tbl["present_dither"].value<bool>()) config.present_dither = *v;
    if (auto v = tbl["show_fps_overlay"].value<bool>()) config.show_fps_overlay = *v;
    if (auto v = tbl["show_startup_menu"].value<bool>()) config.show_startup_menu = *v;
    if (auto v = tbl["crash_reporting"].value<bool>()) config.crash_reporting = *v;
    if (auto v = tbl["mnk_sensitivity"].value<double>()) if (std::isfinite(*v)) config.mnk_sensitivity = *v;
    if (auto v = tbl["video_mode_refresh_rate"].value<double>()) if (std::isfinite(*v)) config.video_mode_refresh_rate = *v;
    if (auto v = tbl["aspect_ratio"].value<double>()) if (std::isfinite(*v)) config.aspect_ratio = *v;
    if (auto v = tbl["anisotropic_override"].value<int>()) config.anisotropic_override = *v;
    if (auto v = tbl["msaa_samples"].value<int>()) config.msaa_samples = *v;
    if (auto v = tbl["frame_rate"].value<int>()) config.frame_rate = *v;
    if (auto v = tbl["max_frame_latency"].value<int>()) config.max_frame_latency = *v;
    if (auto v = tbl["transparency_antialiasing"].value<bool>()) config.transparency_antialiasing = *v;
    if (auto v = tbl["triple_buffering"].value<bool>()) config.triple_buffering = *v;
    if (auto v = tbl["brightness"].value<double>()) if (std::isfinite(*v)) config.brightness = *v;
    if (auto v = tbl["internal_resolution_scale"].value<double>()) if (std::isfinite(*v)) config.internal_resolution_scale = *v;
    if (auto v = tbl["renderer"].value<std::string>()) config.renderer = *v == "native" ? "native" : "xenos";
    if (auto v = tbl["native_backend"].value<std::string>()) config.native_backend = *v;
    if (auto v = tbl["user_data_root"].value<std::string>()) config.user_data_root = *v;
    if (auto v = tbl["resolution_scale"].value<int>()) {
      config.resolution_scale = config.draw_resolution_scale_x = config.draw_resolution_scale_y = *v;
    }
    for (std::size_t i = 0; i < kKeyActionCount; ++i) {
      if (auto v = tbl[KeyActionOption(i)].value<std::string>()) config.key_binds[i] = *v;
    }
    using namespace broken_bond::controls;
    // Player one starts from the legacy keyboard and mouse settings.
    config.controls = {};
    config.controls.ports[0].keys = config.key_binds;
    config.controls.ports[0].mouse = config.mnk_mouse;
    config.controls.ports[0].sensitivity = std::clamp(config.mnk_sensitivity, .01, 10.0);
    if (config.mnk_mouse) {
      constexpr const char* motion[] = {"MouseUp","MouseDown","MouseLeft","MouseRight"};
      for (int i = 0; i < 4; ++i) config.controls.ports[0].keys[16+i] += std::string(",") + motion[i];
    }
    if (auto* controls = tbl["controls"].as_table()) {
      config.controls.version = (*controls)["version"].value_or(1);
      for (size_t i = 0; i < kPorts; ++i) if (auto* port = (*controls)["player" + std::to_string(i+1)].as_table()) {
        auto& p = config.controls.ports[i];
        p.device = (*port)["device"].value_or(std::string{});
        p.device_name = (*port)["device_name"].value_or(std::string{});
        p.mouse = (*port)["mouse"].value_or(true);
        p.deadzones = (*port)["deadzones"].value_or(true);
        p.sensitivity = (*port)["sensitivity"].value_or(1.0);
        for (size_t a = 0; a < kKeyActionCount; ++a) {
          if (auto s = (*port)["pad"][KeyActionOption(a)].value<std::string>()) p.pad[a] = Decode(*s);
          if (auto s = (*port)["keys"][KeyActionOption(a)].value<std::string>()) p.keys[a] = *s;
        }
        constexpr const char* axes[] = {"left_stick", "right_stick", "left_trigger", "right_trigger"};
        for (size_t a = 0; a < 4; ++a) p.deadzone[a] = (*port)[axes[a]].value_or(p.deadzone[a]);
        p.rumble[0] = (*port)["rumble_low"].value_or(1.0); p.rumble[1] = (*port)["rumble_high"].value_or(1.0);
      }
    }
    if (!Valid(config.controls)) return false;
    NormalizeRenderer(config);
    out = std::move(config);
    return true;
  } catch (const toml::parse_error&) {
    return false;
  }
}

bool ConfigManager::Save(const std::filesystem::path& toml_path, const GameConfig& requested) {
  using namespace broken_bond::controls;
  if (!Valid(requested.controls)) return false;
  auto config = requested;
  NormalizeRenderer(config);
  toml::table tbl;
  std::error_code ec;
  const bool exists = std::filesystem::exists(toml_path, ec);
  if (ec) return false;
  if (exists) {
    try {
      std::ifstream input(toml_path);
      if (!input) return false;
      tbl = toml::parse(input);
      if (tbl["controls"]["version"].value_or(1) > 1) return false;  // A newer layout: keep it.
    } catch (...) {
      // Preserve unreadable or malformed user configuration for manual repair.
      return false;
    }
  }
  tbl.erase("gpu_plugin");
  tbl.insert_or_assign("graphics_device", config.graphics_device);
  tbl.insert_or_assign("input_backend", config.input_backend);
  tbl.insert_or_assign("swap_post_effect", config.swap_post_effect);
  tbl.insert_or_assign("present_effect", config.present_effect);
  tbl.insert_or_assign("launcher_language", config.launcher_language);
  tbl.insert_or_assign("user_language", config.user_language);
  tbl.insert_or_assign("translation_mod", config.translation_mod);
  tbl.insert_or_assign("skip_intro_videos", config.skip_intro_videos);
  tbl.insert_or_assign("mnk_mode", config.mnk_mode);
  tbl.insert_or_assign("mnk_mouse", config.mnk_mouse);
  tbl.insert_or_assign("fullscreen", config.fullscreen);
  tbl.insert_or_assign("vsync", config.vsync);
  tbl.insert_or_assign("d3d12_host_vsync", config.d3d12_host_vsync);
  tbl.insert_or_assign("d3d12_allow_variable_refresh_rate_and_tearing", config.d3d12_allow_variable_refresh_rate_and_tearing);
  tbl.insert_or_assign("present_dither", config.present_dither);
  tbl.insert_or_assign("show_fps_overlay", config.show_fps_overlay);
  tbl.insert_or_assign("show_startup_menu", config.show_startup_menu);
  tbl.insert_or_assign("crash_reporting", config.crash_reporting);
  tbl.insert_or_assign("mnk_sensitivity", config.mnk_sensitivity);
  tbl.insert_or_assign("video_mode_refresh_rate", config.video_mode_refresh_rate);
  tbl.insert_or_assign("anisotropic_override", config.anisotropic_override);
  if (kNativeRendererBuilt) {
    tbl.insert_or_assign("renderer", config.renderer == "native" ? "native" : "xenos");
    tbl.insert_or_assign("native_backend", config.native_backend);
    tbl.insert_or_assign("msaa_samples", config.msaa_samples);
    tbl.insert_or_assign("frame_rate", config.frame_rate);
    tbl.insert_or_assign("max_frame_latency", config.max_frame_latency);
    tbl.insert_or_assign("transparency_antialiasing", config.transparency_antialiasing);
    tbl.insert_or_assign("triple_buffering", config.triple_buffering);
    tbl.insert_or_assign("brightness", config.brightness);
    tbl.insert_or_assign("internal_resolution_scale", config.internal_resolution_scale);
    tbl.insert_or_assign("aspect_ratio", config.aspect_ratio);
  }
  tbl.erase("ultrawide_target_aspect");  // The disabled option of earlier launchers.
  tbl.insert_or_assign("resolution_scale", config.resolution_scale);
  tbl.insert_or_assign("draw_resolution_scale_x", config.resolution_scale);
  tbl.insert_or_assign("draw_resolution_scale_y", config.resolution_scale);
  for (std::size_t i = 0; i < kKeyActionCount; ++i) {
    tbl.insert_or_assign(KeyActionOption(i), config.key_binds[i]);
  }
  if (config.controls.version) {
    if (!tbl["controls"].is_table()) tbl.insert_or_assign("controls", toml::table{});
    auto& controls = *tbl["controls"].as_table();
    controls.insert_or_assign("version", config.controls.version);
    for (size_t i = 0; i < kPorts; ++i) {
      const auto& p = config.controls.ports[i];
      const auto name = "player" + std::to_string(i+1);
      if (!controls[name].is_table()) controls.insert_or_assign(name, toml::table{});
      auto& port = *controls[name].as_table();
      port.insert_or_assign("device", p.device); port.insert_or_assign("device_name", p.device_name);
      port.insert_or_assign("mouse", p.mouse); port.insert_or_assign("deadzones", p.deadzones);
      port.insert_or_assign("sensitivity", p.sensitivity);
      constexpr const char* axes[] = {"left_stick", "right_stick", "left_trigger", "right_trigger"};
      for (size_t a = 0; a < 4; ++a) port.insert_or_assign(axes[a], p.deadzone[a]);
      port.insert_or_assign("rumble_low", p.rumble[0]); port.insert_or_assign("rumble_high", p.rumble[1]);
      for (const char* bank : {"pad", "keys"}) if (!port[bank].is_table()) port.insert_or_assign(bank, toml::table{});
      for (size_t a = 0; a < kKeyActionCount; ++a) {
        port["pad"].as_table()->insert_or_assign(KeyActionOption(a), Encode(p.pad[a]));
        port["keys"].as_table()->insert_or_assign(KeyActionOption(a), p.keys[a]);
      }
    }
  }
  if (!config.custom_game_root.empty()) {
    tbl.insert_or_assign("custom_game_root", config.custom_game_root);
  } else {
    tbl.erase("custom_game_root");
  }
  // user_data_root is read only, so an absent override stays absent.
  // Replace only after a complete write; failed saves keep the old settings.
  auto temporary = toml_path;
#ifdef _WIN32
  const unsigned long process = GetCurrentProcessId();
#else
  const unsigned long process = getpid();
#endif
  temporary += ".launcher-" + std::to_string(process) + ".tmp";
  std::ofstream output(temporary, std::ios::trunc | std::ios::binary);
  if (!output) return false;
  output << "# broken_bond - runtime configuration generated by Naruto: The Broken Bond Launcher\n\n";
  output << tbl << "\n";
  output.flush();
  const bool written = output.good();
  output.close();
  if (!written || output.fail()) {
    std::filesystem::remove(temporary, ec);
    return false;
  }
#ifdef _WIN32
  const bool saved = MoveFileExW(temporary.c_str(), toml_path.c_str(),
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  std::filesystem::rename(temporary, toml_path, ec);
  const bool saved = !ec;
#endif
  if (!saved) std::filesystem::remove(temporary, ec);
  return saved;
}
