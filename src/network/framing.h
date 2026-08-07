#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace ithax::network {

constexpr std::uint32_t MAX_FRAME_PAYLOAD_BYTES = 1U * 1024U * 1024U;
constexpr std::uint32_t DEFLATE_THRESHOLD_BYTES = 8U * 1024U;
constexpr std::size_t FRAME_LENGTH_PREFIX_BYTES = 4U;

class FrameError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class FrameTooLargeError : public FrameError {
public:
  using FrameError::FrameError;
};

class FrameTruncatedError : public FrameError {
public:
  using FrameError::FrameError;
};

class FrameFormatError : public FrameError {
public:
  using FrameError::FrameError;
};

std::vector<std::uint8_t> EncodeFrame(
    const std::vector<std::uint8_t> &payload);

class FrameDecoder {
public:
  void Push(const std::vector<std::uint8_t> &bytes);
  std::optional<std::vector<std::uint8_t>> TryPopFrame();
  std::size_t BufferedBytes() const noexcept;

private:
  std::vector<std::uint8_t> m_buffer;
};

} // namespace ithax::network
