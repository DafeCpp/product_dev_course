#pragma once

namespace bench {

/**
 * @brief Связь с supervisory-уровнем (платформой)
 *
 * В спайке — заглушка: контуру важен только факт «связь жива»
 * (вход LinkWatchdog). Реальный транспорт-агностичный протокол
 * (WebSocket/WiFi, RS485) — LOS-74.
 */
class ISupervisoryLink {
 public:
  virtual ~ISupervisoryLink() = default;

  /// Связь с платформой жива (heartbeat в допуске)
  [[nodiscard]] virtual bool IsAlive() const = 0;
};

/**
 * @brief Управляемая заглушка для тестов и SIL-сценариев
 */
class StubSupervisoryLink final : public ISupervisoryLink {
 public:
  [[nodiscard]] bool IsAlive() const override { return alive_; }
  void SetAlive(bool alive) { alive_ = alive; }

 private:
  bool alive_{true};
};

}  // namespace bench
