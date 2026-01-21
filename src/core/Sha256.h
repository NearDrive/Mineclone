#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace core {

std::string Sha256Hex(const std::uint8_t* data, std::size_t size);
std::string Sha256Hex(const std::vector<std::uint8_t>& data);

} // namespace core
