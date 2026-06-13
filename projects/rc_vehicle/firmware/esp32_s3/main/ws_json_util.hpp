#pragma once

#include <utility>

#include "cJSON.h"
#include "esp_http_server.h"
#include "ws_command_registry.hpp"

namespace rc_vehicle {

/**
 * @brief RAII-обёртка над cJSON*
 *
 * Удаляет объект при выходе из области видимости — исключает утечки
 * при ранних return в хендлерах.
 */
class JsonDoc {
 public:
  explicit JsonDoc(cJSON* ptr) : ptr_(ptr) {}
  ~JsonDoc() { cJSON_Delete(ptr_); }

  JsonDoc(const JsonDoc&) = delete;
  JsonDoc& operator=(const JsonDoc&) = delete;
  JsonDoc(JsonDoc&& other) noexcept
      : ptr_(std::exchange(other.ptr_, nullptr)) {}
  JsonDoc& operator=(JsonDoc&&) = delete;

  [[nodiscard]] cJSON* get() const { return ptr_; }
  explicit operator bool() const { return ptr_ != nullptr; }

 private:
  cJSON* ptr_;
};

/**
 * @brief Отправить JSON-ответ на WS-команду
 *
 * Создаёт объект, добавляет поле "type", заполняет остальные поля через
 * fill(reply), отправляет и освобождает. При неудаче cJSON_CreateObject()
 * fill не вызывается — побочные эффекты (вызовы vc.*), которые должны
 * выполняться только при успешной аллокации ответа, размещаются внутри fill.
 */
template <typename Fill>
void WsReply(httpd_req_t* req, const char* type, Fill&& fill) {
  JsonDoc reply(cJSON_CreateObject());
  if (!reply) return;
  cJSON_AddStringToObject(reply.get(), "type", type);
  std::forward<Fill>(fill)(reply.get());
  WsSendJsonReply(req, reply.get());
}

/**
 * @brief Вариант WsReply для готового объекта
 *
 * Принимает владение doc (например, результатом StabilizationConfigToJson),
 * добавляет "type", дозаполняет через fill, отправляет и освобождает.
 */
template <typename Fill>
void WsReply(httpd_req_t* req, const char* type, JsonDoc doc, Fill&& fill) {
  if (!doc) return;
  cJSON_AddStringToObject(doc.get(), "type", type);
  std::forward<Fill>(fill)(doc.get());
  WsSendJsonReply(req, doc.get());
}

/** Вариант WsReply для готового объекта без дополнительных полей. */
inline void WsReply(httpd_req_t* req, const char* type, JsonDoc doc) {
  WsReply(req, type, std::move(doc), [](cJSON*) {});
}

/** float-поле с дефолтом: ключ отсутствует или не число → def. */
inline float JsonGetFloat(const cJSON* json, const char* key, float def) {
  const cJSON* item = cJSON_GetObjectItem(json, key);
  return (item && cJSON_IsNumber(item)) ? static_cast<float>(item->valuedouble)
                                        : def;
}

/** int-поле с дефолтом: ключ отсутствует или не число → def. */
inline int JsonGetInt(const cJSON* json, const char* key, int def) {
  const cJSON* item = cJSON_GetObjectItem(json, key);
  return (item && cJSON_IsNumber(item)) ? item->valueint : def;
}

/** Строковое поле с дефолтом: ключ отсутствует или не строка → def. */
inline const char* JsonGetString(const cJSON* json, const char* key,
                                 const char* def) {
  const cJSON* item = cJSON_GetObjectItem(json, key);
  return (item && cJSON_IsString(item)) ? item->valuestring : def;
}

/** bool-поле с дефолтом: ключ отсутствует или не bool → def. */
inline bool JsonGetBool(const cJSON* json, const char* key, bool def) {
  const cJSON* item = cJSON_GetObjectItem(json, key);
  return (item && cJSON_IsBool(item)) ? cJSON_IsTrue(item) : def;
}

/**
 * @brief int-поле с валидацией диапазона
 *
 * Отсутствующий ключ — не ошибка (возвращается def, *ok не изменяется).
 * Нечисловое значение или выход за [min_val, max_val] → *ok = false,
 * возвращается def. При успехе *ok не изменяется — каллер инициализирует
 * ok = true и проверяет после всех полей.
 */
inline int JsonGetIntChecked(const cJSON* json, const char* key, int def,
                             int min_val, int max_val, bool* ok) {
  const cJSON* item = cJSON_GetObjectItem(json, key);
  if (!item) return def;
  if (!cJSON_IsNumber(item) || item->valuedouble < min_val ||
      item->valuedouble > max_val) {
    if (ok) *ok = false;
    return def;
  }
  return item->valueint;
}

}  // namespace rc_vehicle
