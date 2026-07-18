#pragma once

#include <iostream>
#include <string>
#include <vector>

struct AppConfig {
  std::string ip = "127.0.0.1";
  int port = 1313;
  size_t chunk_size = 4096;
  std::string src_path;
  std::string dst_path;
  bool is_upload =
      false;  // true = отправка на сервер, false = скачивание с сервера
};

inline bool send_packet(int socket_fd, CommandType type, const void* data,
                        uint64_t length) {
  PacketHeader header{type, length};
  if (send(socket_fd, &header, sizeof(header), 0) <= 0) return false;
  if (length > 0 && data != nullptr) {
    if (send(socket_fd, data, length, 0) <= 0) return false;
  }
  return true;
}

inline std::string recv_string(int socket_fd, uint64_t length) {
  if (length == 0) return "";
  std::vector<char> buffer(length);
  // Получаем строку целиком (MSG_WAITALL)
  recv(socket_fd, buffer.data(), length, MSG_WAITALL);
  return std::string(buffer.begin(), buffer.end());
}

// Finding ip:path
inline bool parse_address(const std::string& arg, std::string& ip,
                          std::string& path) {
  size_t pos = arg.find(':');
  if (pos == std::string::npos) {
    return false;
  }

  ip = arg.substr(0, pos);
  path = arg.substr(pos + 1);

  return true;
}

inline bool parse_arguments(int argc, char* argv[], AppConfig& config,
                            bool is_client) {
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
      std::cerr << "Error. -dst and -src parametrs required for client."
                << std::endl;
      return false;
    }

    std::string tmp_ip, tmp_path;
    if (parse_address(config.dst_path, tmp_ip,
                      tmp_path)) {  // -dst [ip]:[path] = загрузка на сервер
      config.is_upload = true;
      config.ip = tmp_ip;
      config.dst_path = tmp_path;
    } else if (parse_address(
                   config.src_path, tmp_ip,
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