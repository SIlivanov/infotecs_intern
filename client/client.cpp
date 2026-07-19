#include <arpa/inet.h>
#include <dlfcn.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <vector>

#include "../common/protocol.h"
#include "../common/utils.h"
#include "../libs/fileop_lib/fileop.h"

int client_descriptor = -1;
std::string local_file_path;
bool is_download = false;
FileHandler fh;

typedef void (*print_progress_t)(uint64_t, uint64_t);
print_progress_t print_progress_fn = nullptr;
void* lib_handle = nullptr;

void sigint_handler(int signum) {
  std::cout << "\n[CLIENT] Transfer interrupted by user." << std::endl;

  fh.close();

  if (is_download && !local_file_path.empty()) {
    std::cout << "[CLIENT] Deleting incomplete file: " << local_file_path
              << std::endl;
    fh.remove_file(local_file_path);
  }

  if (client_descriptor != -1) {
    close(client_descriptor);
  }

  if (lib_handle) {
    dlclose(lib_handle);
  }

  exit(signum);
}

int main(int argc, char* argv[]) {
  AppConfig config;

  if (!parse_arguments(argc, argv, config, true)) {
    return 1;
  }

  signal(SIGINT, sigint_handler);

  local_file_path = config.is_upload ? config.src_path : config.dst_path;
  is_download = !config.is_upload;

  lib_handle = dlopen("./libprogress.so", RTLD_LAZY);
  if (!lib_handle) {
    std::cerr << "[CLIENT] Failed to load progress library" << dlerror()
              << std::endl;
    return 1;
  }

  // Загружаем указатель на функцию print_progress
  print_progress_fn = (print_progress_t)dlsym(lib_handle, "print_progress");
  if (!print_progress_fn) {
    std::cerr << "[CLIENT] Failed to find symbol 'print_progress': "
              << dlerror() << std::endl;
    dlclose(lib_handle);
    return 1;
  }

  client_descriptor = socket(AF_INET, SOCK_STREAM, 0);
  if (client_descriptor == -1) {
    std::cerr << "failed to create socket" << std::endl;
    dlclose(lib_handle);
    return 1;
  }

  sockaddr_in server_address{};
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(config.port);

  if (inet_pton(AF_INET, config.ip.c_str(), &server_address.sin_addr) <= 0) {
    std::cerr << "[CLIENT] Invalid IP address: " << config.ip << std::endl;
    close(client_descriptor);
    dlclose(lib_handle);
    return 1;
  }

  std::cout << "[CLIENT] Connecting to server " << config.ip << ":"
            << config.port << std::endl;

  if (connect(client_descriptor, (struct sockaddr*)&server_address,
              sizeof(server_address)) < 0) {
    std::cerr << "[CLIENT] Connection failed" << std::endl;
    close(client_descriptor);
    dlclose(lib_handle);
    return 1;
  }

  std::cout << "[CLIENT] Connected successfully." << std::endl;

  if (!config.is_upload) {
    std::cout << "[CLIENT] Mode: Downloading from server." << std::endl;

    // Отправляем запрос DOWNLOAD_REQUEST и путь к удаленному файлу
    send_packet(client_descriptor, CommandType::DOWNLOAD_REQUEST,
                config.src_path.data(), config.src_path.size());
    uint64_t client_chunk = config.chunk_size;
    send(client_descriptor, &client_chunk, sizeof(client_chunk), 0);

    // Ждем ответ от сервера (должен прийти FILE_INFO или ERROR_MSG)
    PacketHeader response_header;
    recv(client_descriptor, &response_header, sizeof(response_header), 0);

    if (response_header.type == CommandType::ERROR_MSG) {
      std::string err_msg = recv_string(client_descriptor, response_header.len);
      std::cerr << "[SERVER ERROR] " << err_msg << std::endl;

      close(client_descriptor);
      dlclose(lib_handle);
      return 1;
    }

    if (response_header.type != CommandType::FILE_INFO) {
      std::cerr << "[CLIENT] Protocol error. Expected FILE_INFO." << std::endl;
      close(client_descriptor);
      dlclose(lib_handle);
      return 1;
    }

    uint64_t total_size = response_header.len;
    uint64_t received_bytes = 0;

    if (!fh.open_write(config.dst_path)) {
      std::cerr << "[CLIENT] Failed to create local file: " << config.dst_path
                << std::endl;
      close(client_descriptor);
      dlclose(lib_handle);
      return 1;
    }

    std::vector<char> buffer;
    while (received_bytes < total_size) {
      PacketHeader data_header;
      if (recv(client_descriptor, &data_header, sizeof(data_header), 0) <= 0)
        break;

      if (data_header.type != CommandType::FILE_DATA) {
        std::cerr << "[CLIENT] Protocol error. Expected FILE_DATA."
                  << std::endl;
        break;
      }

      buffer.resize(data_header.len);
      ssize_t chunk_bytes =
          recv(client_descriptor, buffer.data(), data_header.len, MSG_WAITALL);
      if (chunk_bytes <= 0) break;

      if (!fh.write_chunk(buffer, chunk_bytes)) {
        std::cerr << "[CLIENT] Local write error!" << std::endl;
        break;
      }

      received_bytes += chunk_bytes;

      // Вызываем динамически загруженную функцию отрисовки прогресса
      print_progress_fn(received_bytes, total_size);
    }

    fh.close();

    if (received_bytes < total_size) {
      std::cerr << "[CLIENT] Download failed. Connection closed prematurely."
                << std::endl;
      fh.remove_file(config.dst_path);
    } else {
      std::cout << "[CLIENT] File successfully downloaded!" << std::endl;
    }
  } else {
    std::cout << "[CLIENT] Mode: Uploading to server." << std::endl;

    if (!fh.open_read(config.src_path)) {
      std::cerr << "[CLIENT] Failed to open local file: " << config.src_path
                << std::endl;
      close(client_descriptor);
      dlclose(lib_handle);
      return 1;
    }

    uint64_t total_size = fh.get_file_size(config.src_path);

    send_packet(client_descriptor, CommandType::UPLOAD_REQUEST,
                config.dst_path.data(), config.dst_path.size());

    send_packet(client_descriptor, CommandType::FILE_INFO, nullptr, total_size);

    std::vector<char> buffer;
    int64_t read_bytes = 0;
    uint64_t sent_bytes = 0;

    while ((read_bytes = fh.read_chunk(buffer, config.chunk_size)) > 0) {
      if (!send_packet(client_descriptor, CommandType::FILE_DATA, buffer.data(),
                       read_bytes)) {
        break;
      }
      sent_bytes += read_bytes;

      // На стороне отправки тоже можем красиво показывать прогресс
      print_progress_fn(sent_bytes, total_size);
    }

    fh.close();
    if (sent_bytes < total_size) {
      std::cerr << "[CLIENT] Upload failed/interrupted." << std::endl;
    } else {
      std::cout << "[CLIENT] File successfully uploaded!" << std::endl;
    }
  }

  close(client_descriptor);
  dlclose(lib_handle);
  return 0;
}
