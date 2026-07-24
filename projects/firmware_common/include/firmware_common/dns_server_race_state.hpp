#pragma once

namespace firmware_common {

/**
 * Чистая (без FreeRTOS/сокетов) логика гонки между bind() внутри
 * dns_server_task и DnsServerStop(): кто раньше — задача публикует сокет,
 * либо Stop() успевает попросить остановку раньше, чем сокет опубликован.
 *
 * Методы не потокобезопасны сами по себе — вызывающий код (dns_server.cpp)
 * оборачивает каждый вызов в критическую секцию (portENTER_CRITICAL).
 * Здесь тестируется сама логика решений при разном порядке вызовов, а не
 * синхронизация.
 */
class DnsServerRaceState {
 public:
  static constexpr int kNoSocket = -1;

  enum class BindOutcome { kPublished, kStopRequested };
  enum class StopOutcome { kCloseSocket, kMarkPending };

  /** Задача только что успешно вызвала bind(sock). */
  BindOutcome OnBindSucceeded(int sock) {
    if (stop_requested_) {
      stop_requested_ = false;
      return BindOutcome::kStopRequested;
    }
    sock_ = sock;
    return BindOutcome::kPublished;
  }

  /**
   * DnsServerStop(): если сокет уже опубликован — забирает его (вызывающая
   * сторона закрывает через *out_sock), иначе просит задачу закрыться
   * самостоятельно сразу после bind().
   */
  StopOutcome RequestStop(int* out_sock) {
    if (sock_ != kNoSocket) {
      *out_sock = sock_;
      sock_ = kNoSocket;
      return StopOutcome::kCloseSocket;
    }
    stop_requested_ = true;
    return StopOutcome::kMarkPending;
  }

  /**
   * Вызывается в начале DnsServerStart() перед созданием новой задачи —
   * стук от гонки прошлого цикла (например, Stop() застал сокет
   * неопубликованным, а задача тем временем упала на bind() по независимой
   * причине и никогда не consume-ила флаг) не должен убить новую задачу.
   */
  void ResetForNewTask() {
    stop_requested_ = false;
    sock_ = kNoSocket;
  }

  bool IsSocketPublished() const { return sock_ != kNoSocket; }

 private:
  int sock_ = kNoSocket;
  bool stop_requested_ = false;
};

}  // namespace firmware_common
