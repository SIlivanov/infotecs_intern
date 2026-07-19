#include "fileop.h"

#include <filesystem>

FileHandler::FileHandler() {}

FileHandler::~FileHandler() { close(); }

bool FileHandler::open_read(const std::string& path) {
    input_file.open(path, std::ios::binary);

    return input_file.is_open();
}

bool FileHandler::open_write(const std::string& path) {
    output_file.open(path, std::ios::binary | std::ios::trunc);

    return output_file.is_open();
}

int64_t FileHandler::read_chunk(std::vector<char>& buff, size_t size) {
    if (!input_file.is_open()) return -1;
    buff.resize(size);
    input_file.read(buff.data(), size);

    return input_file.gcount();
}

bool FileHandler::write_chunk(const std::vector<char>& buff, size_t size) {
    if (!output_file.is_open()) return false;
    output_file.write(buff.data(), size);

    return output_file.good();
}

void FileHandler::close() {
    if (input_file.is_open()) input_file.close();
    if (output_file.is_open()) output_file.close();
}

uint64_t FileHandler::get_file_size(const std::string& path) {
    std::error_code ec;
    uint64_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        return 0;
    }

    return size;
}

void FileHandler::remove_file(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}