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

int server_descriptor = -1;  // Дескриптор главного слушающего сокета сервера
std::vector<int> clients;  // Вектор для отслеживания активных сокетов клиентов
std::mutex mtx;  // Мьютекс для синхронизации доступа к вектору клиентов из разных потоков

/**
 * @brief Обработчик сигнала Ctrl-C (SIGINT). Обеспечивает безопасное закрытие всех соединений перед
 * выходом
 */
void sigint_handler(int signum) {
    std::cout << "[SERVER] Interupt signal received. Closing connections" << std::endl;
    // Блокируем доступ к вектору сокетов, чтобы безопасно закрыть каждый из них
    std::lock_guard<std::mutex> lock(mtx);
    for (int client_descriptor : clients) {
        close(client_descriptor);
    }

    if (server_descriptor != -1) {
        close(server_descriptor);
    }

    std::cout << "[SERVER] All connections are closed" << std::endl;
    exit(signum);  // Завершаем процесс с кодом пришедшего сигнала
}

/**
 * @brief Удаляет дескриптор клиента из списка активных и закрывает сокет
 */
void remove_client(int client_descriptor) {
    std::lock_guard<std::mutex> lock(mtx);
    for (auto cur_client = clients.begin(); cur_client != clients.end(); cur_client++) {
        if (*cur_client == client_descriptor) {
            clients.erase(cur_client);
            break;
        }
    }
    close(client_descriptor);
}

/**
 * @brief Отправляет клиенту сообщение об ошибке в формате протокола
 */
void send_error(int client_descriptor, const std::string& error_msg) {
    send_packet(client_descriptor, CommandType::ERROR_MSG, error_msg.data(), error_msg.size());
}

/**
 * @brief Функция потока для обслуживания конкретного клиента
 */
void handle_client(int client_descriptor, size_t chunk_size) {
    std::cout << "[SERVER] New client connected witd file descriptor " << client_descriptor
              << std::endl;

    // Ожидаем самый первый пакет
    PacketHeader header;
    ssize_t bytes_received = recv(client_descriptor, &header, sizeof(header), 0);
    if (bytes_received <= 0) {
        std::cerr << "Failed to read client pocket" << std::endl;
        remove_client(client_descriptor);
        return;
    }

    FileHandler fh;  // Объект для работы с файлами

    // Запрос на скачивание с сервера
    if (header.type == CommandType::DOWNLOAD_REQUEST) {
        // Читаем путь к запрашиваемому файлу
        std::string path = recv_string(client_descriptor, header.len);

        // Считываем желаемый размер чанка, который прислал клиент
        uint64_t client_chunk_size = 0;
        recv(client_descriptor, &client_chunk_size, sizeof(client_chunk_size), MSG_WAITALL);

        std::cout << "[SERVER] Client requested download file by path: " << path << std::endl;

        // Попытка открыть файл для чтения
        if (!fh.open_read(path)) {
            std::cerr << "[SERVER] File not found:" << path << std::endl;
            send_error(client_descriptor, "File not found on server.");
            remove_client(client_descriptor);
            return;
        }

        uint64_t file_size = fh.get_file_size(path);

        // Отправляем пакет с размером файла
        send_packet(client_descriptor, CommandType::FILE_INFO, nullptr, file_size);

        std::vector<char> data_buffer;
        int64_t read_bytes = 0;

        // Читаем файл порциями размером client_chunk_size
        while ((read_bytes = fh.read_chunk(data_buffer, client_chunk_size)) > 0) {
            if (!send_packet(client_descriptor, CommandType::FILE_DATA, data_buffer.data(),
                             read_bytes)) {
                break;
            }
        }
        fh.close();
        std::cout << "[SERVER] File succesfuly sent" << std::endl;
    }
    // Загрузка файла на сервер
    else if (header.type == CommandType::UPLOAD_REQUEST) {
        // Читаем путь по которому сохранится файл
        std::string path = recv_string(client_descriptor, header.len);

        std::cout << "[SERVER] Client requested upload file by path:" << path << std::endl;

        // Создаем пустой файл
        if (!fh.open_write(path)) {
            std::cerr << "[SERVER] Cannot write file:" << path << std::endl;
            send_error(client_descriptor, "Cannot write file.");
            remove_client(client_descriptor);
            return;
        }

        // Получаем данные о размере получаемого файла
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

        // Цикл приёма данных
        while (cur_size < expected_size) {
            PacketHeader data_header;
            if (recv(client_descriptor, &data_header, sizeof(data_header), 0) <= 0) break;

            if (data_header.type != CommandType::FILE_DATA) {
                std::cerr << "[SERVER] Protocol error expected FILE_DATA" << std::endl;
                break;
            }

            data_buffer.resize(data_header.len);
            // MSG_WAITALL ждёт до тех пор пока не будет считан весь заданный размер в буффер
            ssize_t chunk_bytes =
                recv(client_descriptor, data_buffer.data(), data_header.len, MSG_WAITALL);
            if (chunk_bytes <= 0) break;

            // Записываем порцию в файл
            if (!fh.write_chunk(data_buffer, chunk_bytes)) {
                std::cerr << "[SERVER] Cannot write file on disk" << std::endl;
                break;
            }

            cur_size += chunk_bytes;
        }

        fh.close();

        // Если файл записался не полностью удаляем его
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

    // Перехватываем сигнал завершения процесса(Ctrl + C)
    signal(SIGINT, sigint_handler);

    // Создаём IPv4 TCP сокет
    server_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (server_descriptor == -1) {
        std::cerr << "Failed to create socket" << std::endl;
        return -1;
    }

    int opt = 1;

    // Разрешаем повторное использование порта без ожидания закрытия ОС (TIME_WAIT)
    setsockopt(server_descriptor, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Заполняем структуру сетевого адреса (слушаем на всех интерфейсах INADDR_ANY)
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(config.port);  // Перевод порта в сетевой порядок байт (Big-Endian)

    // Связываем дескриптор сокета с сетевым адресом и портом
    if (bind(server_descriptor, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed, port " << config.port << std::endl;
        close(server_descriptor);
        return 1;
    }

    // Переводим сокет в режим ожидания клиентов (длина очереди подключений = 10)
    if (listen(server_descriptor, 10) < 0) {
        std::cerr << "Listen failed" << std::endl;
        close(server_descriptor);
        return 1;
    }

    std::cout << "[SERVER] Server started on port " << config.port << std::endl;

    // Главный бесконечный цикл прослушивания сети
    while (true) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);

        // Блокирующий вызов - ждем нового клиента
        int client_descriptor =
            accept(server_descriptor, (struct sockaddr*)&client_addr, &addr_len);
        if (client_descriptor < 0) {
            break;
        }

        // Сохраняем дескриптор клиента под мьютексом
        {
            std::lock_guard<std::mutex> lock(mtx);
            clients.push_back(client_descriptor);
        }

        // Создаем отдельный поток для обслуживания подключившегося клиента
        std::thread t(handle_client, client_descriptor, config.chunk_size);
        // Отсоединяем поток
        t.detach();
    }

    if (server_descriptor != -1) {
        close(server_descriptor);
    }

    return 0;
}