#include "Esp32Storage.h"

#include <cstdio>

#include "board.h"

namespace hal {

bool Esp32Storage::begin() {
    if (!sd_.begin(board::kSdSck, board::kSdMiso, board::kSdMosi, board::kSdCs)) return false;
    return vol_.mount(&sd_);
}

void Esp32Storage::describe(char* out, int n) const {
    std::snprintf(out, n, "FAT%d, %s, %u MHz, CRC %s", vol_.fatType(), sd_.highCapacity() ? "SDHC" : "SDSC",
                  unsigned(sd_.hz() / 1000000), sd_.crcOn() ? "on" : "off");
}

}  // namespace hal
