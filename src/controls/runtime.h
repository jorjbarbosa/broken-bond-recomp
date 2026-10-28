#pragma once
#include "controls/service.h"
#include <rex/input/device_assignment.h>
#include <rex/input/input_driver.h>
namespace broken_bond::controls {
RawState Transform(const RawState& raw, const Port& port);
std::unique_ptr<rex::input::InputDriver> ConfigureInput(
    std::unique_ptr<rex::input::InputDriver> driver);
std::unique_ptr<rex::input::DeviceAssignment> ConfiguredAssignment();
// New profiles follow available hardware instead of freezing the first poll.
Config Migrate(Config config);
}  // namespace broken_bond::controls
