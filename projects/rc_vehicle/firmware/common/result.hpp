#pragma once

#include <type_traits>
#include <utility>
#include <variant>

namespace rc_vehicle {

/**
 * @brief Result type for error handling (alternative to std::expected for
 * C++23)
 *
 * This is a generic Result type that can hold either a success value (T) or an
 * error value (E). It provides a type-safe way to handle errors without
 * exceptions.
 *
 * Based on std::variant, similar to the implementation in protocol.hpp but more
 * generic.
 *
 * @tparam T The success value type
 * @tparam E The error type
 *
 * Example usage:
 * @code
 * enum class MyError { InvalidInput, Timeout };
 *
 * Result<int, MyError> Divide(int a, int b) {
 *   if (b == 0) {
 *     return Result<int, MyError>::Err(MyError::InvalidInput);
 *   }
 *   return Result<int, MyError>::Ok(a / b);
 * }
 *
 * auto result = Divide(10, 2);
 * if (IsOk(result)) {
 *   int value = GetValue(result);
 * } else {
 *   MyError error = GetError(result);
 * }
 * @endcode
 */
template <typename T, typename E>
using Result = std::variant<T, E>;

// ═══════════════════════════════════════════════════════════════════════════
// Helper functions for Result type
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Check if result contains a success value
 * @param r The result to check
 * @return true if successful, false if error
 */
template <typename T, typename E>
[[nodiscard]] inline bool IsOk(const Result<T, E>& r) noexcept {
  return std::holds_alternative<T>(r);
}

/**
 * @brief Check if result contains an error
 * @param r The result to check
 * @return true if error, false if successful
 */
template <typename T, typename E>
[[nodiscard]] inline bool IsError(const Result<T, E>& r) noexcept {
  return std::holds_alternative<E>(r);
}

/**
 * @brief Get the success value from result
 * @param r The result
 * @return Reference to the success value
 * @note Undefined behavior if called on an error result
 */
template <typename T, typename E>
[[nodiscard]] inline const T& GetValue(const Result<T, E>& r) noexcept {
  return std::get<T>(r);
}

/**
 * @brief Get the success value from result (mutable)
 * @param r The result
 * @return Reference to the success value
 * @note Undefined behavior if called on an error result
 */
template <typename T, typename E>
[[nodiscard]] inline T& GetValue(Result<T, E>& r) noexcept {
  return std::get<T>(r);
}

/**
 * @brief Get the error value from result
 * @param r The result
 * @return The error value
 * @note Undefined behavior if called on a success result
 */
template <typename T, typename E>
[[nodiscard]] inline E GetError(const Result<T, E>& r) noexcept {
  return std::get<E>(r);
}

/**
 * @brief Create a successful result
 * @tparam T The success value type
 * @tparam E The error type
 * @param value The success value
 * @return Result containing the value
 */
template <typename T, typename E>
[[nodiscard]] inline Result<T, E> Ok(T value) noexcept(
    std::is_nothrow_move_constructible<T>::value) {
  return Result<T, E>(std::move(value));
}

/**
 * @brief Create an error result
 * @tparam T The success value type
 * @tparam E The error type
 * @param error The error value
 * @return Result containing the error
 */
template <typename T, typename E>
[[nodiscard]] inline Result<T, E> Err(E error) noexcept(
    std::is_nothrow_move_constructible<E>::value) {
  return Result<T, E>(std::move(error));
}

/**
 * @brief Get the value or a default if error
 * @tparam T The success value type
 * @tparam E The error type
 * @param r The result
 * @param default_value Value to return if this is an error
 * @return The success value or the default
 */
template <typename T, typename E>
[[nodiscard]] inline T ValueOr(const Result<T, E>& r, T default_value) noexcept(
    std::is_nothrow_copy_constructible<T>::value) {
  return IsOk(r) ? GetValue(r) : std::move(default_value);
}

/**
 * @brief Transform the success value if present
 * @tparam T The success value type
 * @tparam E The error type
 * @tparam F Function type (T -> U)
 * @param r The result
 * @param f Function to apply to the value
 * @return Result<U, E> with transformed value or original error
 */
template <typename T, typename E, typename F>
[[nodiscard]] inline auto Map(const Result<T, E>& r, F&& f)
    -> Result<decltype(f(std::declval<T>())), E> {
  using U = decltype(f(std::declval<T>()));
  if (IsOk(r)) {
    return Ok<U, E>(f(GetValue(r)));
  }
  return Err<U, E>(GetError(r));
}

/**
 * @brief Transform the error value if present
 * @tparam T The success value type
 * @tparam E The error type
 * @tparam F Function type (E -> E2)
 * @param r The result
 * @param f Function to apply to the error
 * @return Result<T, E2> with original value or transformed error
 */
template <typename T, typename E, typename F>
[[nodiscard]] inline auto MapErr(const Result<T, E>& r, F&& f)
    -> Result<T, decltype(f(std::declval<E>()))> {
  using E2 = decltype(f(std::declval<E>()));
  if (IsError(r)) {
    return Err<T, E2>(f(GetError(r)));
  }
  return Ok<T, E2>(GetValue(r));
}

/**
 * @brief Chain operations that return Result
 * @tparam T The success value type
 * @tparam E The error type
 * @tparam F Function type (T -> Result<U, E>)
 * @param r The result
 * @param f Function to apply to the value
 * @return Result<U, E> from the function or original error
 */
template <typename T, typename E, typename F>
[[nodiscard]] inline auto AndThen(const Result<T, E>& r, F&& f)
    -> decltype(f(std::declval<T>())) {
  if (IsOk(r)) {
    return f(GetValue(r));
  }
  using U = typename std::decay<decltype(GetValue(f(std::declval<T>())))>::type;
  return Err<U, E>(GetError(r));
}

}  // namespace rc_vehicle