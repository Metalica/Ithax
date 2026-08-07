#include "network/marshal/marshal.h"
#include "network/marshal/string_table.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using ithax::network::marshal::Decode;
using ithax::network::marshal::Encode;
using ithax::network::marshal::Value;
using ithax::network::marshal::ValuePtr;

class TestError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

void Require(bool condition, const char *message) {
  if (!condition) {
    throw TestError(message);
  }
}

std::vector<std::uint8_t> FromHex(const std::string &hex) {
  Require(hex.size() % 2U == 0U, "hex string has odd length");
  std::vector<std::uint8_t> bytes;
  bytes.reserve(hex.size() / 2U);
  for (std::size_t i = 0U; i < hex.size(); i += 2U) {
    const std::string byte_text = hex.substr(i, 2U);
    bytes.push_back(static_cast<std::uint8_t>(
        std::stoul(byte_text, nullptr, 16)));
  }
  return bytes;
}

std::string ToHex(const std::vector<std::uint8_t> &bytes) {
  static const char *kDigits = "0123456789abcdef";
  std::string hex;
  hex.reserve(bytes.size() * 2U);
  for (const std::uint8_t byte : bytes) {
    hex.push_back(kDigits[byte >> 4U]);
    hex.push_back(kDigits[byte & 0x0FU]);
  }
  return hex;
}

void TestGoldenVectors() {
  std::ifstream file("tests/fixtures/stage4_golden_vectors.json");
  Require(file.good(), "golden vector fixture is missing");
  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

  std::size_t pos = 0U;
  std::size_t count = 0U;
  while (true) {
    const std::size_t name_pos = content.find("\"name\": \"", pos);
    if (name_pos == std::string::npos) {
      break;
    }
    const std::size_t name_start = name_pos + 9U;
    const std::size_t name_end = content.find('"', name_start);
    Require(name_end != std::string::npos, "golden vector name is malformed");
    const std::string name = content.substr(name_start, name_end - name_start);

    const std::size_t hex_pos = content.find("\"hex\": \"", name_end);
    Require(hex_pos != std::string::npos, "golden vector hex is missing");
    const std::size_t hex_start = hex_pos + 8U;
    const std::size_t hex_end = content.find('"', hex_start);
    Require(hex_end != std::string::npos, "golden vector hex is malformed");
    const std::string hex = content.substr(hex_start, hex_end - hex_start);

    const std::vector<std::uint8_t> stream = FromHex(hex);
    const ValuePtr decoded = Decode(stream, true);
    Require(decoded != nullptr, "golden vector failed to decode");
    const std::vector<std::uint8_t> reencoded = Encode(*decoded);
    Require(ToHex(reencoded) == hex,
            "golden vector re-encode did not match");
    ++count;
    pos = hex_end;
  }
  Require(count >= 20U, "golden vector fixture has too few vectors");
  std::cout << "{\"event\":\"stage4_golden_vectors\",\"status\":\"pass\","
            << "\"vectors\":" << count << "}\n";
}

void TestRoundTrip() {
  std::vector<ValuePtr> values;
  values.push_back(Value::None());
  values.push_back(Value::Bool(true));
  values.push_back(Value::Bool(false));
  values.push_back(Value::Int(0));
  values.push_back(Value::Int(1));
  values.push_back(Value::Int(-1));
  values.push_back(Value::Int(42));
  values.push_back(Value::Int(-42));
  values.push_back(Value::Int(300));
  values.push_back(Value::Int(-300));
  values.push_back(Value::Int(70'000));
  values.push_back(Value::Int(-70'000));
  values.push_back(Value::Int(5'000'000'000LL));
  values.push_back(Value::Int(-5'000'000'000LL));
  values.push_back(Value::Int(9'223'372'036'854'775'807LL));
  values.push_back(Value::Real(0.0));
  values.push_back(Value::Real(3.14159));
  values.push_back(Value::Real(-2.5e-10));
  values.push_back(Value::String(""));
  values.push_back(Value::String("x"));
  values.push_back(Value::String("name"));
  values.push_back(Value::String("a longer string not in the table"));
  values.push_back(Value::WString(""));
  values.push_back(Value::WString("hello"));
  values.push_back(Value::Token("blue.DBRowDescriptor"));
  values.push_back(Value::Buffer({1U, 2U, 3U, 4U, 5U}));
  values.push_back(Value::Tuple({}));
  values.push_back(Value::Tuple({Value::Int(7)}));
  values.push_back(Value::Tuple({Value::Int(1), Value::String("two")}));
  values.push_back(Value::Tuple(
      {Value::Int(1), Value::Int(2), Value::Int(3), Value::Int(4)}));
  values.push_back(Value::List({}));
  values.push_back(Value::List({Value::Int(1)}));
  values.push_back(Value::List({Value::Int(1), Value::Int(2), Value::Int(3)}));
  values.push_back(Value::Dict({}));
  values.push_back(Value::Dict(
      {{Value::String("key1"), Value::String("value1")},
       {Value::String("key2"), Value::Int(42)}}));
  values.push_back(Value::Object(
      "macho.MachoAddress",
      Value::Tuple({Value::Int(8), Value::String("machoNet"), Value::None()})));
  values.push_back(Value::SubStruct(Value::Int(5)));
  values.push_back(Value::SubStream(Encode(*Value::Tuple(
      {Value::Int(1), Value::Int(2)}))));
  values.push_back(Value::CPicked({0x80U, 0x02U, 0x4EU}));

  for (const auto &value : values) {
    const std::vector<std::uint8_t> stream = Encode(*value);
    const ValuePtr decoded = Decode(stream, true);
    Require(decoded != nullptr, "round-trip decode returned null");
    const std::vector<std::uint8_t> reencoded = Encode(*decoded);
    Require(reencoded == stream, "round-trip re-encode diverged");
  }
  std::cout << "{\"event\":\"stage4_marshal_roundtrip\",\"status\":\"pass\","
            << "\"values\":" << values.size() << "}\n";
}

void TestPackedRowRoundTrip() {
  ithax::network::marshal::PackedRowData row;
  row.columns = {
      {"historyDate", ithax::network::marshal::DbType::FileTime},
      {"lowPrice", ithax::network::marshal::DbType::Cy},
      {"highPrice", ithax::network::marshal::DbType::Cy},
      {"avgPrice", ithax::network::marshal::DbType::Cy},
      {"volume", ithax::network::marshal::DbType::I8},
      {"orders", ithax::network::marshal::DbType::I4},
      {"active", ithax::network::marshal::DbType::Bool},
      {"note", ithax::network::marshal::DbType::Str},
  };
  row.values = {
      Value::Int(1'700'000'000'000'000'000LL),
      Value::Int(18'000),
      Value::Int(19'000),
      Value::Int(18'400),
      Value::Int(5'463'586),
      Value::Int(254),
      Value::Bool(true),
      Value::String("hello"),
  };
  const ValuePtr packed = Value::PackedRow(row);
  const std::vector<std::uint8_t> stream = Encode(*packed);
  const ValuePtr decoded = Decode(stream, true);
  Require(decoded->IsPackedRow(), "packed row did not decode as a row");
  const auto &decoded_row = decoded->PackedRowValue();
  Require(decoded_row.columns.size() == row.columns.size(),
          "packed row column count changed");
  Require(decoded_row.values.size() == row.values.size(),
          "packed row value count changed");
  for (std::size_t i = 0U; i < row.values.size(); ++i) {
    Require(decoded_row.values[i]->Kind() == row.values[i]->Kind(),
            "packed row field kind changed");
    if (row.values[i]->IsInt()) {
      Require(decoded_row.values[i]->IntValue() ==
                  row.values[i]->IntValue(),
              "packed row int field changed");
    } else if (row.values[i]->IsBool()) {
      Require(decoded_row.values[i]->BoolValue() ==
                  row.values[i]->BoolValue(),
              "packed row bool field changed");
    } else if (row.values[i]->IsString()) {
      Require(decoded_row.values[i]->StringValue() ==
                  row.values[i]->StringValue(),
              "packed row string field changed");
    }
  }
  std::cout << "{\"event\":\"stage4_packed_row_roundtrip\",\"status\":\"pass\""
            << "}\n";
}

void TestMalformedInputs() {
  struct Case {
    const char *name;
    std::vector<std::uint8_t> stream;
  };
  const std::vector<Case> cases = {
      {"bad_header", {0x7FU, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U}},
      {"truncated_header", {0x7EU, 0x00U}},
      {"truncated_int", {0x7EU, 0x00U, 0x00U, 0x00U, 0x00U, 0x06U}},
      {"unknown_opcode", {0x7EU, 0x00U, 0x00U, 0x00U, 0x00U, 0x0CU}},
      {"unknown_flag", {0x7EU, 0x00U, 0x00U, 0x00U, 0x00U, 0x80U | 0x01U}},
      {"save_without_store",
       {0x7EU, 0x00U, 0x00U, 0x00U, 0x00U, 0x40U | 0x01U}},
      {"string_table_out_of_range",
       {0x7EU, 0x00U, 0x00U, 0x00U, 0x00U, 0x11U, 0xC4U}},
      {"trailing_data",
       {0x7EU, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U, 0x02U}},
      {"truncated_tuple",
       {0x7EU, 0x00U, 0x00U, 0x00U, 0x00U, 0x14U, 0x03U, 0x01U}},
      {"truncated_string",
       {0x7EU, 0x00U, 0x00U, 0x00U, 0x00U, 0x13U, 0x05U, 0x61U}},
      {"objectex_unterminated",
       {0x7EU, 0x00U, 0x00U, 0x00U, 0x00U, 0x22U, 0x01U}},
      {"saved_element_without_store",
       {0x7EU, 0x00U, 0x00U, 0x00U, 0x00U, 0x1BU, 0x01U}},
  };
  for (const auto &test : cases) {
    bool rejected = false;
    try {
      Decode(test.stream, true);
    } catch (const ithax::network::marshal::MarshalError &) {
      rejected = true;
    }
    Require(rejected, "malformed stream was accepted");
  }
  std::cout << "{\"event\":\"stage4_marshal_malformed\",\"status\":\"pass\","
            << "\"cases\":" << cases.size() << "}\n";
}

void TestDepthLimit() {
  ValuePtr nested = Value::Int(1);
  for (std::uint32_t i = 0U; i < 100U; ++i) {
    nested = Value::Tuple({nested});
  }
  bool rejected = false;
  try {
    Encode(*nested);
  } catch (const ithax::network::marshal::MarshalError &) {
    rejected = true;
  }
  Require(rejected, "excessive nesting was accepted");
  std::cout << "{\"event\":\"stage4_marshal_depth_limit\",\"status\":\"pass\""
            << "}\n";
}

void TestFuzz() {
  std::mt19937 generator(0x5EED'0401U);
  std::uniform_int_distribution<int> byte_dist(0, 255);
  std::size_t accepted = 0U;
  const std::size_t iterations = 2'000U;
  for (std::size_t i = 0U; i < iterations; ++i) {
    const std::size_t length = byte_dist(generator) % 64U;
    std::vector<std::uint8_t> stream;
    stream.reserve(length);
    for (std::size_t j = 0U; j < length; ++j) {
      stream.push_back(static_cast<std::uint8_t>(byte_dist(generator)));
    }
    try {
      Decode(stream, true);
      ++accepted;
    } catch (const ithax::network::marshal::MarshalError &) {
    }
  }
  std::cout << "{\"event\":\"stage4_marshal_fuzz\",\"status\":\"pass\","
            << "\"iterations\":" << iterations
            << ",\"accepted\":" << accepted << "}\n";
}

void TestStringTable() {
  using ithax::network::marshal::Djb2Hash;
  using ithax::network::marshal::LookupString;
  using ithax::network::marshal::LookupStringIndex;
  Require(LookupStringIndex("name").value() == 52U,
          "string table index for name is wrong");
  Require(LookupStringIndex("macho.MachoAddress").value() == 48U,
          "string table index for macho address is wrong");
  Require(!LookupStringIndex("not in the table").has_value(),
          "string table matched an unknown string");
  Require(LookupString(52U).value() == "name",
          "string table lookup by index is wrong");
  Require(!LookupString(0U).has_value(),
          "string table accepted index zero");
  Require(!LookupString(196U).has_value(),
          "string table accepted an out-of-range index");
  Require(Djb2Hash("name") == Djb2Hash("name"),
          "djb2 hash is not deterministic");
  std::cout << "{\"event\":\"stage4_string_table\",\"status\":\"pass\"}\n";
}

} // namespace

int main() {
  try {
    TestStringTable();
    TestGoldenVectors();
    TestRoundTrip();
    TestPackedRowRoundTrip();
    TestMalformedInputs();
    TestDepthLimit();
    TestFuzz();
    std::cout << "{\"event\":\"stage4_marshal_suite\",\"status\":\"pass\"}\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "stage4 marshal suite failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
