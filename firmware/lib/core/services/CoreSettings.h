#pragma once
#include "Settings.h"

namespace svc {

// Settings owned by the core platform (location, data sources, display, units).
// Later sub-projects append their own tables.
extern const SettingDef kCoreSettings[];
extern const int kCoreSettingsCount;

namespace key {
constexpr const char* kLat = "lat";
constexpr const char* kLon = "lon";
constexpr const char* kLocSet = "loc_set";
constexpr const char* kRadius = "radius_nm";
constexpr const char* kProvider = "provider";
constexpr const char* kFallback = "fallback";
constexpr const char* kLocalUrl = "local_url";
constexpr const char* kOskyId = "osky_id";
constexpr const char* kOskySecret = "osky_secret";
constexpr const char* kAeroKey = "aero_key";
constexpr const char* kAeroCap = "aero_cap";
constexpr const char* kPollComm = "poll_comm_s";
constexpr const char* kPollLocal = "poll_local_s";
constexpr const char* kPollOsky = "poll_osky_s";
constexpr const char* kUnits = "units";
constexpr const char* kBrightMode = "bright_mode";
constexpr const char* kBrightness = "brightness";
constexpr const char* kTz = "tz";
constexpr const char* kDispDriver = "disp_driver";
constexpr const char* kDispInvert = "disp_invert";
constexpr const char* kDispBgr = "disp_bgr";
constexpr const char* kDispRotate = "disp_rotate";
constexpr const char* kScreenMode = "screen_mode";
constexpr const char* kCarouselS = "carousel_s";
constexpr const char* kTheme = "theme";
constexpr const char* kAlertEmerg = "alert_emerg";
constexpr const char* kAlertWatch = "alert_watch";
constexpr const char* kAlertMil = "alert_mil";
constexpr const char* kAlertLow = "alert_low";
constexpr const char* kWatchlist = "watchlist";
constexpr const char* kAlertLowFt = "alert_low_ft";
constexpr const char* kAlertLowNm = "alert_low_nm";
constexpr const char* kQuietFrom = "quiet_from";
constexpr const char* kQuietTo = "quiet_to";
constexpr const char* kAlertSound = "alert_sound";   // the chime when an alert starts (boards with a speaker)
constexpr const char* kAlertVolume = "alert_volume";
}  // namespace key

}  // namespace svc
