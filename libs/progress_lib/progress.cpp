#include "progress.h"

#include <iomanip>
#include <iostream>

extern "C" {
void print_progress(uint64_t transferred, uint64_t total) {
    if (total == 0) return;
    double percentage = (static_cast<double>(transferred) / total) * 100;

    // Вывод прогресса в процентах (всё в одной строке без захламления консоли)
    std::cout << "\rProgress: " << std::fixed << std::setprecision(2) << percentage << "% " << " ("
              << transferred << "/" << total << "bytes)" << std::flush;

    if (transferred >= total) {
        std::cout << std::endl;
    }
}
}