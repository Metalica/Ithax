#include "network/marshal/marshal.h"

#include "network/marshal/string_table.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <utility>

namespace ithax::network::marshal {

namespace {

constexpr std::size_t kSizeExEscape = 0xFFU;
constexpr std::size_t kMaxPackedRowColumns = 1024U;
constexpr std::size_t kMaxPackedRowBytes = 1U * 1024U * 1024U;

std::uint32_t DbTypeSizeBitsImpl(DbType type) noexcept {
  switch (type) {
    case DbType::Cy:
    case DbType::I8:
    case DbType::Ui8:
    case DbType::FileTime:
    case DbType::R8:
      return 64U;
    case DbType::I4:
    case DbType::Ui4:
    case DbType::R4:
      return 32U;
    case DbType::I2:
    case DbType::Ui2:
      return 16U;
    case DbType::I1:
    case DbType::Ui1:
      return 8U;
    case DbType::Bool:
      return 1U;
    case DbType::Empty:
    case DbType::Error:
    case DbType::Bytes:
    case DbType::Str:
    case DbType::WStr:
      return 0U;
  }
  return 0U;
}

class Writer {
public:
  void PutU8(std::uint8_t value) { m_bytes.push_back(value); }

  void PutU16(std::uint16_t value) {
    m_bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    m_bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  }

  void PutU32(std::uint32_t value) {
    for (std::size_t i = 0U; i < 4U; ++i) {
      m_bytes.push_back(
          static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU));
    }
  }

  void PutI8(std::int8_t value) {
    m_bytes.push_back(static_cast<std::uint8_t>(value));
  }

  void PutI16(std::int16_t value) {
    PutU16(static_cast<std::uint16_t>(value));
  }

  void PutI32(std::int32_t value) {
    PutU32(static_cast<std::uint32_t>(value));
  }

  void PutI64(std::int64_t value) {
    PutU64(static_cast<std::uint64_t>(value));
  }

  void PutU64(std::uint64_t value) {
    for (std::size_t i = 0U; i < 8U; ++i) {
      m_bytes.push_back(
          static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU));
    }
  }

  void PutDouble(double value) {
    std::uint64_t bits = 0U;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    PutU64(bits);
  }

  void PutBytes(const std::vector<std::uint8_t> &bytes) {
    m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
  }

  void PutSizeEx(std::uint32_t size) {
    if (size < kSizeExEscape) {
      PutU8(static_cast<std::uint8_t>(size));
    } else {
      PutU8(static_cast<std::uint8_t>(kSizeExEscape));
      PutU32(size);
    }
  }

  std::vector<std::uint8_t> Take() { return std::move(m_bytes); }

private:
  std::vector<std::uint8_t> m_bytes;
};

class Reader {
public:
  explicit Reader(const std::vector<std::uint8_t> &bytes)
      : m_bytes(bytes) {}

  std::size_t Remaining() const noexcept {
    return m_bytes.size() - m_offset;
  }

  std::uint8_t PeekU8() const {
    if (m_offset >= m_bytes.size()) {
      throw MarshalTruncatedError("marshal stream ended unexpectedly");
    }
    return m_bytes[m_offset];
  }

  std::uint8_t ReadU8() {
    if (m_offset >= m_bytes.size()) {
      throw MarshalTruncatedError("marshal stream ended unexpectedly");
    }
    return m_bytes[m_offset++];
  }

  std::int8_t ReadI8() { return static_cast<std::int8_t>(ReadU8()); }

  std::uint16_t ReadU16() {
    RequireBytes(2U);
    const std::uint16_t value = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(m_bytes[m_offset]) |
        (static_cast<std::uint16_t>(m_bytes[m_offset + 1U]) << 8U));
    m_offset += 2U;
    return value;
  }

  std::int16_t ReadI16() { return static_cast<std::int16_t>(ReadU16()); }

  std::uint32_t ReadU32() {
    RequireBytes(4U);
    std::uint32_t value = 0U;
    for (std::size_t i = 0U; i < 4U; ++i) {
      value |= static_cast<std::uint32_t>(m_bytes[m_offset + i])
               << (i * 8U);
    }
    m_offset += 4U;
    return value;
  }

  std::int32_t ReadI32() { return static_cast<std::int32_t>(ReadU32()); }

  std::uint64_t ReadU64() {
    RequireBytes(8U);
    std::uint64_t value = 0U;
    for (std::size_t i = 0U; i < 8U; ++i) {
      value |= static_cast<std::uint64_t>(m_bytes[m_offset + i])
               << (i * 8U);
    }
    m_offset += 8U;
    return value;
  }

  std::int64_t ReadI64() { return static_cast<std::int64_t>(ReadU64()); }

  double ReadDouble() {
    const std::uint64_t bits = ReadU64();
    double value = 0.0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  std::vector<std::uint8_t> ReadBytes(std::size_t count) {
    RequireBytes(count);
    std::vector<std::uint8_t> result(
        m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset),
        m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset + count));
    m_offset += count;
    return result;
  }

  std::uint32_t ReadSizeEx() {
    const std::uint8_t first = ReadU8();
    if (first == kSizeExEscape) {
      return ReadU32();
    }
    return first;
  }

  void Skip(std::size_t count) { RequireBytes(count); m_offset += count; }

  std::size_t Offset() const noexcept { return m_offset; }

private:
  void RequireBytes(std::size_t count) const {
    if (count > Remaining()) {
      throw MarshalTruncatedError("marshal stream ended unexpectedly");
    }
  }

  const std::vector<std::uint8_t> &m_bytes;
  std::size_t m_offset = 0U;
};

class Encoder {
public:
  std::vector<std::uint8_t> Encode(const Value &value) {
    Writer writer;
    writer.PutU8(MARSHAL_HEADER_BYTE);
    writer.PutU32(0U);
    EncodeValue(writer, value, 0U);
    return writer.Take();
  }

private:
  void EncodeValue(Writer &writer, const Value &value,
                   std::uint32_t depth) {
    if (depth > MAX_MARSHAL_DEPTH) {
      throw MarshalDepthError("marshal nesting exceeds the depth limit");
    }
    switch (value.Kind()) {
      case ValueKind::None:
        writer.PutU8(static_cast<std::uint8_t>(Opcode::PyNone));
        break;
      case ValueKind::Bool:
        writer.PutU8(static_cast<std::uint8_t>(
            value.BoolValue() ? Opcode::PyTrue : Opcode::PyFalse));
        break;
      case ValueKind::Int:
        EncodeInteger(writer, value.IntValue());
        break;
      case ValueKind::Real:
        EncodeReal(writer, value.RealValue());
        break;
      case ValueKind::String:
        EncodeString(writer, value.StringValue());
        break;
      case ValueKind::WString:
        EncodeWString(writer, value.WStringValue());
        break;
      case ValueKind::Token:
        EncodeToken(writer, value.TokenValue());
        break;
      case ValueKind::Buffer:
        EncodeBuffer(writer, value.BufferValue());
        break;
      case ValueKind::Tuple:
        EncodeTuple(writer, value.TupleValue(), depth);
        break;
      case ValueKind::List:
        EncodeList(writer, value.ListValue(), depth);
        break;
      case ValueKind::Dict:
        EncodeDict(writer, value.DictValue(), depth);
        break;
      case ValueKind::Object:
        EncodeObject(writer, value, depth);
        break;
      case ValueKind::ObjectEx:
        EncodeObjectEx(writer, value, depth);
        break;
      case ValueKind::SubStruct:
        writer.PutU8(static_cast<std::uint8_t>(Opcode::PySubStruct));
        EncodeValue(writer, *value.SubStructValue(), depth + 1U);
        break;
      case ValueKind::SubStream:
        EncodeSubStream(writer, value.SubStreamData());
        break;
      case ValueKind::ChecksumedStream:
        EncodeChecksumedStream(writer, value, depth);
        break;
      case ValueKind::PackedRow:
        EncodePackedRow(writer, value, depth);
        break;
      case ValueKind::CPicked:
        EncodeCPicked(writer, value.CPickedData());
        break;
    }
  }

  void EncodeInteger(Writer &writer, std::int64_t value) {
    if (value == -1) {
      writer.PutU8(static_cast<std::uint8_t>(Opcode::PyMinusOne));
    } else if (value == 0) {
      writer.PutU8(static_cast<std::uint8_t>(Opcode::PyZeroInteger));
    } else if (value == 1) {
      writer.PutU8(static_cast<std::uint8_t>(Opcode::PyOneInteger));
    } else if (value >= std::numeric_limits<std::int8_t>::min() &&
               value <= std::numeric_limits<std::int8_t>::max()) {
      writer.PutU8(static_cast<std::uint8_t>(Opcode::PyByte));
      writer.PutI8(static_cast<std::int8_t>(value));
    } else if (value >= std::numeric_limits<std::int16_t>::min() &&
               value <= std::numeric_limits<std::int16_t>::max()) {
      writer.PutU8(static_cast<std::uint8_t>(Opcode::PySignedShort));
      writer.PutI16(static_cast<std::int16_t>(value));
    } else if (value >= std::numeric_limits<std::int32_t>::min() &&
               value <= std::numeric_limits<std::int32_t>::max()) {
      writer.PutU8(static_cast<std::uint8_t>(Opcode::PyLong));
      writer.PutI32(static_cast<std::int32_t>(value));
    } else {
      writer.PutU8(static_cast<std::uint8_t>(Opcode::PyLongLong));
      writer.PutI64(value);
    }
  }

  void EncodeReal(Writer &writer, double value) {
    if (value == 0.0) {
      writer.PutU8(static_cast<std::uint8_t>(Opcode::PyZeroReal));
    } else {
      writer.PutU8(static_cast<std::uint8_t>(Opcode::PyReal));
      writer.PutDouble(value);
    }
  }

  void EncodeString(Writer &writer, const std::string &value) {
    if (value.empty()) {
      writer.PutU8(static_cast<std::uint8_t>(Opcode::PyEmptyString));
    } else if (value.size() == 1U) {
      writer.PutU8(static_cast<std::uint8_t>(Opcode::PyCharString));
      writer.PutU8(static_cast<std::uint8_t>(value[0]));
    } else {
      const auto index = LookupStringIndex(value);
      if (index.has_value()) {
        writer.PutU8(static_cast<std::uint8_t>(Opcode::PyStringTableItem));
        writer.PutU8(index.value());
      } else {
        writer.PutU8(static_cast<std::uint8_t>(Opcode::PyLongString));
        writer.PutSizeEx(static_cast<std::uint32_t>(value.size()));
        writer.PutBytes(std::vector<std::uint8_t>(value.begin(),
                                                  value.end()));
      }
    }
  }

  void EncodeWString(Writer &writer, const std::string &value) {
    if (value.empty()) {
      writer.PutU8(static_cast<std::uint8_t>(Opcode::PyEmptyWString));
    } else {
      writer.PutU8(static_cast<std::uint8_t>(Opcode::PyWStringUTF8));
      writer.PutSizeEx(static_cast<std::uint32_t>(value.size()));
      writer.PutBytes(std::vector<std::uint8_t>(value.begin(),
                                                value.end()));
    }
  }

  void EncodeToken(Writer &writer, const std::string &value) {
    writer.PutU8(static_cast<std::uint8_t>(Opcode::PyToken));
    writer.PutSizeEx(static_cast<std::uint32_t>(value.size()));
    writer.PutBytes(std::vector<std::uint8_t>(value.begin(), value.end()));
  }

  void EncodeBuffer(Writer &writer,
                    const std::vector<std::uint8_t> &value) {
    writer.PutU8(static_cast<std::uint8_t>(Opcode::PyBuffer));
    writer.PutSizeEx(static_cast<std::uint32_t>(value.size()));
    writer.PutBytes(value);
  }

  void EncodeTuple(Writer &writer, const std::vector<ValuePtr> &items,
                   std::uint32_t depth) {
    if (items.size() > MAX_CONTAINER_COUNT) {
      throw MarshalLimitError("tuple element count exceeds the limit");
    }
    if (items.empty()) {
      writer.PutU8(static_cast<std::uint8_t>(Opcode::PyEmptyTuple));
    } else if (items.size() == 1U) {
      writer.PutU8(static_cast<std::uint8_t>(Opcode::PyOneTuple));
    } else if (items.size() == 2U) {
      writer.PutU8(static_cast<std::uint8_t>(Opcode::PyTwoTuple));
    } else {
      writer.PutU8(static_cast<std::uint8_t>(Opcode::PyTuple));
      writer.PutSizeEx(static_cast<std::uint32_t>(items.size()));
    }
    for (const auto &item : items) {
      EncodeValue(writer, *item, depth + 1U);
    }
  }

  void EncodeList(Writer &writer, const std::vector<ValuePtr> &items,
                  std::uint32_t depth) {
    if (items.size() > MAX_CONTAINER_COUNT) {
      throw MarshalLimitError("list element count exceeds the limit");
    }
    if (items.empty()) {
      writer.PutU8(static_cast<std::uint8_t>(Opcode::PyEmptyList));
    } else if (items.size() == 1U) {
      writer.PutU8(static_cast<std::uint8_t>(Opcode::PyOneList));
    } else {
      writer.PutU8(static_cast<std::uint8_t>(Opcode::PyList));
      writer.PutSizeEx(static_cast<std::uint32_t>(items.size()));
    }
    for (const auto &item : items) {
      EncodeValue(writer, *item, depth + 1U);
    }
  }

  void EncodeDict(Writer &writer, const std::vector<DictEntry> &entries,
                  std::uint32_t depth) {
    if (entries.size() > MAX_CONTAINER_COUNT) {
      throw MarshalLimitError("dict entry count exceeds the limit");
    }
    writer.PutU8(static_cast<std::uint8_t>(Opcode::PyDict));
    writer.PutSizeEx(static_cast<std::uint32_t>(entries.size()));
    for (const auto &entry : entries) {
      EncodeValue(writer, *entry.value, depth + 1U);
      EncodeValue(writer, *entry.key, depth + 1U);
    }
  }

  void EncodeObject(Writer &writer, const Value &value,
                    std::uint32_t depth) {
    writer.PutU8(static_cast<std::uint8_t>(Opcode::PyObject));
    EncodeString(writer, value.ObjectType());
    EncodeValue(writer, *value.ObjectArgs(), depth + 1U);
  }

  void EncodeObjectEx(Writer &writer, const Value &value,
                      std::uint32_t depth) {
    const auto &data = value.ObjectExValue();
    writer.PutU8(static_cast<std::uint8_t>(
        value.IsObjectEx2() ? Opcode::PyObjectEx2 : Opcode::PyObjectEx1));
    EncodeValue(writer, *data.header, depth + 1U);
    for (const auto &item : data.list) {
      EncodeValue(writer, *item, depth + 1U);
    }
    writer.PutU8(static_cast<std::uint8_t>(Opcode::PackedTerminator));
    for (const auto &entry : data.dict) {
      EncodeValue(writer, *entry.key, depth + 1U);
      EncodeValue(writer, *entry.value, depth + 1U);
    }
    writer.PutU8(static_cast<std::uint8_t>(Opcode::PackedTerminator));
  }

  void EncodeSubStream(Writer &writer,
                       const std::vector<std::uint8_t> &data) {
    writer.PutU8(static_cast<std::uint8_t>(Opcode::PySubStream));
    writer.PutSizeEx(static_cast<std::uint32_t>(data.size()));
    writer.PutBytes(data);
  }

  void EncodeChecksumedStream(Writer &writer, const Value &value,
                              std::uint32_t depth) {
    writer.PutU8(static_cast<std::uint8_t>(Opcode::PyChecksumedStream));
    writer.PutU32(value.Checksum());
    EncodeValue(writer, *value.ChecksumedValue(), depth + 1U);
  }

  void EncodeCPicked(Writer &writer,
                     const std::vector<std::uint8_t> &data) {
    writer.PutU8(static_cast<std::uint8_t>(Opcode::cPicked));
    writer.PutSizeEx(static_cast<std::uint32_t>(data.size()));
    writer.PutBytes(data);
  }

  void EncodePackedRow(Writer &writer, const Value &value,
                       std::uint32_t depth) {
    const auto &row = value.PackedRowValue();
    if (row.columns.size() > kMaxPackedRowColumns) {
      throw MarshalLimitError("packed row column count exceeds the limit");
    }
    if (row.values.size() != row.columns.size()) {
      throw MarshalFormatError("packed row value count mismatch");
    }
    writer.PutU8(static_cast<std::uint8_t>(Opcode::PyPackedRow));
    EncodeValue(writer, *BuildRowHeader(row), depth + 1U);

    std::vector<std::pair<std::uint32_t, std::size_t>> size_order;
    for (std::size_t i = 0U; i < row.columns.size(); ++i) {
      size_order.emplace_back(DbTypeSizeBits(row.columns[i].type), i);
    }
    std::stable_sort(size_order.begin(), size_order.end(),
                     [](const auto &left, const auto &right) {
                       return left.first > right.first;
                     });

    std::vector<std::uint8_t> packed;
    std::vector<std::uint8_t> bit_data;
    std::size_t bool_count = 0U;
    for (const auto &[size, index] : size_order) {
      if (row.columns[index].type == DbType::Bool) {
        ++bool_count;
      }
    }
    const std::size_t bit_bytes = (bool_count + row.columns.size() + 7U) / 8U;
    bit_data.assign(bit_bytes, 0U);
    std::size_t bool_offset = 0U;

    for (const auto &[size, index] : size_order) {
      const DbType type = row.columns[index].type;
      if (size == 0U) {
        continue;
      }
      const Value &field = *row.values[index];
      const bool is_null = field.IsNone();
      if (is_null) {
        const std::size_t null_bit = index + bool_count;
        bit_data[null_bit / 8U] |=
            static_cast<std::uint8_t>(1U << (null_bit % 8U));
      }
      if (type == DbType::Bool) {
        if (!is_null && field.BoolValue()) {
          bit_data[bool_offset / 8U] |=
              static_cast<std::uint8_t>(1U << (bool_offset % 8U));
        }
        ++bool_offset;
        continue;
      }
      AppendPackedScalar(packed, type, field);
    }

    std::vector<std::uint8_t> rle = ZeroCompress(packed, bit_data);
    writer.PutSizeEx(static_cast<std::uint32_t>(rle.size()));
    writer.PutBytes(rle);

    for (const auto &[size, index] : size_order) {
      if (size == 0U) {
        EncodeValue(writer, *row.values[index], depth + 1U);
      }
    }
  }

  static ValuePtr BuildRowHeader(const PackedRowData &row) {
    std::vector<ValuePtr> columns;
    columns.reserve(row.columns.size());
    for (const auto &column : row.columns) {
      std::vector<ValuePtr> column_tuple;
      column_tuple.push_back(Value::String(column.name));
      column_tuple.push_back(
          Value::Int(static_cast<std::int64_t>(column.type)));
      columns.push_back(Value::Tuple(std::move(column_tuple)));
    }
    std::vector<ValuePtr> args;
    args.push_back(Value::Tuple(std::move(columns)));
    ObjectExData data;
    data.header = Value::Token("blue.DBRowDescriptor");
    data.list = std::move(args);
    return Value::ObjectEx(false, std::move(data));
  }

  static void AppendPackedScalar(std::vector<std::uint8_t> &out,
                                DbType type, const Value &field) {
    const std::int64_t int_value =
        field.IsNone() ? 0 : field.IntValue();
    switch (type) {
      case DbType::Cy:
      case DbType::I8:
      case DbType::Ui8:
      case DbType::FileTime:
        AppendI64(out, int_value);
        break;
      case DbType::I4:
        AppendI32(out, static_cast<std::int32_t>(int_value));
        break;
      case DbType::Ui4:
        AppendU32(out, static_cast<std::uint32_t>(int_value));
        break;
      case DbType::R4: {
        const double real_value =
            field.IsNone() ? 0.0 : field.RealValue();
        const float f = static_cast<float>(real_value);
        std::uint32_t bits = 0U;
        static_assert(sizeof(bits) == sizeof(f));
        std::memcpy(&bits, &f, sizeof(bits));
        AppendU32(out, bits);
        break;
      }
      case DbType::R8: {
        const double real_value =
            field.IsNone() ? 0.0 : field.RealValue();
        std::uint64_t bits = 0U;
        static_assert(sizeof(bits) == sizeof(real_value));
        std::memcpy(&bits, &real_value, sizeof(bits));
        AppendU64(out, bits);
        break;
      }
      case DbType::I2:
        AppendI16(out, static_cast<std::int16_t>(int_value));
        break;
      case DbType::Ui2:
        AppendU16(out, static_cast<std::uint16_t>(int_value));
        break;
      case DbType::I1:
        out.push_back(static_cast<std::uint8_t>(
            static_cast<std::int8_t>(int_value)));
        break;
      case DbType::Ui1:
        out.push_back(static_cast<std::uint8_t>(int_value));
        break;
      case DbType::Bool:
      case DbType::Empty:
      case DbType::Error:
      case DbType::Bytes:
      case DbType::Str:
      case DbType::WStr:
        break;
    }
  }

  static void AppendU16(std::vector<std::uint8_t> &out,
                        std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  }

  static void AppendI16(std::vector<std::uint8_t> &out,
                        std::int16_t value) {
    AppendU16(out, static_cast<std::uint16_t>(value));
  }

  static void AppendU32(std::vector<std::uint8_t> &out,
                        std::uint32_t value) {
    for (std::size_t i = 0U; i < 4U; ++i) {
      out.push_back(static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU));
    }
  }

  static void AppendI32(std::vector<std::uint8_t> &out,
                        std::int32_t value) {
    AppendU32(out, static_cast<std::uint32_t>(value));
  }

  static void AppendU64(std::vector<std::uint8_t> &out,
                        std::uint64_t value) {
    for (std::size_t i = 0U; i < 8U; ++i) {
      out.push_back(static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU));
    }
  }

  static void AppendI64(std::vector<std::uint8_t> &out,
                        std::int64_t value) {
    AppendU64(out, static_cast<std::uint64_t>(value));
  }

  static std::vector<std::uint8_t> ZeroCompress(
      const std::vector<std::uint8_t> &packed,
      const std::vector<std::uint8_t> &bit_data) {
    std::vector<std::uint8_t> input = packed;
    input.insert(input.end(), bit_data.begin(), bit_data.end());
    std::vector<std::uint8_t> out;
    std::size_t i = 0U;
    while (i < input.size()) {
      const std::size_t control_index = out.size();
      out.push_back(0U);
      for (std::size_t nibble = 0U; nibble < 2U && i < input.size();
           ++nibble) {
        if (input[i] == 0U) {
          std::size_t run = 1U;
          while (run < 8U && i + run < input.size() &&
                 input[i + run] == 0U) {
            ++run;
          }
          const std::uint8_t encoded =
              static_cast<std::uint8_t>(run + 7U);
          out[control_index] |= static_cast<std::uint8_t>(
              encoded << (nibble * 4U));
          i += run;
        } else {
          std::size_t run = 1U;
          while (run < 8U && i + run < input.size() &&
                 input[i + run] != 0U) {
            ++run;
          }
          const std::uint8_t encoded =
              static_cast<std::uint8_t>(8U - run);
          out[control_index] |= static_cast<std::uint8_t>(
              encoded << (nibble * 4U));
          out.insert(out.end(), input.begin() + static_cast<std::ptrdiff_t>(i),
                     input.begin() + static_cast<std::ptrdiff_t>(i + run));
          i += run;
        }
      }
    }
    return out;
  }
};

class Decoder {
public:
  explicit Decoder(const std::vector<std::uint8_t> &stream)
      : m_reader(stream) {}

  ValuePtr Decode(bool require_exact_end) {
    const std::uint8_t header = m_reader.ReadU8();
    if (header != MARSHAL_HEADER_BYTE) {
      throw MarshalFormatError("invalid marshal header byte");
    }
    const std::uint32_t save_count = m_reader.ReadU32();
    if (save_count > MAX_SAVED_OBJECTS) {
      throw MarshalLimitError("marshal save count exceeds the limit");
    }
    m_saved.resize(save_count);
    const std::size_t value_end =
        save_count == 0U
            ? m_reader.Remaining()
            : m_reader.Remaining() - static_cast<std::size_t>(save_count) * 4U;
    m_value_end = m_reader.Offset() + value_end;

    ValuePtr result = DecodeValue(0U);
    if (require_exact_end) {
      if (m_reader.Offset() != m_value_end) {
        throw MarshalFormatError("marshal stream has trailing data");
      }
    }
    return result;
  }

private:
  ValuePtr DecodeValue(std::uint32_t depth) {
    if (depth > MAX_MARSHAL_DEPTH) {
      throw MarshalDepthError("marshal nesting exceeds the depth limit");
    }
    const std::uint8_t raw = m_reader.ReadU8();
    const bool flag_save = (raw & SAVE_MASK) != 0U;
    const bool flag_unknown = (raw & UNKNOWN_MASK) != 0U;
    const std::uint8_t opcode = raw & OPCODE_MASK;
    if (flag_unknown) {
      throw MarshalFormatError("marshal stream set the unknown flag");
    }
    std::uint32_t storage_index = 0U;
    if (flag_save) {
      storage_index = ReadSaveIndex();
    }

    ValuePtr result = DecodeOpcode(opcode, depth);
    if (flag_save) {
      if (storage_index == 0U || storage_index > m_saved.size()) {
        throw MarshalFormatError("marshal save index is out of range");
      }
      m_saved[storage_index - 1U] = result;
    }
    return result;
  }

  ValuePtr DecodeOpcode(std::uint8_t opcode, std::uint32_t depth) {
    switch (static_cast<Opcode>(opcode)) {
      case Opcode::PyNone:
        return Value::None();
      case Opcode::PyTrue:
        return Value::Bool(true);
      case Opcode::PyFalse:
        return Value::Bool(false);
      case Opcode::PyZeroInteger:
        return Value::Int(0);
      case Opcode::PyOneInteger:
        return Value::Int(1);
      case Opcode::PyMinusOne:
        return Value::Int(-1);
      case Opcode::PyByte:
        return Value::Int(m_reader.ReadI8());
      case Opcode::PySignedShort:
        return Value::Int(m_reader.ReadI16());
      case Opcode::PyLong:
        return Value::Int(m_reader.ReadI32());
      case Opcode::PyLongLong:
        return Value::Int(m_reader.ReadI64());
      case Opcode::PyVarInteger:
        return DecodeVarInteger();
      case Opcode::PyZeroReal:
        return Value::Real(0.0);
      case Opcode::PyReal:
        return Value::Real(m_reader.ReadDouble());
      case Opcode::PyEmptyString:
        return Value::String("");
      case Opcode::PyCharString:
        return Value::String(
            std::string(1U, static_cast<char>(m_reader.ReadU8())));
      case Opcode::PyShortString:
        return DecodeString(m_reader.ReadU8());
      case Opcode::PyLongString:
        return DecodeString(ReadBoundedSize());
      case Opcode::PyStringTableItem:
        return DecodeStringTableItem();
      case Opcode::PyEmptyWString:
        return Value::WString("");
      case Opcode::PyWStringUCS2Char:
        return DecodeWStringUcs2(1U);
      case Opcode::PyWStringUCS2:
        return DecodeWStringUcs2(ReadBoundedSize());
      case Opcode::PyWStringUTF8:
        return DecodeWStringUtf8(ReadBoundedSize());
      case Opcode::PyToken:
        return DecodeToken(ReadBoundedSize());
      case Opcode::PyBuffer:
        return DecodeBuffer(ReadBoundedSize());
      case Opcode::PyEmptyTuple:
        return Value::Tuple({});
      case Opcode::PyOneTuple:
        return Value::Tuple({DecodeValue(depth + 1U)});
      case Opcode::PyTwoTuple:
        return Value::Tuple(
            {DecodeValue(depth + 1U), DecodeValue(depth + 1U)});
      case Opcode::PyTuple:
        return DecodeTuple(ReadBoundedCount(), depth);
      case Opcode::PyEmptyList:
        return Value::List({});
      case Opcode::PyOneList:
        return Value::List({DecodeValue(depth + 1U)});
      case Opcode::PyList:
        return DecodeList(ReadBoundedCount(), depth);
      case Opcode::PyDict:
        return DecodeDict(ReadBoundedCount(), depth);
      case Opcode::PyObject:
        return DecodeObject(depth);
      case Opcode::PyObjectEx1:
        return DecodeObjectEx(false, depth);
      case Opcode::PyObjectEx2:
        return DecodeObjectEx(true, depth);
      case Opcode::PySubStruct:
        return Value::SubStruct(DecodeValue(depth + 1U));
      case Opcode::PySubStream:
        return DecodeSubStream();
      case Opcode::PyChecksumedStream:
        return DecodeChecksumedStream(depth);
      case Opcode::PySavedStreamElement:
        return DecodeSavedElement();
      case Opcode::PyPackedRow:
        return DecodePackedRow(depth);
      case Opcode::cPicked:
        return DecodeCPicked(ReadBoundedSize());
      case Opcode::PackedTerminator:
        throw MarshalFormatError("unexpected packed terminator");
    }
    throw MarshalFormatError("unknown marshal opcode");
  }

  ValuePtr DecodeVarInteger() {
    const std::uint32_t len = ReadBoundedSize();
    if (len == 0U) {
      return Value::Int(0);
    }
    if (len > 8U) {
      throw MarshalLimitError("var integer length exceeds 8 bytes");
    }
    const auto bytes = m_reader.ReadBytes(len);
    std::uint64_t value = 0U;
    for (std::size_t i = 0U; i < bytes.size(); ++i) {
      value |= static_cast<std::uint64_t>(bytes[i]) << (i * 8U);
    }
    return Value::Int(static_cast<std::int64_t>(value));
  }

  ValuePtr DecodeString(std::uint32_t len) {
    if (len > MAX_STRING_BYTES) {
      throw MarshalLimitError("string length exceeds the limit");
    }
    const auto bytes = m_reader.ReadBytes(len);
    return Value::String(std::string(bytes.begin(), bytes.end()));
  }

  ValuePtr DecodeStringTableItem() {
    const std::uint8_t index = m_reader.ReadU8();
    const auto text = LookupString(index);
    if (!text.has_value()) {
      throw MarshalStringTableError("string table index is out of range");
    }
    return Value::String(text.value());
  }

  ValuePtr DecodeWStringUcs2(std::uint32_t count) {
    if (count > MAX_STRING_BYTES / 2U) {
      throw MarshalLimitError("wide string length exceeds the limit");
    }
    const auto bytes = m_reader.ReadBytes(static_cast<std::size_t>(count) * 2U);
    std::string utf8;
    utf8.reserve(count);
    for (std::size_t i = 0U; i < bytes.size(); i += 2U) {
      const std::uint16_t unit = static_cast<std::uint16_t>(
          static_cast<std::uint16_t>(bytes[i]) |
          (static_cast<std::uint16_t>(bytes[i + 1U]) << 8U));
      AppendUtf16Unit(utf8, unit);
    }
    return Value::WString(std::move(utf8));
  }

  ValuePtr DecodeWStringUtf8(std::uint32_t len) {
    if (len > MAX_STRING_BYTES) {
      throw MarshalLimitError("wide string length exceeds the limit");
    }
    const auto bytes = m_reader.ReadBytes(len);
    return Value::WString(std::string(bytes.begin(), bytes.end()));
  }

  ValuePtr DecodeToken(std::uint32_t len) {
    if (len > MAX_STRING_BYTES) {
      throw MarshalLimitError("token length exceeds the limit");
    }
    const auto bytes = m_reader.ReadBytes(len);
    return Value::Token(std::string(bytes.begin(), bytes.end()));
  }

  ValuePtr DecodeBuffer(std::uint32_t len) {
    if (len > MAX_STRING_BYTES) {
      throw MarshalLimitError("buffer length exceeds the limit");
    }
    return Value::Buffer(m_reader.ReadBytes(len));
  }

  ValuePtr DecodeTuple(std::uint32_t count, std::uint32_t depth) {
    std::vector<ValuePtr> items;
    items.reserve(count);
    for (std::uint32_t i = 0U; i < count; ++i) {
      items.push_back(DecodeValue(depth + 1U));
    }
    return Value::Tuple(std::move(items));
  }

  ValuePtr DecodeList(std::uint32_t count, std::uint32_t depth) {
    std::vector<ValuePtr> items;
    items.reserve(count);
    for (std::uint32_t i = 0U; i < count; ++i) {
      items.push_back(DecodeValue(depth + 1U));
    }
    return Value::List(std::move(items));
  }

  ValuePtr DecodeDict(std::uint32_t count, std::uint32_t depth) {
    std::vector<DictEntry> entries;
    entries.reserve(count);
    for (std::uint32_t i = 0U; i < count; ++i) {
      ValuePtr value = DecodeValue(depth + 1U);
      ValuePtr key = DecodeValue(depth + 1U);
      entries.push_back({std::move(key), std::move(value)});
    }
    return Value::Dict(std::move(entries));
  }

  ValuePtr DecodeObject(std::uint32_t depth) {
    ValuePtr type = DecodeValue(depth + 1U);
    if (!type->IsString()) {
      throw MarshalFormatError("object type is not a string");
    }
    ValuePtr args = DecodeValue(depth + 1U);
    return Value::Object(type->StringValue(), std::move(args));
  }

  ValuePtr DecodeObjectEx(bool is_type2, std::uint32_t depth) {
    ObjectExData data;
    data.header = DecodeValue(depth + 1U);
    while (true) {
      if (m_reader.Offset() >= m_value_end) {
        throw MarshalTruncatedError("object ex list is unterminated");
      }
      if ((m_reader.PeekU8() & OPCODE_MASK) ==
          static_cast<std::uint8_t>(Opcode::PackedTerminator)) {
        m_reader.ReadU8();
        break;
      }
      data.list.push_back(DecodeValue(depth + 1U));
    }
    while (true) {
      if (m_reader.Offset() >= m_value_end) {
        throw MarshalTruncatedError("object ex dict is unterminated");
      }
      if ((m_reader.PeekU8() & OPCODE_MASK) ==
          static_cast<std::uint8_t>(Opcode::PackedTerminator)) {
        m_reader.ReadU8();
        break;
      }
      ValuePtr key = DecodeValue(depth + 1U);
      ValuePtr value = DecodeValue(depth + 1U);
      data.dict.push_back({std::move(key), std::move(value)});
    }
    return Value::ObjectEx(is_type2, std::move(data));
  }

  ValuePtr DecodeSubStream() {
    const std::uint32_t len = ReadBoundedSize();
    const auto data = m_reader.ReadBytes(len);
    return Value::SubStream(data);
  }

  ValuePtr DecodeChecksumedStream(std::uint32_t depth) {
    const std::uint32_t checksum = m_reader.ReadU32();
    ValuePtr value = DecodeValue(depth + 1U);
    return Value::ChecksumedStream(checksum, std::move(value));
  }

  ValuePtr DecodeSavedElement() {
    const std::uint32_t index = ReadBoundedSize();
    if (index == 0U || index > m_saved.size()) {
      throw MarshalFormatError("saved stream element index is out of range");
    }
    const ValuePtr &stored = m_saved[index - 1U];
    if (!stored) {
      throw MarshalFormatError("saved stream element was not stored");
    }
    return stored;
  }

  ValuePtr DecodeCPicked(std::uint32_t len) {
    if (len > MAX_STRING_BYTES) {
      throw MarshalLimitError("pickle length exceeds the limit");
    }
    return Value::CPicked(m_reader.ReadBytes(len));
  }

  ValuePtr DecodePackedRow(std::uint32_t depth) {
    ValuePtr header = DecodeValue(depth + 1U);
    if (!header->IsObjectEx()) {
      throw MarshalFormatError("packed row header is not an object ex");
    }
    const auto columns = ExtractColumns(*header);
    const std::uint32_t rle_len = ReadBoundedSize();
    if (rle_len > kMaxPackedRowBytes) {
      throw MarshalLimitError("packed row RLE length exceeds the limit");
    }
    const auto rle = m_reader.ReadBytes(rle_len);
    const auto unpacked = ZeroDecompress(rle, columns);
    std::vector<ValuePtr> values;
    values.reserve(columns.size());

    std::vector<std::pair<std::uint32_t, std::size_t>> size_order;
    for (std::size_t i = 0U; i < columns.size(); ++i) {
      size_order.emplace_back(DbTypeSizeBits(columns[i].type), i);
    }
    std::stable_sort(size_order.begin(), size_order.end(),
                     [](const auto &left, const auto &right) {
                       return left.first > right.first;
                     });

    std::size_t bool_count = 0U;
    for (const auto &[size, index] : size_order) {
      if (columns[index].type == DbType::Bool) {
        ++bool_count;
      }
    }
    const std::size_t bit_bytes = (bool_count + columns.size() + 7U) / 8U;
    if (unpacked.size() < bit_bytes) {
      throw MarshalTruncatedError("packed row bit data is truncated");
    }
    const std::size_t data_bytes = unpacked.size() - bit_bytes;
    std::size_t offset = 0U;
    std::size_t bool_offset = 0U;
    values.resize(columns.size());

    for (const auto &[size, index] : size_order) {
      const DbType type = columns[index].type;
      if (size == 0U) {
        continue;
      }
      const std::size_t null_bit = index + bool_count;
      const bool is_null =
          (unpacked[data_bytes + null_bit / 8U] &
           (1U << (null_bit % 8U))) != 0U;
      if (type == DbType::Bool) {
        if (is_null) {
          values[index] = Value::None();
        } else {
          const bool bit =
              (unpacked[data_bytes + bool_offset / 8U] &
               (1U << (bool_offset % 8U))) != 0U;
          values[index] = Value::Bool(bit);
        }
        ++bool_offset;
        continue;
      }
      if (is_null) {
        values[index] = Value::None();
        offset += size / 8U;
        continue;
      }
      values[index] = ReadPackedScalar(unpacked, offset, type);
      offset += size / 8U;
    }

    for (const auto &[size, index] : size_order) {
      if (size == 0U) {
        values[index] = DecodeValue(depth + 1U);
      }
    }

    PackedRowData row;
    row.columns = columns;
    row.values = std::move(values);
    return Value::PackedRow(std::move(row));
  }

  static std::vector<PackedColumn> ExtractColumns(const Value &header) {
    const auto &data = header.ObjectExValue();
    if (!data.header->IsToken() ||
        data.header->TokenValue() != "blue.DBRowDescriptor") {
      throw MarshalFormatError("packed row header is not a row descriptor");
    }
    if (data.list.size() != 1U || !data.list[0]->IsTuple()) {
      throw MarshalFormatError("row descriptor args are malformed");
    }
    const auto &columns = data.list[0]->TupleValue();
    if (columns.size() > kMaxPackedRowColumns) {
      throw MarshalLimitError("row descriptor column count exceeds the limit");
    }
    std::vector<PackedColumn> result;
    result.reserve(columns.size());
    for (const auto &column : columns) {
      if (!column->IsTuple() || column->TupleValue().size() != 2U ||
          !column->TupleValue()[0]->IsString() ||
          !column->TupleValue()[1]->IsInt()) {
        throw MarshalFormatError("row descriptor column is malformed");
      }
      PackedColumn entry;
      entry.name = column->TupleValue()[0]->StringValue();
      entry.type = static_cast<DbType>(
          static_cast<std::uint8_t>(column->TupleValue()[1]->IntValue()));
      result.push_back(std::move(entry));
    }
    return result;
  }

  static ValuePtr ReadPackedScalar(const std::vector<std::uint8_t> &data,
                                   std::size_t offset, DbType type) {
    switch (type) {
      case DbType::Cy:
      case DbType::I8:
      case DbType::Ui8:
      case DbType::FileTime:
        return Value::Int(ReadI64At(data, offset));
      case DbType::I4:
        return Value::Int(ReadI32At(data, offset));
      case DbType::Ui4:
        return Value::Int(ReadU32At(data, offset));
      case DbType::R4: {
        const std::uint32_t bits = ReadU32At(data, offset);
        float f = 0.0F;
        static_assert(sizeof(bits) == sizeof(f));
        std::memcpy(&f, &bits, sizeof(f));
        return Value::Real(f);
      }
      case DbType::R8: {
        const std::uint64_t bits = ReadU64At(data, offset);
        double d = 0.0;
        static_assert(sizeof(bits) == sizeof(d));
        std::memcpy(&d, &bits, sizeof(d));
        return Value::Real(d);
      }
      case DbType::I2:
        return Value::Int(ReadI16At(data, offset));
      case DbType::Ui2:
        return Value::Int(ReadU16At(data, offset));
      case DbType::I1:
        return Value::Int(static_cast<std::int8_t>(data[offset]));
      case DbType::Ui1:
        return Value::Int(data[offset]);
      case DbType::Bool:
      case DbType::Empty:
      case DbType::Error:
      case DbType::Bytes:
      case DbType::Str:
      case DbType::WStr:
        throw MarshalFormatError("packed row scalar has no fixed size");
    }
    throw MarshalFormatError("packed row scalar has an unknown type");
  }

  static std::uint16_t ReadU16At(const std::vector<std::uint8_t> &data,
                                 std::size_t offset) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[offset]) |
        (static_cast<std::uint16_t>(data[offset + 1U]) << 8U));
  }

  static std::int16_t ReadI16At(const std::vector<std::uint8_t> &data,
                                std::size_t offset) {
    return static_cast<std::int16_t>(ReadU16At(data, offset));
  }

  static std::uint32_t ReadU32At(const std::vector<std::uint8_t> &data,
                                 std::size_t offset) {
    std::uint32_t value = 0U;
    for (std::size_t i = 0U; i < 4U; ++i) {
      value |= static_cast<std::uint32_t>(data[offset + i]) << (i * 8U);
    }
    return value;
  }

  static std::int32_t ReadI32At(const std::vector<std::uint8_t> &data,
                                std::size_t offset) {
    return static_cast<std::int32_t>(ReadU32At(data, offset));
  }

  static std::uint64_t ReadU64At(const std::vector<std::uint8_t> &data,
                                 std::size_t offset) {
    std::uint64_t value = 0U;
    for (std::size_t i = 0U; i < 8U; ++i) {
      value |= static_cast<std::uint64_t>(data[offset + i]) << (i * 8U);
    }
    return value;
  }

  static std::int64_t ReadI64At(const std::vector<std::uint8_t> &data,
                                std::size_t offset) {
    return static_cast<std::int64_t>(ReadU64At(data, offset));
  }

  static std::vector<std::uint8_t> ZeroDecompress(
      const std::vector<std::uint8_t> &rle,
      const std::vector<PackedColumn> &columns) {
    std::size_t fixed_bits = 0U;
    std::size_t bool_count = 0U;
    for (const auto &column : columns) {
      if (column.type == DbType::Bool) {
        ++bool_count;
      } else {
        fixed_bits += DbTypeSizeBits(column.type);
      }
    }
    const std::size_t fixed_bytes = fixed_bits / 8U;
    const std::size_t bit_bytes =
        (bool_count + columns.size() + 7U) / 8U;
    const std::size_t expected = fixed_bytes + bit_bytes;
    std::vector<std::uint8_t> out;
    out.reserve(expected);
    std::size_t i = 0U;
    while (i < rle.size() && out.size() < expected) {
      const std::uint8_t control = rle[i++];
      for (std::size_t nibble = 0U; nibble < 2U; ++nibble) {
        if (out.size() >= expected) {
          break;
        }
        const std::uint8_t encoded = static_cast<std::uint8_t>(
            (control >> (nibble * 4U)) & 0x0FU);
        if (encoded < 8U) {
          const std::size_t run = 8U - encoded;
          if (i + run > rle.size()) {
            throw MarshalTruncatedError("packed row RLE is truncated");
          }
          out.insert(out.end(), rle.begin() + static_cast<std::ptrdiff_t>(i),
                     rle.begin() + static_cast<std::ptrdiff_t>(i + run));
          i += run;
        } else {
          const std::size_t run = encoded - 7U;
          out.insert(out.end(), run, 0U);
        }
      }
    }
    if (out.size() < expected) {
      throw MarshalTruncatedError("packed row data is truncated");
    }
    return out;
  }

  static void AppendUtf16Unit(std::string &out, std::uint16_t unit) {
    if (unit < 0x80U) {
      out.push_back(static_cast<char>(unit));
    } else if (unit < 0x800U) {
      out.push_back(static_cast<char>(0xC0U | (unit >> 6U)));
      out.push_back(static_cast<char>(0x80U | (unit & 0x3FU)));
    } else {
      out.push_back(static_cast<char>(0xE0U | (unit >> 12U)));
      out.push_back(static_cast<char>(0x80U | ((unit >> 6U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | (unit & 0x3FU)));
    }
  }

  std::uint32_t ReadBoundedSize() {
    const std::uint32_t size = m_reader.ReadSizeEx();
    if (size > MAX_STRING_BYTES) {
      throw MarshalLimitError("marshal size exceeds the limit");
    }
    return size;
  }

  std::uint32_t ReadBoundedCount() {
    const std::uint32_t count = m_reader.ReadSizeEx();
    if (count > MAX_CONTAINER_COUNT) {
      throw MarshalLimitError("marshal container count exceeds the limit");
    }
    return count;
  }

  std::uint32_t ReadSaveIndex() {
    if (m_saved.empty()) {
      throw MarshalFormatError("marshal save flag set without a store");
    }
    if (m_reader.Offset() + 4U > m_value_end) {
      throw MarshalTruncatedError("marshal save table is truncated");
    }
    return m_reader.ReadU32();
  }

  Reader m_reader;
  std::vector<ValuePtr> m_saved;
  std::size_t m_value_end = 0U;
};

} // namespace

std::uint32_t DbTypeSizeBits(DbType type) noexcept {
  return DbTypeSizeBitsImpl(type);
}

ValuePtr Value::None() { return ValuePtr(new Value(ValueKind::None)); }

ValuePtr Value::Bool(bool value) {
  ValuePtr result(new Value(ValueKind::Bool));
  result->m_bool = value;
  return result;
}

ValuePtr Value::Int(std::int64_t value) {
  ValuePtr result(new Value(ValueKind::Int));
  result->m_int = value;
  return result;
}

ValuePtr Value::Real(double value) {
  ValuePtr result(new Value(ValueKind::Real));
  result->m_real = value;
  return result;
}

ValuePtr Value::String(std::string value) {
  ValuePtr result(new Value(ValueKind::String));
  result->m_string = std::move(value);
  return result;
}

ValuePtr Value::WString(std::string value) {
  ValuePtr result(new Value(ValueKind::WString));
  result->m_string = std::move(value);
  return result;
}

ValuePtr Value::Token(std::string value) {
  ValuePtr result(new Value(ValueKind::Token));
  result->m_string = std::move(value);
  return result;
}

ValuePtr Value::Buffer(std::vector<std::uint8_t> value) {
  ValuePtr result(new Value(ValueKind::Buffer));
  result->m_bytes = std::move(value);
  return result;
}

ValuePtr Value::Tuple(std::vector<ValuePtr> items) {
  ValuePtr result(new Value(ValueKind::Tuple));
  result->m_items = std::move(items);
  return result;
}

ValuePtr Value::List(std::vector<ValuePtr> items) {
  ValuePtr result(new Value(ValueKind::List));
  result->m_items = std::move(items);
  return result;
}

ValuePtr Value::Dict(std::vector<DictEntry> entries) {
  ValuePtr result(new Value(ValueKind::Dict));
  result->m_entries = std::move(entries);
  return result;
}

ValuePtr Value::Object(std::string type, ValuePtr args) {
  ValuePtr result(new Value(ValueKind::Object));
  result->m_object_type = std::move(type);
  result->m_object_args = std::move(args);
  return result;
}

ValuePtr Value::ObjectEx(bool is_type2, ObjectExData data) {
  ValuePtr result(new Value(ValueKind::ObjectEx));
  result->m_bool = is_type2;
  result->m_object_ex = std::move(data);
  return result;
}

ValuePtr Value::SubStruct(ValuePtr value) {
  ValuePtr result(new Value(ValueKind::SubStruct));
  result->m_sub_value = std::move(value);
  return result;
}

ValuePtr Value::SubStream(std::vector<std::uint8_t> data) {
  ValuePtr result(new Value(ValueKind::SubStream));
  result->m_bytes = std::move(data);
  return result;
}

ValuePtr Value::ChecksumedStream(std::uint32_t checksum, ValuePtr value) {
  ValuePtr result(new Value(ValueKind::ChecksumedStream));
  result->m_checksum = checksum;
  result->m_sub_value = std::move(value);
  return result;
}

ValuePtr Value::PackedRow(PackedRowData data) {
  ValuePtr result(new Value(ValueKind::PackedRow));
  result->m_packed_row = std::move(data);
  return result;
}

ValuePtr Value::CPicked(std::vector<std::uint8_t> data) {
  ValuePtr result(new Value(ValueKind::CPicked));
  result->m_bytes = std::move(data);
  return result;
}

bool Value::BoolValue() const {
  if (m_kind != ValueKind::Bool) {
    throw MarshalFormatError("value is not a bool");
  }
  return m_bool;
}

std::int64_t Value::IntValue() const {
  if (m_kind != ValueKind::Int) {
    throw MarshalFormatError("value is not an int");
  }
  return m_int;
}

double Value::RealValue() const {
  if (m_kind != ValueKind::Real) {
    throw MarshalFormatError("value is not a real");
  }
  return m_real;
}

const std::string &Value::StringValue() const {
  if (m_kind != ValueKind::String) {
    throw MarshalFormatError("value is not a string");
  }
  return m_string;
}

const std::string &Value::WStringValue() const {
  if (m_kind != ValueKind::WString) {
    throw MarshalFormatError("value is not a wide string");
  }
  return m_string;
}

const std::string &Value::TokenValue() const {
  if (m_kind != ValueKind::Token) {
    throw MarshalFormatError("value is not a token");
  }
  return m_string;
}

const std::vector<std::uint8_t> &Value::BufferValue() const {
  if (m_kind != ValueKind::Buffer) {
    throw MarshalFormatError("value is not a buffer");
  }
  return m_bytes;
}

const std::vector<ValuePtr> &Value::TupleValue() const {
  if (m_kind != ValueKind::Tuple) {
    throw MarshalFormatError("value is not a tuple");
  }
  return m_items;
}

const std::vector<ValuePtr> &Value::ListValue() const {
  if (m_kind != ValueKind::List) {
    throw MarshalFormatError("value is not a list");
  }
  return m_items;
}

const std::vector<DictEntry> &Value::DictValue() const {
  if (m_kind != ValueKind::Dict) {
    throw MarshalFormatError("value is not a dict");
  }
  return m_entries;
}

const std::string &Value::ObjectType() const {
  if (m_kind != ValueKind::Object) {
    throw MarshalFormatError("value is not an object");
  }
  return m_object_type;
}

const ValuePtr &Value::ObjectArgs() const {
  if (m_kind != ValueKind::Object) {
    throw MarshalFormatError("value is not an object");
  }
  return m_object_args;
}

const ObjectExData &Value::ObjectExValue() const {
  if (m_kind != ValueKind::ObjectEx) {
    throw MarshalFormatError("value is not an object ex");
  }
  return m_object_ex;
}

const ValuePtr &Value::SubStructValue() const {
  if (m_kind != ValueKind::SubStruct) {
    throw MarshalFormatError("value is not a sub struct");
  }
  return m_sub_value;
}

const std::vector<std::uint8_t> &Value::SubStreamData() const {
  if (m_kind != ValueKind::SubStream) {
    throw MarshalFormatError("value is not a sub stream");
  }
  return m_bytes;
}

const PackedRowData &Value::PackedRowValue() const {
  if (m_kind != ValueKind::PackedRow) {
    throw MarshalFormatError("value is not a packed row");
  }
  return m_packed_row;
}

std::uint32_t Value::Checksum() const {
  if (m_kind != ValueKind::ChecksumedStream) {
    throw MarshalFormatError("value is not a checksumed stream");
  }
  return m_checksum;
}

const ValuePtr &Value::ChecksumedValue() const {
  if (m_kind != ValueKind::ChecksumedStream) {
    throw MarshalFormatError("value is not a checksumed stream");
  }
  return m_sub_value;
}

const std::vector<std::uint8_t> &Value::CPickedData() const {
  if (m_kind != ValueKind::CPicked) {
    throw MarshalFormatError("value is not a picked value");
  }
  return m_bytes;
}

std::vector<std::uint8_t> Encode(const Value &value) {
  Encoder encoder;
  return encoder.Encode(value);
}

ValuePtr Decode(const std::vector<std::uint8_t> &stream,
                bool require_exact_end) {
  if (stream.size() > MAX_STREAM_BYTES) {
    throw MarshalLimitError("marshal stream exceeds the size limit");
  }
  Decoder decoder(stream);
  return decoder.Decode(require_exact_end);
}

} // namespace ithax::network::marshal
