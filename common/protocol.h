#pragma once

#include <cstdint>

constexpr int DEFAULT_PORT = 1313;
constexpr std::size_t DEFAULT_CHUNK_SIZE = 4096;

enum class CommandType : uint8_t {
  DOWNLOAD_REQUEST = 1,
  UPLOAD_REQUEST = 2,
  FILE_INFO = 3,
  FILE_DATA = 4,
  ERROR_MSG = 5
};

#pragma pack(push, 1)
struct PacketHeader {
  CommandType type;
  uint64_t len;
};
#pragma pack(pop)
