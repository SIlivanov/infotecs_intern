/**
 * @file fileop.h
 * @brief Описание класса FileHandler для работы с файлами.
 */

#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

/**
 * @class FileHandler
 * @brief Класс реализующий операции над файлами
 */
class FileHandler {
   public:
    /**
     * @brief Конструктор по умолчанию
     */
    FileHandler();

    /**
     * @brief Деструктор автоматически закрывает открытые файлы
     */
    ~FileHandler();

    /**
     * @brief Открывает файл для чтения в бинарном режиме
     * @param path Путь к файлу
     * @return true если файл успешно открыт, false в противном случае
     */
    bool open_read(const std::string& path);

    /**
     * @brief Открывает файл для записи в бинарном режиме
     * @param path Путь к файлу
     * @return true если файл успешно подготовлен к записи, false в противном
     * случае
     */
    bool open_write(const std::string& path);

    /**
     * @brief Читает порцию данных из файла
     * @param buffer буффер куда записываются прочтённые данные
     * @param size размер данных в байтах, которые будут считаны в буффер
     * @return количество считанных байтов, -1 в случае ошибки
     */
    int64_t read_chunk(std::vector<char>& buffer, size_t size);

    /**
     * @brief Записывает порцию данных в файл
     * @param buffer буффер откуда берутся данные для записи в файл
     * @param size размер данных в байтах, которые запишутся в файл
     * @return true если операция прошла успешно, false в противном случае
     */
    bool write_chunk(const std::vector<char>& buffer, size_t size);

    /**
     * @brief Закрывает все файлы
     */
    void close();

    /**
     * @brief Считывает размер файла
     * @param path Путь по которому находится файл
     * @return Размер файла в байтах, 0 если его нет
     */
    uint64_t get_file_size(const std::string& path);

    /**
     * @brief Удаляет файл с диска
     * @param path Путь по которому находится файл
     */
    void remove_file(const std::string& path);

   private:
    std::ifstream input_file;   ///< Поток ввода (чтение)
    std::ofstream output_file;  ///< Поток вывода (запись)
};