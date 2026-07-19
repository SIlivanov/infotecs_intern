#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "../common/protocol.h"
#include "../common/utils.h"
#include "../libs/fileop_lib/fileop.h"

int server_descriptor = -1;
std::vector<int> clients;
std::mutex mtx;

void sigint_handler(int signum) {
  std::cout << "[SERVER] Interupt signal received. Closing connections"
            << std::endl;

  std::lock_guard<std::mutex> lock(mtx);
  for (int client_descriptor : clients) {
    close(client_descriptor);
  }

  if (server_descriptor != -1) {
    close(server_descriptor);
  }

  std::cout << "[SERVER] All connections are closed" << std::endl;
  exit(signum);
}

void remove_client(int client_descriptor) {
  std::lock_guard<std::mutex> lock(mtx);
  for (auto cur_client = clients.begin(); cur_client != clients.end();
       cur_client++) {
    if (*cur_client == client_descriptor) {
      clients.erase(cur_client);
      break;
    }
  }
  close(client_descriptor);
}

void send_error(int client_descriptor, const std::string& error_msg) {
  send_packet(client_descriptor, CommandType::ERROR_MSG, error_msg.data(),
              error_msg.size());
}

void handle_client(int client_descriptor, size_t chunk_size) {
  std::cout << "[SERVER] New client connected witd file descriptor "
            << client_descriptor << std::endl;

  PacketHeader header;
  ssize_t bytes_received = recv(client_descriptor, &header, sizeof(header), 0);
  if (bytes_received <= 0) {
    std::cerr << "Failed to read client pocket" << std::endl;
    remove_client(client_descriptor);
    return;
  }

  FileHandler fh;

  if (header.type == CommandType::DOWNLOAD_REQUEST) {
    std::string path = recv_string(client_descriptor, header.len);
    // Считываем желаемый размер чанка, который прислал клиент
    uint64_t client_chunk_size = 0;
    recv(client_descriptor, &client_chunk_size, sizeof(client_chunk_size),
         MSG_WAITALL);

    std::cout << "[SERVER] Client requested download file by path: " << path
              << std::endl;

    if (!fh.open_read(path)) {
      std::cerr << "[SERVER] File not found:" << path << std::endl;
      send_error(client_descriptor, "File not found on server.");
      remove_client(client_descriptor);
      return;
    }

    uint64_t file_size = fh.get_file_size(path);

    // Sending FILE_INFO to client
    send_packet(client_descriptor, CommandType::FILE_INFO, nullptr, file_size);

    std::vector<char> data_buffer;
    int64_t read_bytes = 0;
    while ((read_bytes = fh.read_chunk(data_buffer, client_chunk_size)) > 0) {
      if (!send_packet(client_descriptor, CommandType::FILE_DATA,
                       data_buffer.data(), read_bytes)) {
        break;
      }
    }
    fh.close();
    std::cout << "[SERVER] File succesfuly sent" << std::endl;
  } else if (header.type == CommandType::UPLOAD_REQUEST) {
    std::string path = recv_string(client_descriptor, header.len);

    std::cout << "[SERVER] Client requested upload file by path:" << path
              << std::endl;

    if (!fh.open_write(path)) {
      std::cerr << "[SERVER] Cannot write file:" << path << std::endl;
      send_error(client_descriptor, "Cannot write file.");
      remove_client(client_descriptor);
      return;
    }

    recv(client_descriptor, &header, sizeof(header), 0);
    if (header.type != CommandType::FILE_INFO) {
      std::cerr << "[SERVER] Protocol error, expected FILE_INFO" << std::endl;
      fh.remove_file(path);
      remove_client(client_descriptor);
      return;
    }

    uint64_t expected_size = header.len;
    uint64_t cur_size = 0;

    std::vector<char> data_buffer;

    while (cur_size < expected_size) {
      PacketHeader data_header;
      if (recv(client_descriptor, &data_header, sizeof(data_header), 0) <= 0)
        break;

      if (data_header.type != CommandType::FILE_DATA) {
        std::cerr << "[SERVER] Protocol error expected FILE_DATA" << std::endl;
        break;
      }

      data_buffer.resize(data_header.len);
      ssize_t chunk_bytes = recv(client_descriptor, data_buffer.data(),
                                 data_header.len, MSG_WAITALL);
      if (chunk_bytes <= 0) break;

      if (!fh.write_chunk(data_buffer, chunk_bytes)) {
        std::cerr << "[SERVER] Cannot write file on disk" << std::endl;
        break;
      }

      cur_size += chunk_bytes;
    }

    fh.close();

    if (cur_size < expected_size) {
      std::cerr << "[SERVER] Cannot get full file" << std::endl;
      fh.remove_file(path);
    } else {
      std::cout << "[SERVER] File succesfuly written" << std::endl;
    }
  }
  remove_client(client_descriptor);
}

int main(int argc, char* argv[]) {
  AppConfig config;
  if (!parse_arguments(argc, argv, config, false)) {
    return 1;
  }

  signal(SIGINT, sigint_handler);
  server_descriptor = socket(AF_INET, SOCK_STREAM, 0);
  if (server_descriptor == -1) {
    std::cerr << "Failed to create socket" << std::endl;
    return -1;
  }

  int opt = 1;

  setsockopt(server_descriptor, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(config.port);

  if (bind(server_descriptor, (struct sockaddr*)&address, sizeof(address)) <
      0) {
    std::cerr << "Bind failed, port " << config.port << std::endl;
    close(server_descriptor);
    return 1;
  }

  if (listen(server_descriptor, 10) < 0) {
    std::cerr << "Listen failed" << std::endl;
    close(server_descriptor);
    return 1;
  }

  std::cout << "[SERVER] Server started on port " << config.port << std::endl;

  while (true) {
    sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);

    int client_descriptor =
        accept(server_descriptor, (struct sockaddr*)&client_addr, &addr_len);
    if (client_descriptor < 0) {
      break;
    }

    {
      std::lock_guard<std::mutex> lock(mtx);
      clients.push_back(client_descriptor);
    }

    std::thread t(handle_client, client_descriptor, config.chunk_size);
    t.detach();
  }

  if (server_descriptor != -1) {
    close(server_descriptor);
  }

  return 0;
}