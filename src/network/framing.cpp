#include "network/framing.h"

#include <cstring>

namespace ithax::network {

namespace {

std::uint32_t ReadU32Le(const std::vector<std::uint8_t> &bytes,
                        std::size_t offset) {
  std::uint32_t value = 0U;
  for (std::size_t i = 0U; i < 4U; ++i) {
    value |= static_cast<std::uint32_t>(bytes[offset + i]) << (i * 8U);
  }
  return value;
}

void AppendU32Le(std::vector<std::uint8_t> &out, std::uint32_t value) {
  for (std::size_t i = 0U; i < 4U; ++i) {
    out.push_back(static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU));
  }
}

} // namespace

std::vector<std::uint8_t> EncodeFrame(
    const std::vector<std::uint8_t> &payload) {
  if (payload.size() > MAX_FRAME_PAYLOAD_BYTES) {
    throw FrameTooLargeError("frame payload exceeds the size limit");
  }
  std::vector<std::uint8_t> frame;
  frame.reserve(FRAME_LENGTH_PREFIX_BYTES + payload.size());
  AppendU32Le(frame, static_cast<std::uint32_t>(payload.size()));
  frame.insert(frame.end(), payload.begin(), payload.end());
  return frame;
}

void FrameDecoder::Push(const std::vector<std::uint8_t> &bytes) {
  m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.end());
  if (m_buffer.size() > MAX_FRAME_PAYLOAD_BYTES + FRAME_LENGTH_PREFIX_BYTES) {
    throw FrameTooLargeError("buffered frame data exceeds the size limit");
  }
}

std::optional<std::vector<std::uint8_t>> FrameDecoder::TryPopFrame() {
  if (m_buffer.size() < FRAME_LENGTH_PREFIX_BYTES) {
    return std::nullopt;
  }
  const std::uint32_t length = ReadU32Le(m_buffer, 0U);
  if (length > MAX_FRAME_PAYLOAD_BYTES) {
    throw FrameTooLargeError("frame length exceeds the size limit");
  }
  if (m_buffer.size() < FRAME_LENGTH_PREFIX_BYTES + length) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> payload(
      m_buffer.begin() + static_cast<std::ptrdiff_t>(FRAME_LENGTH_PREFIX_BYTES),
      m_buffer.begin() +
          static_cast<std::ptrdiff_t>(FRAME_LENGTH_PREFIX_BYTES + length));
  m_buffer.erase(
      m_buffer.begin(),
      m_buffer.begin() +
          static_cast<std::ptrdiff_t>(FRAME_LENGTH_PREFIX_BYTES + length));
  return payload;
}

std::size_t FrameDecoder::BufferedBytes() const noexcept {
  return m_buffer.size();
}

} // namespace ithax::network
