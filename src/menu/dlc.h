// The DLC page of the game menu. Copies run outside the UI; STFS packages are
// staged for the game's installer at the next launch.
#pragma once
#include <filesystem>
#include <cstdint>
#include <functional>
#include <future>
#include <string>
namespace broken_bond::menu {
class Ui;
class Dlc {
 public:
  // staging: the folder the game installs packages from at start.
  void Open(Ui& ui, std::filesystem::path staging, std::filesystem::path user_root, void* owner,
            std::function<void(std::function<void()>)> defer, std::function<void()> closed);
  void Poll();
  void InstallFolder(const std::filesystem::path& source);
  void Wait() { if (work_.valid()) work_.wait(); }
 private:
  void Redraw();
  void ChooseFolder();
  struct Result { int staged = -1; std::string error; };
  std::future<Result> work_;
  Ui* ui_ = nullptr;
  uint64_t view_ = 0, job_view_ = 0;
  void* owner_ = nullptr;
  std::filesystem::path staging_;
  std::function<void(std::function<void()>)> defer_;
  std::function<void()> closed_;
};
}  // namespace broken_bond::menu
