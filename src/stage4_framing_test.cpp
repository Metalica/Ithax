#include "network/framing.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using ithax::network::EncodeFrame;
using ithax::network::FrameDecoder;
using ithax::network::FrameError;
using ithax::network::FrameTooLargeError;

class TestError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

void Require(bool condition, const char *message) {
  if (!condition) {
    throw TestError(message);
  }
}

void TestRoundTrip() {
  FrameDecoder decoder;
  const std::vector<std::uint8_t> payload = {0x7EU, 0x00U, 0x00U, 0x00U,
                                             0x00U, 0x01U};
  const std::vector<std::uint8_t> frame = EncodeFrame(payload);
  Require(frame.size() == payload.size() + 4U,
          "frame length prefix is missing");
  Require(frame[0] == 0x06U && frame[1] == 0x00U && frame[2] == 0x00U &&
              frame[3] == 0x00U,
          "frame length prefix is wrong");
  decoder.Push(frame);
  const auto popped = decoder.TryPopFrame();
  Require(popped.has_value(), "frame did not decode");
  Require(popped.value() == payload, "frame payload changed");
  Require(decoder.BufferedBytes() == 0U, "decoder retained bytes");
  std::cout << "{\"event\":\"stage4_framing_roundtrip\",\"status\":\"pass\""
            << "}\n";
}

void TestPartialFrames() {
  FrameDecoder decoder;
  const std::vector<std::uint8_t> payload = {1U, 2U, 3U, 4U, 5U, 6U, 7U};
  const std::vector<std::uint8_t> frame = EncodeFrame(payload);
  bool popped_early = false;
  std::optional<std::vector<std::uint8_t>> popped;
  for (std::size_t i = 0U; i < frame.size(); ++i) {
    decoder.Push({frame[i]});
    const auto candidate = decoder.TryPopFrame();
    if (candidate.has_value()) {
      if (i + 1U != frame.size()) {
        popped_early = true;
      }
      popped = candidate;
    }
  }
  Require(!popped_early, "partial frame decoded early");
  Require(popped.has_value() && popped.value() == payload,
          "partial frame did not assemble");
  std::cout << "{\"event\":\"stage4_framing_partial\",\"status\":\"pass\"}\n";
}

void TestCoalescedFrames() {
  FrameDecoder decoder;
  const std::vector<std::uint8_t> first = {0x01U};
  const std::vector<std::uint8_t> second = {0x02U, 0x03U};
  std::vector<std::uint8_t> combined = EncodeFrame(first);
  const std::vector<std::uint8_t> second_frame = EncodeFrame(second);
  combined.insert(combined.end(), second_frame.begin(), second_frame.end());
  decoder.Push(combined);
  const auto first_pop = decoder.TryPopFrame();
  const auto second_pop = decoder.TryPopFrame();
  Require(first_pop.has_value() && first_pop.value() == first,
          "coalesced first frame is wrong");
  Require(second_pop.has_value() && second_pop.value() == second,
          "coalesced second frame is wrong");
  Require(!decoder.TryPopFrame().has_value(),
          "coalesced decoder produced an extra frame");
  std::cout << "{\"event\":\"stage4_framing_coalesced\",\"status\":\"pass\""
            << "}\n";
}

void TestOversizedFrame() {
  FrameDecoder decoder;
  std::vector<std::uint8_t> oversized(ithax::network::MAX_FRAME_PAYLOAD_BYTES +
                                       1U, 0U);
  bool rejected = false;
  try {
    EncodeFrame(oversized);
  } catch (const FrameTooLargeError &) {
    rejected = true;
  }
  Require(rejected, "oversized frame was accepted");

  std::vector<std::uint8_t> header = {0xFFU, 0xFFU, 0xFFU, 0x7FU};
  bool header_rejected = false;
  try {
    decoder.Push(header);
    decoder.TryPopFrame();
  } catch (const FrameTooLargeError &) {
    header_rejected = true;
  }
  Require(header_rejected, "oversized frame header was accepted");
  std::cout << "{\"event\":\"stage4_framing_oversized\",\"status\":\"pass\""
            << "}\n";
}

} // namespace

int main() {
  try {
    TestRoundTrip();
    TestPartialFrames();
    TestCoalescedFrames();
    TestOversizedFrame();
    std::cout << "{\"event\":\"stage4_framing_suite\",\"status\":\"pass\"}\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "stage4 framing suite failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
