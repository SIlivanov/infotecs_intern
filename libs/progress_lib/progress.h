/**
 * @file progress.h
 * @brief Описание интерфейса динамической библиотеки вывода прогресс-бара
 */

#pragma once

#include <cstdint>

extern "C" {
/**
 * @brief Выводит индикатор прогресса передачи файла в консоль
 * @param transferred Количество уже переданных или принятых байт
 * @param total Общий размер передаваемого файла в байтах
 */
void print_progress(uint64_t transferred, uint64_t total);
}
