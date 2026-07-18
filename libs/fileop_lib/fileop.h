#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

class FileHandler {
 public:
  FileHandler();
  ~FileHandler();

  bool open_read(const std::string& path);
  bool open_write(const std::string& path);

  int64_t read_chunk(std::vector<char>& buffer, size_t size);
  bool write_chunk(const std::vector<char>& buffer, size_t size);

  void close();

  uint64_t get_file_size(const std::string& path);

  void remove_file(const std::string& path);

 private:
  std::ifstream input_file;
  std::ofstream output_file;
};