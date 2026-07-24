#pragma once

#include <cstddef>

namespace firmware_common {

/**
 * Отслеживает число подряд неудачных отправок на WS-клиента (по fd) в
 * фиксированной таблице слотов. Чистая логика без ESP-IDF — собственно
 * отправку (httpd_ws_send_data) и закрытие сессии (httpd_sess_trigger_close)
 * делает вызывающая сторона.
 *
 * Ключ — fd, а не позиция в списке клиентов httpd: позиция меняется между
 * вызовами httpd_get_client_list, индексирование по ней было бы некорректным.
 * kNoSlot ("пусто") — не 0, т.к. fd 0 (stdin) httpd может переиспользовать.
 */
template <int kMaxClients>
class WsClientFailureTracker {
 public:
  static constexpr int kNoSlot = -1;

  WsClientFailureTracker() {
    for (int s = 0; s < kMaxClients; ++s) {
      keys_[s] = kNoSlot;
    }
  }

  /** Найти существующий слот для fd или занять свободный. kNoSlot, если
   * таблица заполнена другими fd — неудачи для этого fd тогда не считаются
   * (деградация, а не ошибка: тот же компромисс, что был до выноса). */
  int FindOrAllocate(int fd) {
    for (int s = 0; s < kMaxClients; ++s) {
      if (keys_[s] == fd) return s;
    }
    for (int s = 0; s < kMaxClients; ++s) {
      if (keys_[s] == kNoSlot) {
        keys_[s] = fd;
        fail_count_[s] = 0;
        return s;
      }
    }
    return kNoSlot;
  }

  void RecordSuccess(int slot) {
    if (slot == kNoSlot) return;
    fail_count_[slot] = 0;
  }

  /** Инкрементирует счётчик неудач и возвращает новое значение (-1 для
   * kNoSlot). Порог и решение об эвикции — на стороне вызывающего кода. */
  int RecordFailure(int slot) {
    if (slot == kNoSlot) return -1;
    return ++fail_count_[slot];
  }

  /** Освободить слот (клиент закрыт после превышения порога неудач). */
  void Evict(int slot) {
    if (slot == kNoSlot) return;
    keys_[slot] = kNoSlot;
    fail_count_[slot] = 0;
  }

  /** Освободить слоты для fd, которых больше нет в списке текущих клиентов. */
  void GarbageCollect(const int* current_fds, size_t count) {
    for (int s = 0; s < kMaxClients; ++s) {
      if (keys_[s] == kNoSlot) continue;
      bool found = false;
      for (size_t i = 0; i < count; ++i) {
        if (current_fds[i] == keys_[s]) {
          found = true;
          break;
        }
      }
      if (!found) {
        keys_[s] = kNoSlot;
        fail_count_[s] = 0;
      }
    }
  }

 private:
  int keys_[kMaxClients];
  int fail_count_[kMaxClients] = {};
};

}  // namespace firmware_common
