#pragma once

#include "hydraulic_plant_model.hpp"
#include "valve_channel.hpp"

namespace bench::testing {

/**
 * @brief Канал клапана поверх гидравлической модели
 *
 * Замыкает контур в SIL без CAN: WriteSetpoint запоминает команду,
 * PostTick шагает модель, ReadFeedback отдаёт её состояние. Displacement-
 * команды с отрицательным value двигают шток назад — как настоящий
 * пропорциональный клапан.
 */
class PlantValveChannel final : public IValveChannel {
 public:
  explicit PlantValveChannel(const HydraulicPlantModel::Config& config)
      : plant_(config) {}

  void WriteSetpoint(const ValveSetpoint& sp) override { last_sp_ = sp; }

  [[nodiscard]] ValveFeedback ReadFeedback() override {
    const auto& s = plant_.GetState();
    return ValveFeedback{.force_n = s.force_n,
                         .position_mm = s.position_mm,
                         .status = 0,
                         .fresh = true,
                         .age_ticks = 0};
  }

  void PostTick(uint32_t dt_us) override {
    const float dt_sec = static_cast<float>(dt_us) / 1e6f;
    plant_.Step(last_sp_.enable ? last_sp_.value : 0.0f, dt_sec);
  }

  [[nodiscard]] bool IsOperational() const override { return true; }

  HydraulicPlantModel& Plant() { return plant_; }
  [[nodiscard]] const ValveSetpoint& LastSetpoint() const { return last_sp_; }

 private:
  HydraulicPlantModel plant_;
  ValveSetpoint last_sp_{};
};

}  // namespace bench::testing
