#pragma once

#include <fstream>
#include <cstdint>
#include <string>
#include <vector>
#include <iostream>

namespace toy2d {
    std::vector<uint32_t> ReadSpvFile(const std::string& filename);
}
