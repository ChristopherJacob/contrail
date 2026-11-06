#pragma once
#include <fstream>
#include <sstream>
#include <string>

inline std::string loadFixture(const char* name) {
    std::ifstream in(std::string(FIXTURE_DIR) + "/" + name, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
