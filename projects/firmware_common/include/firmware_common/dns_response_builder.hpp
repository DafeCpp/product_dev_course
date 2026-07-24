#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace firmware_common {

/**
 * Строит минимальный DNS response (echo вопроса + один A record) для
 * captive-portal DNS-сервера: любой запрос отвечается одним и тем же IP.
 *
 * @param query     сырой DNS-запрос (заголовок + вопрос)
 * @param query_len длина query в байтах
 * @param answer_ip IPv4 в network byte order
 * @param out       буфер под ответ (может совпадать с query — реальный
 *                  вызывающий код в dns_server_task пишет поверх своего же
 *                  буфера приёма)
 * @param out_len   на входе — размер out, на выходе — длина ответа (0, если
 *                  запрос короче заголовка или буфер недостаточен)
 */
inline void BuildDnsResponse(const uint8_t* query, size_t query_len,
                             uint32_t answer_ip, uint8_t* out,
                             size_t* out_len) {
  if (query_len < 12 || *out_len < query_len + 16) {
    *out_len = 0;
    return;
  }

  std::memcpy(out, query, query_len);

  // Заголовок: QR=1 (response), AA=1 (authoritative), RCODE=0
  out[2] = 0x81;  // QR=1, Opcode=0, AA=0, TC=0, RD=1
  out[3] = 0x80;  // RA=1, Z=0, RCODE=0
  out[6] = 0;     // ANCOUNT high
  out[7] = 1;     // ANCOUNT low = 1 answer

  // После вопроса добавляем A record
  size_t off = query_len;
  out[off++] = 0xC0;  // Pointer to name at offset 12
  out[off++] = 0x0C;
  out[off++] = 0;  // TYPE A
  out[off++] = 1;
  out[off++] = 0;  // CLASS IN
  out[off++] = 1;
  out[off++] = 0;  // TTL
  out[off++] = 0;
  out[off++] = 0;
  out[off++] = 60;  // 60 seconds
  out[off++] = 0;   // RDLENGTH
  out[off++] = 4;
  // A record: answer_ip is already in network byte order — copy bytes directly
  std::memcpy(out + off, &answer_ip, 4);
  off += 4;

  *out_len = off;
}

}  // namespace firmware_common
