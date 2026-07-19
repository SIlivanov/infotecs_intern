/**
 * @file protocol.h
 * @brief Определение структуры протокола сетевого обмена
 */
#pragma once

#include <cstdint>

constexpr int DEFAULT_PORT = 1313;                ///< Порт по умолчанию
constexpr std::size_t DEFAULT_CHUNK_SIZE = 4096;  ///< Размер порции по умолчанию

/**
 * @class CommandType
 * @brief Тип сетевой команды
 */
enum class CommandType : uint8_t {
    DOWNLOAD_REQUEST = 1,
    UPLOAD_REQUEST = 2,
    FILE_INFO = 3,
    FILE_DATA = 4,
    ERROR_MSG = 5
};

#pragma pack(push, 1)
/**
 * @struct PacketHeader
 * @brief Заголовок любого сетевого сообщения. Имеет размер ровно 9 байт
 */
struct PacketHeader {
    CommandType type;  ///< Тип команды
    uint64_t len;  ///< Длина сообщения, которое пойдёт после заголовка
};
#pragma pack(pop)
