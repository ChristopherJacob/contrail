#include "CoreSettings.h"

namespace svc {
namespace {
const char* const kProviders[] = {"community", "local", "opensky"};
const char* const kUnitOpts[] = {"aviation", "imperial", "metric"};
const char* const kBrightOpts[] = {"auto", "manual"};
const char* const kDriverOpts[] = {"auto", "ili9341", "st7789"};
const char* const kInvertOpts[] = {"auto", "on", "off"};
const char* const kRotateOpts[] = {"0", "180"};
const char* const kScreenModes[] = {"director", "carousel", "stay"};
const char* const kThemes[] = {"cinematic", "editorial"};
}  // namespace

// clang-format off
const SettingDef kCoreSettings[] = {
    // key                 type           group       label                              min   max     def  defStr  options       n  secret
    {key::kLat,         SType::Float,  "location", "Latitude",                         -90,  90,     0,   nullptr, nullptr,      0, false},
    {key::kLon,         SType::Float,  "location", "Longitude",                        -180, 180,    0,   nullptr, nullptr,      0, false},
    {key::kLocSet,      SType::Bool,   "location", "Location set",                     0,    1,      0,   nullptr, nullptr,      0, false},
    {key::kRadius,      SType::Int,    "location", "Radius (nm)",                      5,    100,    25,  nullptr, nullptr,      0, false},
    {key::kProvider,    SType::Enum,   "data",     "Flight data source",               0,    0,      0,   nullptr, kProviders,   3, false},
    {key::kFallback,    SType::Bool,   "data",     "Fall back to other sources",       0,    1,      1,   nullptr, nullptr,      0, false},
    {key::kLocalUrl,    SType::String, "data",     "Local receiver aircraft.json URL", 0,    0,      0,   "",      nullptr,      0, false},
    {key::kOskyId,      SType::String, "data",     "OpenSky client ID",                0,    0,      0,   "",      nullptr,      0, false},
    {key::kOskySecret,  SType::String, "data",     "OpenSky client secret",            0,    0,      0,   "",      nullptr,      0, true},
    {key::kAeroKey,     SType::String, "data",     "FlightAware AeroAPI key",          0,    0,      0,   "",      nullptr,      0, true},
    {key::kAeroCap,     SType::Int,    "data",     "AeroAPI lookups per day",          0,    500,    20,  nullptr, nullptr,      0, false},
    {key::kPollComm,    SType::Int,    "data",     "Community poll interval (s)",      2,    60,     10,   nullptr, nullptr,      0, false},
    {key::kPollLocal,   SType::Int,    "data",     "Local receiver poll interval (s)", 1,    30,     1,   nullptr, nullptr,      0, false},
    {key::kPollOsky,    SType::Int,    "data",     "OpenSky poll interval (s)",        10,   300,    30,  nullptr, nullptr,      0, false},
    {key::kUnits,       SType::Enum,   "display",  "Units",                            0,    0,      0,   nullptr, kUnitOpts,    3, false},
    {key::kBrightMode,  SType::Enum,   "display",  "Brightness",                       0,    0,      0,   nullptr, kBrightOpts,  2, false},
    {key::kBrightness,  SType::Int,    "display",  "Manual brightness",                5,    255,    200, nullptr, nullptr,      0, false},
    {key::kScreenMode,  SType::Enum,   "display",  "Screen mode",                      0,    0,      0,   nullptr, kScreenModes, 3, false},
    {key::kCarouselS,   SType::Int,    "display",  "Carousel interval (s)",            10,   300,    30,  nullptr, nullptr,      0, false},
    {key::kTheme,       SType::Enum,   "display",  "Theme",                            0,    0,      0,   nullptr, kThemes,      2, false},
    {key::kTz,          SType::String, "location", "Time zone (POSIX)",                0,    0,      0,   "",      nullptr,      0, false},
    {key::kDispDriver,  SType::Enum,   "hardware", "Display controller",               0,    0,      0,   nullptr, kDriverOpts,  3, false},
    {key::kDispInvert,  SType::Enum,   "hardware", "Invert colours",                   0,    0,      0,   nullptr, kInvertOpts,  3, false},
    {key::kDispBgr,     SType::Bool,   "hardware", "Swap red/blue",                    0,    1,      0,   nullptr, nullptr,      0, false},
    {key::kDispRotate,  SType::Enum,   "hardware", "Rotation",                         0,    0,      0,   nullptr, kRotateOpts,  2, false},
    {key::kAlertEmerg,  SType::Bool,   "alerts",   "Emergency alerts",                 0,    1,      1,   nullptr, nullptr,      0, false},
    {key::kAlertWatch,  SType::Bool,   "alerts",   "Watchlist alerts",                 0,    1,      1,   nullptr, nullptr,      0, false},
    {key::kAlertMil,    SType::Bool,   "alerts",   "Military alerts",                  0,    1,      1,   nullptr, nullptr,      0, false},
    {key::kAlertLow,    SType::Bool,   "alerts",   "Low pass alerts",                  0,    1,      1,   nullptr, nullptr,      0, false},
    {key::kWatchlist,   SType::String, "alerts",   "Watchlist",                        0,    0,      0,   "",      nullptr,      0, false},
    {key::kAlertLowFt,  SType::Int,    "alerts",   "Low pass below (ft)",              500,  10000,  3000, nullptr, nullptr,     0, false},
    {key::kAlertLowNm,  SType::Float,  "alerts",   "Low pass within (nm)",             0.5,  10,     2,   nullptr, nullptr,      0, false},
    {key::kQuietFrom,   SType::String, "alerts",   "Quiet hours from (HH:MM)",         0,    0,      0,   "22:00", nullptr,      0, false},
    {key::kQuietTo,     SType::String, "alerts",   "Quiet hours to (HH:MM)",           0,    0,      0,   "07:00", nullptr,      0, false},
    {key::kAlertSound,  SType::Bool,   "alerts",   "Chime on alerts",                  0,    1,      1,   nullptr, nullptr,      0, false},
    {key::kAlertVolume, SType::Int,    "alerts",   "Chime volume (%)",                 0,    100,    60,  nullptr, nullptr,      0, false},
};
// clang-format on
const int kCoreSettingsCount = sizeof(kCoreSettings) / sizeof(kCoreSettings[0]);

}  // namespace svc
