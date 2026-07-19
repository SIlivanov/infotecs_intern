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

int client_descriptor = -1;  // Дескриптор сетевого сокета соединения с сервером
std::string local_file_path;  // Локальный путь к файлу (нужен для удаления в случае ошибки)
bool is_download = false;  // Флаг режима передачи: true - скачивание, false - загрузка
FileHandler fh;  //Объект для работы с файлами

// Объявляем тип указателя на функцию вывода прогресса из динамической библиотеки
typedef void (*print_progress_t)(uint64_t, uint64_t);
print_progress_t print_progress_fn = nullptr;  // Указатель на функцию вывода
void* lib_handle = nullptr;  // Дескриптор загруженной библиотеки libprogress.so

/**
 * @brief Обработчик сигнала Ctrl-C (SIGINT) на клиенте
 */
void sigint_handler(int signum) {
    std::cout << "\n[CLIENT] Transfer interrupted by user." << std::endl;

    fh.close();  // Принудительно закрываем файловый дескриптор на диске

    // Если мы скачивали файл и прервали загрузку — удаляем неполный файл
    if (is_download && !local_file_path.empty()) {
        std::cout << "[CLIENT] Deleting incomplete file: " << local_file_path << std::endl;
        fh.remove_file(local_file_path);
    }

    if (client_descriptor != -1) {
        close(client_descriptor);
    }

    // Выгружаем динамическую библиотеку из памяти
    if (lib_handle) {
        dlclose(lib_handle);
    }

    exit(signum);  // Выходим с кодом прерванного сигнала
}

int main(int argc, char* argv[]) {
    AppConfig config;

    // Разбираем аргументы запуска для клиента (is_client = true)
    if (!parse_arguments(argc, argv, config, true)) {
        return 1;
    }

    // Перехватываем сигнал завершения процесса(Ctrl + C)
    signal(SIGINT, sigint_handler);

    local_file_path = config.is_upload ? config.src_path : config.dst_path;
    is_download = !config.is_upload;

    // Позднее связывание динамической библиотеки
    lib_handle = dlopen("./libprogress.so", RTLD_LAZY);
    if (!lib_handle) {
        std::cerr << "[CLIENT] Failed to load progress library" << dlerror() << std::endl;
        return 1;
    }

    // Загружаем указатель на функцию print_progress
    print_progress_fn = (print_progress_t)dlsym(lib_handle, "print_progress");
    if (!print_progress_fn) {
        std::cerr << "[CLIENT] Failed to find symbol 'print_progress': " << dlerror() << std::endl;
        dlclose(lib_handle);
        return 1;
    }

    // Создаём IPv4 TCP сокет клиента
    client_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (client_descriptor == -1) {
        std::cerr << "failed to create socket" << std::endl;
        dlclose(lib_handle);
        return 1;
    }

    // Настраиваем адрес сервера
    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(config.port);

    // Преобразуем ip в бинарный формат
    if (inet_pton(AF_INET, config.ip.c_str(), &server_address.sin_addr) <= 0) {
        std::cerr << "[CLIENT] Invalid IP address: " << config.ip << std::endl;
        close(client_descriptor);
        dlclose(lib_handle);
        return 1;
    }

    std::cout << "[CLIENT] Connecting to server " << config.ip << ":" << config.port << std::endl;

    // Устанавливаем TCP-соединение с сервером (вызов блокирующий)
    if (connect(client_descriptor, (struct sockaddr*)&server_address, sizeof(server_address)) < 0) {
        std::cerr << "[CLIENT] Connection failed" << std::endl;
        close(client_descriptor);
        dlclose(lib_handle);
        return 1;
    }

    std::cout << "[CLIENT] Connected successfully." << std::endl;

    // Загрузка файла на сервер
    if (!config.is_upload) {
        std::cout << "[CLIENT] Mode: Downloading from server." << std::endl;

        // Отправляем запрос DOWNLOAD_REQUEST и путь к удаленному файлу на сервер
        send_packet(client_descriptor, CommandType::DOWNLOAD_REQUEST, config.src_path.data(),
                    config.src_path.size());

        // Отправляем размер порций, который выбрал клиент
        uint64_t client_chunk = config.chunk_size;
        send(client_descriptor, &client_chunk, sizeof(client_chunk), 0);

        // Ждем ответ от сервера (должен прийти FILE_INFO или ERROR_MSG)
        PacketHeader response_header;
        recv(client_descriptor, &response_header, sizeof(response_header), 0);

        // Если сервер не нашел файл — выводим его сообщение об ошибке
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

        // Открываем файл для записи
        if (!fh.open_write(config.dst_path)) {
            std::cerr << "[CLIENT] Failed to create local file: " << config.dst_path << std::endl;
            close(client_descriptor);
            dlclose(lib_handle);
            return 1;
        }

        std::vector<char> buffer;

        // Цикл приема данных файла порциями
        while (received_bytes < total_size) {
            PacketHeader data_header;
            if (recv(client_descriptor, &data_header, sizeof(data_header), 0) <= 0) break;

            if (data_header.type != CommandType::FILE_DATA) {
                std::cerr << "[CLIENT] Protocol error. Expected FILE_DATA." << std::endl;
                break;
            }

            buffer.resize(data_header.len);
            // MSG_WAITALL значит что мы будем ждать до тех пор пока не примем весь пакет указанного
            // размера
            ssize_t chunk_bytes =
                recv(client_descriptor, buffer.data(), data_header.len, MSG_WAITALL);
            if (chunk_bytes <= 0) break;

            // Записываем полученный чанк на локальный диск
            if (!fh.write_chunk(buffer, chunk_bytes)) {
                std::cerr << "[CLIENT] Local write error!" << std::endl;
                break;
            }

            received_bytes += chunk_bytes;

            // Вызываем динамически загруженную функцию отрисовки прогресса
            print_progress_fn(received_bytes, total_size);
        }

        fh.close();

        // Если файл записался не полностью удаляем его
        if (received_bytes < total_size) {
            std::cerr << "[CLIENT] Download failed. Connection closed prematurely." << std::endl;
            fh.remove_file(config.dst_path);
        } else {
            std::cout << "[CLIENT] File successfully downloaded!" << std::endl;
        }
    }
    // Загрузка на сервер
    else {
        std::cout << "[CLIENT] Mode: Uploading to server." << std::endl;

        // Открываем файл на чтение
        if (!fh.open_read(config.src_path)) {
            std::cerr << "[CLIENT] Failed to open local file: " << config.src_path << std::endl;
            close(client_descriptor);
            dlclose(lib_handle);
            return 1;
        }

        uint64_t total_size = fh.get_file_size(config.src_path);

        // Отправляем запрос за загрузку и путь для загрузки
        send_packet(client_descriptor, CommandType::UPLOAD_REQUEST, config.dst_path.data(),
                    config.dst_path.size());

        // Отправляем размер файла
        send_packet(client_descriptor, CommandType::FILE_INFO, nullptr, total_size);

        std::vector<char> buffer;
        int64_t read_bytes = 0;
        uint64_t sent_bytes = 0;

        // Цикл отправки порций
        while ((read_bytes = fh.read_chunk(buffer, config.chunk_size)) > 0) {
            if (!send_packet(client_descriptor, CommandType::FILE_DATA, buffer.data(),
                             read_bytes)) {
                break;
            }
            sent_bytes += read_bytes;

            // Показываем прогресс
            print_progress_fn(sent_bytes, total_size);
        }

        fh.close();
        if (sent_bytes < total_size) {
            std::cerr << "[CLIENT] Upload failed/interrupted." << std::endl;
        } else {
            std::cout << "[CLIENT] File successfully uploaded!" << std::endl;
        }
    }

    // Очищаем системные ресурсы перед завершением работы
    close(client_descriptor);
    dlclose(lib_handle);  // Выгружаем динамическую библиотеку прогресса из памяти
    return 0;
}
