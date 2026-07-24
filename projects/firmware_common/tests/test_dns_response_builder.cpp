#include <gtest/gtest.h>

#include <cstring>

#include "firmware_common/dns_response_builder.hpp"

namespace firmware_common {
namespace {

TEST(BuildDnsResponseTest, BuildsMinimalResponse) {
  uint8_t query[12] = {0x12, 0x34, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t out[64] = {};
  size_t out_len = sizeof(out);

  BuildDnsResponse(query, sizeof(query), /*answer_ip=*/0x0100A8C0, out,
                   &out_len);

  ASSERT_EQ(out_len, sizeof(query) + 16u);
  EXPECT_EQ(out[0], 0x12);  // transaction id сохранён
  EXPECT_EQ(out[1], 0x34);
  EXPECT_EQ(out[2], 0x81);  // QR=1, Opcode=0, AA=0, TC=0, RD=1
  EXPECT_EQ(out[3], 0x80);  // RA=1, Z=0, RCODE=0
  EXPECT_EQ(out[6], 0);     // ANCOUNT high
  EXPECT_EQ(out[7], 1);     // ANCOUNT low = 1

  const size_t off = sizeof(query);
  EXPECT_EQ(out[off + 0], 0xC0);  // pointer to name @12
  EXPECT_EQ(out[off + 1], 0x0C);
  EXPECT_EQ(out[off + 2], 0);  // TYPE A
  EXPECT_EQ(out[off + 3], 1);
  EXPECT_EQ(out[off + 4], 0);  // CLASS IN
  EXPECT_EQ(out[off + 5], 1);
  EXPECT_EQ(out[off + 9], 60);  // TTL = 60s
  EXPECT_EQ(out[off + 11], 4);  // RDLENGTH = 4

  uint32_t got_ip = 0;
  std::memcpy(&got_ip, out + off + 12, 4);
  EXPECT_EQ(got_ip, 0x0100A8C0u);
}

TEST(BuildDnsResponseTest, RejectsQueryShorterThanHeader) {
  uint8_t query[11] = {};
  uint8_t out[64] = {};
  size_t out_len = sizeof(out);

  BuildDnsResponse(query, sizeof(query), 0, out, &out_len);

  EXPECT_EQ(out_len, 0u);
}

TEST(BuildDnsResponseTest, RejectsUndersizedOutputBuffer) {
  uint8_t query[12] = {};
  uint8_t out[12 + 16 - 1] = {};  // на 1 байт меньше нужного
  size_t out_len = sizeof(out);

  BuildDnsResponse(query, sizeof(query), 0, out, &out_len);

  EXPECT_EQ(out_len, 0u);
}

TEST(BuildDnsResponseTest, AcceptsExactlyFittingOutputBuffer) {
  uint8_t query[12] = {};
  uint8_t out[12 + 16] = {};
  size_t out_len = sizeof(out);

  BuildDnsResponse(query, sizeof(query), 0, out, &out_len);

  EXPECT_EQ(out_len, sizeof(out));
}

// Реальный вызов в dns_server_task пишет ответ поверх собственного буфера
// приёма: BuildDnsResponse(buf, n, ip, buf, &len).
TEST(BuildDnsResponseTest, WorksWhenOutputAliasesQuery) {
  uint8_t buf[64] = {0xAA, 0xBB, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  size_t len = sizeof(buf);

  BuildDnsResponse(buf, 12, 0x01020304, buf, &len);

  EXPECT_EQ(len, 12u + 16u);
  EXPECT_EQ(buf[0], 0xAA);
  EXPECT_EQ(buf[1], 0xBB);
}

}  // namespace
}  // namespace firmware_common
