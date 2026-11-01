// The input system read by several guest threads at once, as the title does:
// every XAM input export refreshes the device list, so no thread may see it
// rebuilt under another (patches/rexglue-input-system-lock.patch). The drivers
// sit behind the game's controls and menu gate, as in input_replay.cpp.
#include "controls/runtime.h"
#include "menu/input_gate.h"
#include <rex/input/input_system.h>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
using namespace rex::input;
using rex::X_RESULT;
using rex::X_STATUS;
namespace {
constexpr uint64_t kDevices = 24;
// Calls that reached a driver with a device it does not own.
std::atomic<uint64_t> foreign{0};
// Pads that connect and disconnect at every enumeration, so the input system
// rebuilds its device list in every call.
class Churn final : public InputDriver {
 public:
  explicit Churn(uint64_t base) : InputDriver(nullptr, 0), base_(base) {}
  X_STATUS Setup() override { return X_STATUS_SUCCESS; }
  void EnumerateDevices(std::vector<DeviceInfo>& out) override {
    const uint64_t count = 1 + enumerations_.fetch_add(1) % kDevices;
    for (uint64_t i = 0; i < count; ++i) {
      DeviceInfo d;
      d.id = DeviceId(base_ + i);
      d.key = "fixture:churn:" + std::to_string(base_ + i);
      d.name = "Churn fixture";
      out.push_back(std::move(d));
    }
  }
  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out) override {
    Own(id);
    *out = {};
    return X_ERROR_SUCCESS;
  }
  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t, X_INPUT_CAPABILITIES* out) override {
    Own(id);
    *out = {};
    out->type = 1;
    return X_ERROR_SUCCESS;
  }
  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION*) override {
    Own(id);
    return X_ERROR_SUCCESS;
  }
  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t, X_INPUT_KEYSTROKE*) override {
    Own(id);
    return X_ERROR_EMPTY;
  }

 private:
  void Own(DeviceId id) const {
    const auto value = uint64_t(id);
    if (value < base_ || value >= base_ + kDevices) foreign.fetch_add(1);
  }
  uint64_t base_;
  std::atomic<uint64_t> enumerations_{0};
};
}  // namespace
int main(int argc, char** argv) {
  const int iterations = argc > 1 ? std::atoi(argv[1]) : 20000;
  broken_bond::controls::Publish(broken_bond::controls::Migrate({}));
  InputSystem input(nullptr);
  for (uint64_t base : {1000ull, 2000ull})
    input.AddDriver(broken_bond::menu::GateInput(broken_bond::controls::ConfigureInput(std::make_unique<Churn>(base))));
  input.SetDeviceAssignment(broken_bond::controls::ConfiguredAssignment());
  std::atomic<uint64_t> states{0};
  std::vector<std::thread> threads;
  for (unsigned t = 0; t < 8; ++t)
    threads.emplace_back([&, t] {
      for (int i = 0; i < iterations; ++i) {
        const uint32_t user = (t + unsigned(i)) % 4;
        switch ((t + unsigned(i)) % 4) {
          case 0:
          case 1: {
            X_INPUT_STATE state{};
            if (input.GetState(0, &state) == X_ERROR_SUCCESS) states.fetch_add(1);
            break;
          }
          case 2: {
            X_INPUT_CAPABILITIES caps{};
            input.GetCapabilities(user, 1, &caps);
            break;
          }
          default: {
            X_INPUT_VIBRATION vibration{};
            input.SetState(0, &vibration);
            X_INPUT_KEYSTROKE stroke{};
            input.GetKeystroke(user, 0, &stroke);
          }
        }
      }
    });
  for (auto& thread : threads) thread.join();
  input.Shutdown();
  if (foreign.load() || !states.load()) {
    std::cerr << "FAIL: " << foreign.load() << " calls reached a driver with a device it does not own; "
              << states.load() << " states read\n";
    return 1;
  }
  std::cout << "PASS: 8 threads x " << iterations << " calls over a changing device list; " << states.load()
            << " states read, no call reached the wrong driver\n";
  return 0;
}
