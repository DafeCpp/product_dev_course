#pragma once

#include <cstdint>
#include <tuple>
#include <type_traits>

namespace rc_vehicle {

/** Проверка на этапе компиляции: T входит в список типов Us. */
template <typename T, typename... Us>
struct IsOneOf;

template <typename T, typename U>
struct IsOneOf<T, U> : std::is_same<T, U> {};

template <typename T, typename U, typename... Rest>
struct IsOneOf<T, U, Rest...>
    : std::integral_constant<bool, std::is_same<T, U>::value ||
                                       IsOneOf<T, Rest...>::value> {};

/**
 * Контекст приложения: регистрация и получение компонентов по типу
 * (compile-time). Список типов компонентов задаётся при объявлении контекста,
 * например: using AppContext = Context<UartBridgeBase, SpiDevice>; Регистрация:
 * ctx.Set<UartBridgeBase>(&uart_impl); Получение:   auto* uart =
 * ctx.Get<UartBridgeBase>();
 */
template <typename... ComponentTypes>
class Context {
 public:
  using Storage = std::tuple<ComponentTypes *...>;

  /** Регистрирует компонент по типу T (по ссылке). */
  template <typename T>
  void Set(T &ref) {
    static_assert(IsOneOf<T, ComponentTypes...>::value,
                  "T must be one of the context's component types");
    std::get<T *>(ptrs_) = &ref;
  }

  /** Возвращает зарегистрированный компонент по типу T; nullptr если не задан.
   */
  template <typename T>
  T *Get() {
    static_assert(IsOneOf<T, ComponentTypes...>::value,
                  "T must be one of the context's component types");
    return std::get<T *>(ptrs_);
  }

  template <typename T>
  const T *Get() const {
    static_assert(IsOneOf<T, ComponentTypes...>::value,
                  "T must be one of the context's component types");
    return std::get<T *>(ptrs_);
  }

  /** Платформенное время (мс). Может быть nullptr. */
  uint32_t (*get_time_ms)(){nullptr};

 private:
  Storage ptrs_{};
};

}  // namespace rc_vehicle
