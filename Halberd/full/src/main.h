#pragma once
#include <mutex>
#include <string>

namespace halberd {
    extern std::string lastResults;
    extern std::mutex lastResultsMutex;
}