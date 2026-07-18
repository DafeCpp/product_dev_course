#pragma once

#include <expected>
#include <utility>

namespace rc_vehicle {

/**
 * @brief Unit type for std::expected<void, E> (represents successful void
 * operation)
 */
struct Unit {};

/**
 * @brief Result type for error handling — thin alias over std::expected
 *
 * Replaces the old std::variant-based Result. All old helper functions
 * (IsOk, IsError, GetValue, GetError, ValueOr, Map, MapErr, AndThen) are
 * kept as free-function wrappers for backward compatibility during migration.
 *
 * @tparam T The success value type
 * @tparam E The error type
 */
template <typename T, typename E>
using Result = std::expected<T, E>;

// ═══════════════════════════════════════════════════════════════════════════
// Backward-compatible helpers (thin wrappers over std::expected API)
// ═══════════════════════════════════════════════════════════════════════════

template <typename T, typename E>
[[nodiscard]] inline bool IsOk(const Result<T, E>& r) noexcept {
  return r.has_value();
}

template <typename T, typename E>
[[nodiscard]] inline bool IsError(const Result<T, E>& r) noexcept {
  return !r.has_value();
}

template <typename T, typename E>
[[nodiscard]] inline const T& GetValue(const Result<T, E>& r) noexcept {
  return *r;
}

template <typename T, typename E>
[[nodiscard]] inline T& GetValue(Result<T, E>& r) noexcept {
  return *r;
}

template <typename T, typename E>
[[nodiscard]] inline E GetError(const Result<T, E>& r) noexcept {
  return r.error();
}

template <typename T, typename E>
[[nodiscard]] inline T ValueOr(const Result<T, E>& r, T default_value) noexcept(
    std::is_nothrow_copy_constructible<T>::value) {
  return r.value_or(std::move(default_value));
}

template <typename T, typename E, typename F>
[[nodiscard]] inline auto Map(const Result<T, E>& r, F&& f) {
  return r.transform(std::forward<F>(f));
}

template <typename T, typename E, typename F>
[[nodiscard]] inline auto MapErr(const Result<T, E>& r, F&& f) {
  return r.transform_error(std::forward<F>(f));
}

template <typename T, typename E, typename F>
[[nodiscard]] inline auto AndThen(const Result<T, E>& r, F&& f) {
  return r.and_then(std::forward<F>(f));
}

}  // namespace rc_vehicle
