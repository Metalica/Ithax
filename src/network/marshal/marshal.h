#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ithax::network::marshal {

constexpr std::uint8_t MARSHAL_HEADER_BYTE = 0x7EU;
constexpr std::uint8_t OPCODE_MASK = 0x3FU;
constexpr std::uint8_t SAVE_MASK = 0x40U;
constexpr std::uint8_t UNKNOWN_MASK = 0x80U;
constexpr std::uint32_t MAX_MARSHAL_DEPTH = 64U;
constexpr std::uint32_t MAX_CONTAINER_COUNT = 65'536U;
constexpr std::uint32_t MAX_STRING_BYTES = 1U * 1024U * 1024U;
constexpr std::uint32_t MAX_STREAM_BYTES = 1U * 1024U * 1024U;
constexpr std::uint32_t MAX_SAVED_OBJECTS = 65'536U;

enum class Opcode : std::uint8_t {
  PyNone = 0x01,
  PyToken = 0x02,
  PyLongLong = 0x03,
  PyLong = 0x04,
  PySignedShort = 0x05,
  PyByte = 0x06,
  PyMinusOne = 0x07,
  PyZeroInteger = 0x08,
  PyOneInteger = 0x09,
  PyReal = 0x0A,
  PyZeroReal = 0x0B,
  PyBuffer = 0x0D,
  PyEmptyString = 0x0E,
  PyCharString = 0x0F,
  PyShortString = 0x10,
  PyStringTableItem = 0x11,
  PyWStringUCS2 = 0x12,
  PyLongString = 0x13,
  PyTuple = 0x14,
  PyList = 0x15,
  PyDict = 0x16,
  PyObject = 0x17,
  PySubStruct = 0x19,
  PySavedStreamElement = 0x1B,
  PyChecksumedStream = 0x1C,
  PyTrue = 0x1F,
  PyFalse = 0x20,
  cPicked = 0x21,
  PyObjectEx1 = 0x22,
  PyObjectEx2 = 0x23,
  PyEmptyTuple = 0x24,
  PyOneTuple = 0x25,
  PyEmptyList = 0x26,
  PyOneList = 0x27,
  PyEmptyWString = 0x28,
  PyWStringUCS2Char = 0x29,
  PyPackedRow = 0x2A,
  PySubStream = 0x2B,
  PyTwoTuple = 0x2C,
  PackedTerminator = 0x2D,
  PyWStringUTF8 = 0x2E,
  PyVarInteger = 0x2F,
};

enum class DbType : std::uint8_t {
  Empty = 0x00,
  I2 = 0x02,
  I4 = 0x03,
  R4 = 0x04,
  R8 = 0x05,
  Cy = 0x06,
  Error = 0x0A,
  Bool = 0x0B,
  I1 = 0x10,
  Ui1 = 0x11,
  Ui2 = 0x12,
  Ui4 = 0x13,
  I8 = 0x14,
  Ui8 = 0x15,
  FileTime = 0x40,
  Bytes = 0x80,
  Str = 0x81,
  WStr = 0x82,
};

std::uint32_t DbTypeSizeBits(DbType type) noexcept;

class MarshalError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class MarshalTruncatedError : public MarshalError {
public:
  using MarshalError::MarshalError;
};

class MarshalDepthError : public MarshalError {
public:
  using MarshalError::MarshalError;
};

class MarshalLimitError : public MarshalError {
public:
  using MarshalError::MarshalError;
};

class MarshalFormatError : public MarshalError {
public:
  using MarshalError::MarshalError;
};

class MarshalStringTableError : public MarshalError {
public:
  using MarshalError::MarshalError;
};

class Value;
using ValuePtr = std::shared_ptr<Value>;

enum class ValueKind {
  None,
  Bool,
  Int,
  Real,
  String,
  WString,
  Token,
  Buffer,
  Tuple,
  List,
  Dict,
  Object,
  ObjectEx,
  SubStruct,
  SubStream,
  ChecksumedStream,
  PackedRow,
  CPicked,
};

struct DictEntry {
  ValuePtr key;
  ValuePtr value;
};

struct ObjectExData {
  ValuePtr header;
  std::vector<ValuePtr> list;
  std::vector<DictEntry> dict;
};

struct PackedColumn {
  std::string name;
  DbType type = DbType::Empty;
};

struct PackedRowData {
  std::vector<PackedColumn> columns;
  std::vector<ValuePtr> values;
};

class Value {
public:
  static ValuePtr None();
  static ValuePtr Bool(bool value);
  static ValuePtr Int(std::int64_t value);
  static ValuePtr Real(double value);
  static ValuePtr String(std::string value);
  static ValuePtr WString(std::string value);
  static ValuePtr Token(std::string value);
  static ValuePtr Buffer(std::vector<std::uint8_t> value);
  static ValuePtr Tuple(std::vector<ValuePtr> items);
  static ValuePtr List(std::vector<ValuePtr> items);
  static ValuePtr Dict(std::vector<DictEntry> entries);
  static ValuePtr Object(std::string type, ValuePtr args);
  static ValuePtr ObjectEx(bool is_type2, ObjectExData data);
  static ValuePtr SubStruct(ValuePtr value);
  static ValuePtr SubStream(std::vector<std::uint8_t> data);
  static ValuePtr ChecksumedStream(std::uint32_t checksum, ValuePtr value);
  static ValuePtr PackedRow(PackedRowData data);
  static ValuePtr CPicked(std::vector<std::uint8_t> data);

  ValueKind Kind() const noexcept { return m_kind; }
  bool IsNone() const noexcept { return m_kind == ValueKind::None; }
  bool IsBool() const noexcept { return m_kind == ValueKind::Bool; }
  bool IsInt() const noexcept { return m_kind == ValueKind::Int; }
  bool IsReal() const noexcept { return m_kind == ValueKind::Real; }
  bool IsString() const noexcept { return m_kind == ValueKind::String; }
  bool IsWString() const noexcept { return m_kind == ValueKind::WString; }
  bool IsToken() const noexcept { return m_kind == ValueKind::Token; }
  bool IsBuffer() const noexcept { return m_kind == ValueKind::Buffer; }
  bool IsTuple() const noexcept { return m_kind == ValueKind::Tuple; }
  bool IsList() const noexcept { return m_kind == ValueKind::List; }
  bool IsDict() const noexcept { return m_kind == ValueKind::Dict; }
  bool IsObject() const noexcept { return m_kind == ValueKind::Object; }
  bool IsObjectEx() const noexcept { return m_kind == ValueKind::ObjectEx; }
  bool IsObjectEx2() const noexcept {
    return m_kind == ValueKind::ObjectEx && m_bool;
  }
  bool IsSubStruct() const noexcept { return m_kind == ValueKind::SubStruct; }
  bool IsSubStream() const noexcept { return m_kind == ValueKind::SubStream; }
  bool IsPackedRow() const noexcept { return m_kind == ValueKind::PackedRow; }
  bool IsCPicked() const noexcept { return m_kind == ValueKind::CPicked; }

  bool BoolValue() const;
  std::int64_t IntValue() const;
  double RealValue() const;
  const std::string &StringValue() const;
  const std::string &WStringValue() const;
  const std::string &TokenValue() const;
  const std::vector<std::uint8_t> &BufferValue() const;
  const std::vector<ValuePtr> &TupleValue() const;
  const std::vector<ValuePtr> &ListValue() const;
  const std::vector<DictEntry> &DictValue() const;
  const std::string &ObjectType() const;
  const ValuePtr &ObjectArgs() const;
  const ObjectExData &ObjectExValue() const;
  const ValuePtr &SubStructValue() const;
  const std::vector<std::uint8_t> &SubStreamData() const;
  const PackedRowData &PackedRowValue() const;
  std::uint32_t Checksum() const;
  const ValuePtr &ChecksumedValue() const;
  const std::vector<std::uint8_t> &CPickedData() const;

private:
  explicit Value(ValueKind kind) : m_kind(kind) {}

  ValueKind m_kind;
  bool m_bool = false;
  std::int64_t m_int = 0;
  double m_real = 0.0;
  std::string m_string;
  std::vector<std::uint8_t> m_bytes;
  std::vector<ValuePtr> m_items;
  std::vector<DictEntry> m_entries;
  std::string m_object_type;
  ValuePtr m_object_args;
  ObjectExData m_object_ex;
  ValuePtr m_sub_value;
  std::uint32_t m_checksum = 0U;
  PackedRowData m_packed_row;
};

std::vector<std::uint8_t> Encode(const Value &value);
ValuePtr Decode(const std::vector<std::uint8_t> &stream,
                bool require_exact_end);

} // namespace ithax::network::marshal
