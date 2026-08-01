#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "firmware_common/json_writer.hpp"

namespace firmware_common {
namespace {

TEST(JsonWriterTest, EmptyObject) {
  std::string out;
  JsonWriter w(out);
  w.BeginObject();
  w.EndObject();
  EXPECT_EQ(out, "{}");
}

TEST(JsonWriterTest, ScalarFieldsAndCommaPlacement) {
  std::string out;
  JsonWriter w(out);
  w.BeginObject();
  w.RawStr("type", "telem");
  w.Bool("ok", true);
  w.Bool("bad", false);
  w.Int("n", 42);
  w.EndObject();
  EXPECT_EQ(out, R"({"type":"telem","ok":true,"bad":false,"n":42})");
}

TEST(JsonWriterTest, IntNegativeAndZero) {
  std::string out;
  JsonWriter w(out);
  w.BeginObject();
  w.Int("a", -5);
  w.Int("b", 0);
  w.EndObject();
  EXPECT_EQ(out, R"({"a":-5,"b":0})");
}

TEST(JsonWriterTest, NestedObjectAndArray) {
  std::string out;
  JsonWriter w(out);
  w.BeginObject();
  w.BeginObject("outer");
  w.Int("x", 1);
  w.EndObject();
  w.BeginArray("arr");
  w.FixedElem(1.5f, 2);
  w.FixedElem(-2.25f, 2);
  w.EndArray();
  w.EndObject();
  EXPECT_EQ(out, R"({"outer":{"x":1},"arr":[1.5,-2.25]})");
}

TEST(JsonWriterTest, FixedRoundsNormally) {
  std::string out;
  JsonWriter w(out);
  w.BeginObject();
  w.Fixed("v", 3.14159f, 2);
  w.EndObject();
  EXPECT_EQ(out, R"({"v":3.14})");
}

TEST(JsonWriterTest, FixedRoundsHalfAwayFromZero) {
  std::string out;
  JsonWriter w(out);
  w.BeginObject();
  w.Fixed("v", 0.125f, 2);
  w.EndObject();
  EXPECT_EQ(out, R"({"v":0.13})");
}

TEST(JsonWriterTest, FixedNegativeRounding) {
  std::string out;
  JsonWriter w(out);
  w.BeginObject();
  w.Fixed("v", -0.125f, 2);
  w.EndObject();
  EXPECT_EQ(out, R"({"v":-0.13})");
}

TEST(JsonWriterTest, FixedTrimsTrailingZeros) {
  std::string out;
  JsonWriter w(out);
  w.BeginObject();
  w.Fixed("v", 1.20f, 3);
  w.EndObject();
  EXPECT_EQ(out, R"({"v":1.2})");
}

TEST(JsonWriterTest, FixedWholeNumberOmitsDecimalPoint) {
  {
    std::string out;
    JsonWriter w(out);
    w.BeginObject();
    w.Fixed("v", 5.0f, 3);
    w.EndObject();
    EXPECT_EQ(out, R"({"v":5})");
  }
  {
    std::string out;
    JsonWriter w(out);
    w.BeginObject();
    w.Fixed("v", -5.0f, 3);
    w.EndObject();
    EXPECT_EQ(out, R"({"v":-5})");
  }
}

TEST(JsonWriterTest, FixedZeroAndNegativeZeroPrintPlainZero) {
  {
    std::string out;
    JsonWriter w(out);
    w.BeginObject();
    w.Fixed("v", 0.0f, 3);
    w.EndObject();
    EXPECT_EQ(out, R"({"v":0})");
  }
  {
    std::string out;
    JsonWriter w(out);
    w.BeginObject();
    w.Fixed("v", -0.0f, 3);
    w.EndObject();
    EXPECT_EQ(out, R"({"v":0})");
  }
}

TEST(JsonWriterTest, FixedNanAndInfBecomeNull) {
  const float kNan = std::numeric_limits<float>::quiet_NaN();
  const float kInf = std::numeric_limits<float>::infinity();
  {
    std::string out;
    JsonWriter w(out);
    w.BeginObject();
    w.Fixed("v", kNan, 2);
    w.EndObject();
    EXPECT_EQ(out, R"({"v":null})");
  }
  {
    std::string out;
    JsonWriter w(out);
    w.BeginObject();
    w.Fixed("v", kInf, 2);
    w.EndObject();
    EXPECT_EQ(out, R"({"v":null})");
  }
  {
    std::string out;
    JsonWriter w(out);
    w.BeginObject();
    w.Fixed("v", -kInf, 2);
    w.EndObject();
    EXPECT_EQ(out, R"({"v":null})");
  }
}

TEST(JsonWriterTest, FixedOutOfRangeBecomesNull) {
  std::string out;
  JsonWriter w(out);
  w.BeginObject();
  w.Fixed("v", 1e10f, 0);
  w.EndObject();
  EXPECT_EQ(out, R"({"v":null})");
}

TEST(JsonWriterTest, FixedLargeInRangeValue) {
  std::string out;
  JsonWriter w(out);
  w.BeginObject();
  w.Fixed("v", 2000000.0f, 0);
  w.EndObject();
  EXPECT_EQ(out, R"({"v":2000000})");
}

TEST(JsonWriterTest, SciZeroPrintsPlainZero) {
  std::string out;
  JsonWriter w(out);
  w.BeginObject();
  w.Sci("v", 0.0f);
  w.EndObject();
  EXPECT_EQ(out, R"({"v":0})");
}

TEST(JsonWriterTest, SciNanBecomesNull) {
  std::string out;
  JsonWriter w(out);
  w.BeginObject();
  w.Sci("v", std::numeric_limits<float>::quiet_NaN());
  w.EndObject();
  EXPECT_EQ(out, R"({"v":null})");
}

TEST(JsonWriterTest, SciAboveThresholdUsesFixedSixDecimals) {
  std::string out;
  JsonWriter w(out);
  w.BeginObject();
  w.Sci("v", 0.001f);
  w.EndObject();
  EXPECT_EQ(out, R"({"v":0.001})");
}

TEST(JsonWriterTest, SciBelowThresholdUsesScientificNotation) {
  std::string out;
  JsonWriter w(out);
  w.BeginObject();
  w.Sci("v", 1.23e-7f);
  w.EndObject();
  // Мантисса округляется до 1 знака после запятой.
  EXPECT_EQ(out, R"({"v":1.2e-7})");
}

TEST(JsonWriterTest, SciBelowThresholdNegativeValue) {
  std::string out;
  JsonWriter w(out);
  w.BeginObject();
  w.Sci("v", -5.0e-6f);
  w.EndObject();
  EXPECT_EQ(out, R"({"v":-5.0e-6})");
}

TEST(JsonWriterTest, SciMantissaRoundsUpToNextExponent) {
  // 9.99e-6 -> мантисса округляется до 10.0 -> переносится в следующий
  // порядок: 1.0e-5.
  std::string out;
  JsonWriter w(out);
  w.BeginObject();
  w.Sci("v", 9.99e-6f);
  w.EndObject();
  EXPECT_EQ(out, R"({"v":1.0e-5})");
}

}  // namespace
}  // namespace firmware_common
