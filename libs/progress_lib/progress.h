#pragma once

#include <cstdint>

extern "C" {
    void print_progress(uint64_t transferred, uint64_t total);
}
