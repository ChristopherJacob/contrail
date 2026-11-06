#include "AircraftTypes.h"

#include <cctype>
#include <cstring>

namespace data {

// clang-format off
const AircraftType kAircraftTypes[] = {
    {"A19N", "Airbus A319neo"}, {"A20N", "Airbus A320neo"}, {"A21N", "Airbus A321neo"},
    {"A306", "Airbus A300-600"}, {"A310", "Airbus A310"}, {"A318", "Airbus A318"},
    {"A319", "Airbus A319"}, {"A320", "Airbus A320"}, {"A321", "Airbus A321"},
    {"A332", "Airbus A330-200"}, {"A333", "Airbus A330-300"}, {"A338", "Airbus A330-800"},
    {"A339", "Airbus A330-900"}, {"A343", "Airbus A340-300"}, {"A346", "Airbus A340-600"},
    {"A359", "Airbus A350-900"}, {"A35K", "Airbus A350-1000"}, {"A388", "Airbus A380"},
    {"A400", "Airbus A400M"}, {"AT45", "ATR 42-500"}, {"AT72", "ATR 72"},
    {"AT75", "ATR 72-500"}, {"AT76", "ATR 72-600"}, {"B350", "Beech King Air 350"},
    {"B37M", "Boeing 737 MAX 7"}, {"B38M", "Boeing 737 MAX 8"}, {"B39M", "Boeing 737 MAX 9"},
    {"B3XM", "Boeing 737 MAX 10"}, {"B712", "Boeing 717"}, {"B722", "Boeing 727"},
    {"B733", "Boeing 737-300"}, {"B734", "Boeing 737-400"}, {"B735", "Boeing 737-500"},
    {"B736", "Boeing 737-600"}, {"B737", "Boeing 737-700"}, {"B738", "Boeing 737-800"},
    {"B739", "Boeing 737-900"}, {"B744", "Boeing 747-400"}, {"B748", "Boeing 747-8"},
    {"B752", "Boeing 757-200"}, {"B753", "Boeing 757-300"}, {"B762", "Boeing 767-200"},
    {"B763", "Boeing 767-300"}, {"B764", "Boeing 767-400"}, {"B772", "Boeing 777-200"},
    {"B773", "Boeing 777-300"}, {"B77L", "Boeing 777-200LR"}, {"B77W", "Boeing 777-300ER"},
    {"B778", "Boeing 777-8"}, {"B779", "Boeing 777-9"}, {"B788", "Boeing 787-8"},
    {"B789", "Boeing 787-9"}, {"B78X", "Boeing 787-10"}, {"BCS1", "Airbus A220-100"},
    {"BCS3", "Airbus A220-300"}, {"BE20", "Beech King Air 200"}, {"BE35", "Beech Bonanza"},
    {"BE36", "Beech Bonanza 36"}, {"BE58", "Beech Baron"}, {"BE9L", "Beech King Air 90"},
    {"C130", "Lockheed C-130"}, {"C150", "Cessna 150"}, {"C152", "Cessna 152"},
    {"C17", "Boeing C-17"}, {"C172", "Cessna 172"}, {"C182", "Cessna 182"},
    {"C208", "Cessna Caravan"}, {"C25A", "Citation CJ2"}, {"C25B", "Citation CJ3"},
    {"C25C", "Citation CJ4"}, {"C510", "Citation Mustang"}, {"C525", "CitationJet"},
    {"C560", "Citation V"}, {"C56X", "Citation Excel"}, {"C680", "Citation Sovereign"},
    {"C68A", "Citation Latitude"}, {"C700", "Citation Longitude"}, {"C750", "Citation X"},
    {"CL30", "Challenger 300"}, {"CL35", "Challenger 350"}, {"CL60", "Challenger 600"},
    {"CRJ2", "Bombardier CRJ200"}, {"CRJ7", "Bombardier CRJ700"}, {"CRJ9", "Bombardier CRJ900"},
    {"CRJX", "Bombardier CRJ1000"}, {"DA40", "Diamond DA40"}, {"DA42", "Diamond DA42"},
    {"DH8A", "Dash 8-100"}, {"DH8B", "Dash 8-200"}, {"DH8C", "Dash 8-300"},
    {"DH8D", "Dash 8-400"}, {"E135", "Embraer ERJ-135"}, {"E145", "Embraer ERJ-145"},
    {"E170", "Embraer E170"}, {"E175", "Embraer E175"}, {"E190", "Embraer E190"},
    {"E195", "Embraer E195"}, {"E290", "Embraer E190-E2"}, {"E295", "Embraer E195-E2"},
    {"E35L", "Embraer Legacy 600"}, {"E545", "Embraer Legacy 450"}, {"E550", "Embraer Praetor 600"},
    {"E55P", "Embraer Phenom 300"}, {"E75L", "Embraer E175"}, {"E75S", "Embraer E175"},
    {"EC35", "Airbus H135"}, {"EC45", "Airbus H145"},
    {"F2TH", "Falcon 2000"}, {"F900", "Falcon 900"}, {"FA7X", "Falcon 7X"},
    {"FA8X", "Falcon 8X"}, {"GL5T", "Global 5000"}, {"GLEX", "Global Express"},
    {"GLF4", "Gulfstream IV"}, {"GLF5", "Gulfstream V"}, {"GLF6", "Gulfstream G650"},
    {"H25B", "Hawker 800"}, {"K35R", "Boeing KC-135"}, {"KC46", "Boeing KC-46"},
    {"LJ45", "Learjet 45"}, {"LJ60", "Learjet 60"}, {"MD11", "McDonnell Douglas MD-11"},
    {"MD82", "McDonnell Douglas MD-82"}, {"MD88", "McDonnell Douglas MD-88"}, {"P28A", "Piper Cherokee"},
    {"P28R", "Piper Arrow"}, {"PA46", "Piper Malibu"}, {"PC12", "Pilatus PC-12"},
    {"PC24", "Pilatus PC-24"}, {"R44", "Robinson R44"}, {"R66", "Robinson R66"},
    {"S76", "Sikorsky S-76"}, {"SF34", "Saab 340"}, {"SR20", "Cirrus SR20"},
    {"SR22", "Cirrus SR22"}, {"TBM7", "Daher TBM 700"}, {"TBM9", "Daher TBM 900"},
};
// clang-format on
const int kAircraftTypeCount = sizeof(kAircraftTypes) / sizeof(kAircraftTypes[0]);

const char* aircraftTypeName(const char* code) {
    if (!code || !code[0]) return nullptr;
    // A linear scan is fine: the screens look up a handful of types per scene.
    for (int i = 0; i < kAircraftTypeCount; ++i)
        if (!std::strcmp(kAircraftTypes[i].code, code)) return kAircraftTypes[i].name;
    return nullptr;
}

bool validTypeCode(const char* c) {
    if (!c) return false;
    const size_t n = std::strlen(c);
    if (n < 2 || n > 4 || !std::isupper((unsigned char)c[0])) return false;
    for (size_t i = 1; i < n; ++i)
        if (!std::isupper((unsigned char)c[i]) && !std::isdigit((unsigned char)c[i])) return false;
    static const char* const kNotAircraft[] = {"ZZZZ", "GRND", "SERV"};  // placeholder, ground vehicles
    for (const char* x : kNotAircraft)
        if (!std::strcmp(c, x)) return false;
    return true;
}

}  // namespace data
