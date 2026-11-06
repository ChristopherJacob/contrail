#include "Airlines.h"

#include <cctype>
#include <cstring>

namespace data {

// Each row: code, name, stripe colour, then the livery fill where the operator has one (left out,
// it is zero: regional, charter, cargo and military operators, whose stripe colour stands in).
// clang-format off
const Airline kAirlines[] = {
    {"AAL", "American", {120, 160, 210}, {0, 120, 210}},
    {"AAY", "Allegiant", {247, 148, 40}, {247, 148, 29}},
    {"ABX", "ABX Air", {90, 130, 210}},
    {"ACA", "Air Canada", {225, 60, 70}, {216, 34, 46}},
    {"AFR", "Air France", {80, 120, 230}, {0, 33, 87}},
    {"AMX", "Aeromexico", {80, 120, 215}, {11, 35, 67}},
    {"ANA", "ANA", {70, 120, 215}, {19, 68, 143}},
    {"ANZ", "Air New Zealand", {200, 210, 225}},
    {"ASA", "Alaska", {0, 168, 224}, {1, 66, 106}},
    {"AUA", "Austrian", {226, 40, 50}, {227, 24, 55}},
    {"AVA", "Avianca", {218, 60, 50}, {218, 41, 28}},
    {"BAW", "British Airways", {80, 125, 210}, {7, 90, 170}},
    {"CCA", "Air China", {227, 40, 40}, {227, 6, 19}},
    {"CES", "China Eastern", {90, 110, 210}, {24, 56, 140}},
    {"CKS", "Kalitta", {210, 60, 70}},
    {"CMP", "Copa", {70, 115, 210}, {0, 32, 91}},
    {"CPA", "Cathay Pacific", {30, 165, 155}, {0, 101, 100}},
    {"CSN", "China Southern", {0, 139, 203}, {0, 114, 188}},
    {"DAL", "Delta", {227, 50, 75}, {200, 16, 46}},
    {"DLH", "Lufthansa", {249, 186, 0}, {5, 22, 77}},
    {"EDV", "Endeavor", {200, 60, 80}, {200, 16, 46}},
    {"EIN", "Aer Lingus", {0, 166, 81}, {0, 98, 114}},
    {"EJA", "NetJets", {170, 180, 200}},
    {"ENY", "Envoy", {111, 151, 216}, {0, 120, 210}},
    {"ETD", "Etihad", {189, 139, 19}, {189, 139, 19}},
    {"EVA", "EVA Air", {34, 163, 111}, {0, 122, 84}},
    {"EZY", "easyJet", {255, 102, 0}, {255, 102, 0}},
    {"FDX", "FedEx", {255, 110, 20}, {77, 20, 140}},
    {"FFT", "Frontier", {0, 166, 90}, {0, 132, 61}},
    {"FIN", "Finnair", {80, 105, 215}, {11, 21, 96}},
    {"GJS", "GoJet", {111, 151, 216}, {31, 79, 216}},
    {"GTI", "Atlas Air", {111, 143, 216}},
    {"HAL", "Hawaiian", {155, 89, 208}, {65, 54, 145}},
    {"IBE", "Iberia", {215, 40, 50}, {215, 25, 45}},
    {"ICE", "Icelandair", {247, 198, 22}, {0, 48, 135}},
    {"JAL", "Japan Airlines", {230, 30, 40}, {204, 0, 0}},
    {"JBU", "JetBlue", {58, 123, 213}, {11, 58, 140}},
    {"JIA", "PSA", {111, 151, 216}, {0, 120, 210}},
    {"KAL", "Korean Air", {0, 160, 221}, {0, 160, 226}},
    {"KLM", "KLM", {0, 161, 222}, {0, 161, 222}},
    {"LAN", "LATAM", {232, 40, 85}, {27, 0, 136}},
    {"MXY", "Breeze", {28, 141, 224}, {15, 45, 82}},
    {"NAX", "Norwegian", {216, 40, 60}, {216, 25, 57}},
    {"NKS", "Spirit", {255, 236, 0}, {255, 236, 0}},
    {"QFA", "Qantas", {228, 30, 30}, {228, 0, 0}},
    {"QTR", "Qatar Airways", {176, 48, 90}, {92, 6, 50}},
    {"QXE", "Horizon", {0, 168, 224}, {1, 66, 106}},
    {"RCH", "US Air Force", {143, 155, 179}},
    {"RPA", "Republic", {138, 160, 200}},
    {"RYR", "Ryanair", {241, 201, 51}, {7, 53, 144}},
    {"SAS", "SAS", {80, 95, 215}, {12, 35, 107}},
    {"SCX", "Sun Country", {245, 128, 37}, {245, 128, 37}},
    {"SIA", "Singapore", {249, 159, 28}, {0, 38, 100}},
    {"SKW", "SkyWest", {108, 142, 191}},
    {"SWA", "Southwest", {249, 182, 18}, {242, 185, 15}},
    {"SWR", "Swiss", {227, 30, 40}, {226, 0, 26}},
    {"TAP", "TAP Air Portugal", {237, 40, 45}, {237, 28, 36}},
    {"THY", "Turkish", {210, 30, 35}, {199, 10, 12}},
    {"UAE", "Emirates", {215, 40, 45}, {215, 25, 33}},
    {"UAL", "United", {59, 123, 255}, {31, 79, 216}},
    {"UPS", "UPS", {255, 181, 0}, {100, 65, 23}},
    {"VIR", "Virgin Atlantic", {225, 30, 40}, {218, 2, 14}},
    {"VOI", "Volaris", {190, 70, 160}, {160, 30, 140}},
    {"WJA", "WestJet", {0, 163, 173}, {0, 122, 135}},
};
// clang-format on
const int kAirlineCount = sizeof(kAirlines) / sizeof(kAirlines[0]);

bool callsignAirline(const char* cs, char out[4]) {
    if (!cs || std::strlen(cs) < 4) return false;
    for (int i = 0; i < 3; ++i)
        if (!std::isalpha((unsigned char)cs[i])) return false;
    if (!std::isdigit((unsigned char)cs[3])) return false;
    // Upper case, so a lower-case callsign finds its airline too.
    for (int i = 0; i < 3; ++i) out[i] = char(std::toupper((unsigned char)cs[i]));
    out[3] = 0;
    return true;
}

const Airline* airlineByCode(const char* icao) {
    if (!icao || !icao[0]) return nullptr;
    for (int i = 0; i < kAirlineCount; ++i)
        if (!std::strncmp(kAirlines[i].icao, icao, 3) && !icao[3]) return &kAirlines[i];
    return nullptr;
}

const Airline* airlineForCallsign(const char* cs) {
    char code[4];
    return callsignAirline(cs, code) ? airlineByCode(code) : nullptr;
}

gfx::Rgb airlineFill(const Airline* al, gfx::Rgb neutral) {
    if (!al) return neutral;
    return al->fill.r || al->fill.g || al->fill.b ? al->fill : al->color;
}

}  // namespace data
