#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace ithax::network::marshal {

constexpr std::uint8_t STRING_TABLE_ERROR = 0U;
constexpr std::size_t STRING_TABLE_SIZE = 195U;

std::uint32_t Djb2Hash(const std::string &text) noexcept;

std::optional<std::uint8_t> LookupStringIndex(const std::string &text);
std::optional<std::string> LookupString(std::uint8_t index);

} // namespace ithax::network::marshal
