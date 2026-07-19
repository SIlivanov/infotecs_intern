/**
 * @file utils.h
 * @brief Реализация утилит разбора аргументов командной строки и сетевых
 * хелперов
 */

#pragma once

#include <iostream>
#include <string>
#include <vector>

/**
 * @struct AppConfig
 * @brief Структура для хранения параметров запуска приложения
 */
struct AppConfig {
    std::string ip = "127.0.0.1";  ///< ip адресс для подключения
    int port = 1313;               ///< Порт для подключения
    size_t chunk_size = 4096;      ///< Размер порции(чанка)
    std::string src_path;          ///< Путь к файлу на источнике
    std::string dst_path;          ///< Путь к файлу у получателя
    bool is_upload = false;  ///< Направление: true - на сервер, false - с сервера
};

/**
 * @brief Отправка заголовка и тела сетевого пакета
 * @param socket_fd Файловый дескриптор получателя
 * @param type Тип сообщения
 * @param data Указатель на буффер с данными
 * @param length Размер буффера в байтах
 * @return true при успешной отправке, false в противном случае
 */
inline bool send_packet(int socket_fd, CommandType type, const void* data, uint64_t length) {
    PacketHeader header{type, length};
    if (send(socket_fd, &header, sizeof(header), 0) <= 0) return false;
    if (length > 0 && data != nullptr) {
        if (send(socket_fd, data, length, 0) <= 0) return false;
    }
    return true;
}

/**
 * @brief Принимает из сокета сообщение и сразу превращает её в строку
 * @param socket_fd Файловый дескриптор отправителя
 * @param length Размер принимаемой строки в байтах
 * @return Полученная строка
 */
inline std::string recv_string(int socket_fd, uint64_t length) {
    if (length == 0) return "";
    std::vector<char> buffer(length);
    // Получаем строку целиком (MSG_WAITALL)
    recv(socket_fd, buffer.data(), length, MSG_WAITALL);
    return std::string(buffer.begin(), buffer.end());
}

/**
 * @brief Разделяет строку адреса формата "IP:Путь" на составляющие
 * @param arg Разделяемая строка
 * @param ip Переменная где сохранится адрес
 * @param path Переменная где сохранится путь
 * @return true если строка соответствует формату, false в противном случае
 */
inline bool parse_address(const std::string& arg, std::string& ip, std::string& path) {
    size_t pos = arg.find(':');
    if (pos == std::string::npos) {
        return false;
    }

    ip = arg.substr(0, pos);
    path = arg.substr(pos + 1);

    return true;
}

/**
 * @brief Разбирает аргументы командной строки
 * @param argc Количество аргументов
 * @param argv Массив аргументов
 * @param config Структура со всемы параметрами
 * @param is_client Флаг для режима разбора: true - клиент, false - сервер
 * @return true при успешном разборе, false в противном случае
 */
inline bool parse_arguments(int argc, char* argv[], AppConfig& config, bool is_client) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-p" && (i + 1) < argc) {
            config.port = std::stoi(argv[++i]);
        } else if (arg == "-chunk" && (i + 1) < argc) {
            config.chunk_size = std::stoul(argv[++i]);
        } else if (arg == "-src" && (i + 1) < argc) {
            config.src_path = argv[++i];
        } else if (arg == "-dst" && (i + 1) < argc) {
            config.dst_path = argv[++i];
        }
    }
    if (is_client) {
        if (config.src_path.empty() || config.dst_path.empty()) {
            std::cerr << "Error. -dst and -src parametrs required for client." << std::endl;
            return false;
        }

        std::string tmp_ip, tmp_path;
        if (parse_address(config.dst_path, tmp_ip,
                          tmp_path)) {  // -dst [ip]:[path] = загрузка на сервер
            config.is_upload = true;
            config.ip = tmp_ip;
            config.dst_path = tmp_path;
        } else if (parse_address(config.src_path, tmp_ip,
                                 tmp_path)) {  // -src [ip]:[path] = скачивание с сервера
            config.is_upload = false;
            config.ip = tmp_ip;
            config.src_path = tmp_path;
        } else {
            std::cerr << "Error. -dst or -src need to contain server "
                         "address([ip]:[path] format)"
                      << std::endl;
            return false;
        }
    }
    return true;
}