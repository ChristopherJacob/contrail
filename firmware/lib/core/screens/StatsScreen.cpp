#include "StatsScreen.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "data/AircraftTypes.h"
#include "data/Airlines.h"
#include "gfx/Font.h"
#include "gfx/Raster.h"
#include "gfx/fonts/Fonts.h"
#include "ui/Format.h"
#include "ui/Widgets.h"

namespace scr {
using namespace gfx;

namespace {
const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

void formatDate(char* out, size_t n, int ymd) {  // 20260912 → "12 Sep 2026"
    int m = ymd / 100 % 100;
    std::snprintf(out, n, "%d %s %d", ymd % 100, kMonths[(m + 11) % 12], ymd / 10000);
}

// When a first sighting was: its time if it was today, else its day ("12 Sep"), so an old
// sighting never reads as this afternoon's.
void formatWhen(char* out, size_t n, int64_t epoch, int todayYmd) {
    std::time_t t = std::time_t(epoch);
    std::tm tm{};
    localtime_r(&t, &tm);
    const int ymd = (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
    if (todayYmd && ymd == todayYmd) std::snprintf(out, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
    else std::snprintf(out, n, "%d %s", tm.tm_mday, kMonths[tm.tm_mon % 12]);
}

// Layout. Every region draws only inside its own box, so a region redrawn alone never cuts
// into a neighbour's text.
constexpr int kChartBase = 136, kChartMaxH = 62;  // bars stand on y = 136, at most 62 px tall
constexpr int kListTitleY = 166, kListRowY = 182, kRowStep = 14;
constexpr int kTypesTitleY = 78, kTypesRowY = 93, kTypesStep = 13;
// (kRight, kBarX, kBarW, kGap and the card's edges are StatsView's: the tests check against them.)
constexpr int kNameW = 88, kMinBarW = 24;
constexpr int kTypeMaxW = 116;  // the newest type's name and sub-line, at most
const StatsSummary kNoStats{};
}  // namespace

int StatsView::rows(const StatsEntry* e, int n, bool airlines, Row* out) const {
    const uint32_t lead = n ? e[0].count : 1;
    for (int i = 0; i < n; ++i) {
        Row& r = out[i];
        const char* name = e[i].code;
        r.colour = t_.statsBar;
        if (airlines) {
            const data::Airline* al = data::airlineByCode(e[i].code);
            if (al) name = al->name;
            r.colour = al ? al->color : t_.brandNeutral;
        } else if (const char* tn = data::aircraftTypeName(e[i].code)) {
            name = tn;
        }
        ui::ellipsize(r.name, sizeof r.name, kFontSmall, name, kNameW);
        ui::formatThousands(r.count, sizeof r.count, int32_t(e[i].count));
        r.frac = uint16_t(lead ? std::min<uint64_t>(1000, uint64_t(e[i].count) * 1000 / lead) : 0);
        // The track stops kGap short of the count, however many digits it has.
        r.barW = uint8_t(std::max(kMinBarW, std::min(kBarW, kRight - textWidth(kFontSmall, r.count) - kGap - kBarX)));
    }
    return n;
}

void StatsView::compose(Text& t) const {
    const StatsSummary& s = scene_ ? scene_->stats : kNoStats;
    std::memset(&t, 0, sizeof t);  // padding too: the regions are hashed byte for byte
    if (scene_ && scene_->timeValid) ui::formatClock(t.clock, sizeof t.clock, scene_->epoch);
    // Without the clock the saved day's counts may be yesterday's: show the logbook as a whole.
    const bool allTime = allTime_ || !s.valid;
    // Short: it shares the top row with the offline badge (x 128..172), which must not cover it.
    if (!s.valid) std::snprintf(t.note, sizeof t.note, "No clock");
    if (!allTime) {
        std::snprintf(t.title, sizeof t.title, "TODAY");
        ui::formatThousands(t.count, sizeof t.count, int32_t(s.flightsToday));
        std::snprintf(t.unit, sizeof t.unit, "%s", s.flightsToday == 1 ? "flight" : "flights");
        int best = -1;
        for (int h = 0; h < 24; ++h)
            if (s.hourly[h] && (best < 0 || s.hourly[h] > s.hourly[best])) best = h;
        if (best >= 0) {
            std::snprintf(t.rightLabel, sizeof t.rightLabel, "Busiest");
            std::snprintf(t.rightValue, sizeof t.rightValue, "%02d:00 · %u", best, unsigned(s.hourly[best]));
        }
        t.chart = true;
        std::memcpy(t.bars, s.hourly, sizeof t.bars);
        t.hour = s.hour, t.barMax = 1;
        for (uint16_t b : t.bars) t.barMax = std::max(t.barMax, b);
        std::snprintf(t.listTitle, sizeof t.listTitle, "TOP AIRLINES TODAY");
        t.listRows = rows(s.airlinesToday, s.nAirlinesToday, true, t.listRow);
    } else {
        std::snprintf(t.title, sizeof t.title, "ALL-TIME");
        ui::formatThousands(t.count, sizeof t.count, int32_t(s.flightsAll));
        std::snprintf(t.unit, sizeof t.unit, "%s", s.flightsAll == 1 ? "flight" : "flights");
        if (s.startYmd) {
            std::snprintf(t.rightLabel, sizeof t.rightLabel, "Since");
            formatDate(t.rightValue, sizeof t.rightValue, s.startYmd);
        }
        std::snprintf(t.chartTitle, sizeof t.chartTitle, "TOP TYPES");
        t.chartRows = rows(s.typesAll, s.nTypesAll, false, t.chartRow);
        if (!t.chartRows) std::snprintf(t.message, sizeof t.message, "No types counted yet");
        std::snprintf(t.listTitle, sizeof t.listTitle, "TOP AIRLINES ALL-TIME");
        t.listRows = rows(s.airlinesAll, s.nAirlinesAll, true, t.listRow);
    }
    // A count too wide for the right-hand value ("1,234,567" next to "12 Sep 2026") keeps the line.
    if (t.rightValue[0] &&
        14 + textWidth(*t_.fontDisplay, t.count) + kGap > kRight - textWidth(kFontBody, t.rightValue))
        t.rightLabel[0] = t.rightValue[0] = 0;
    if (!t.listRows) std::snprintf(t.listEmpty, sizeof t.listEmpty, "No flights counted yet");

    std::snprintf(t.cardCount, sizeof t.cardCount, "%d", s.typesEver);
    t.cardNewUp = s.valid && s.typesNewToday > 0;
    if (t.cardNewUp) std::snprintf(t.cardNew, sizeof t.cardNew, "+%d today", s.typesNewToday);
    else if (s.valid) std::snprintf(t.cardNew, sizeof t.cardNew, "none new today");
    t.cardNewest = s.newestType[0] != 0;
    if (t.cardNewest) {
        char when[12];
        formatWhen(when, sizeof when, s.newestEpoch, s.valid ? s.ymd : 0);
        std::snprintf(t.cardWhen, sizeof t.cardWhen, "NEW · %s", when);
        const char* name = data::aircraftTypeName(s.newestType);
        // The name's line rises beside the count; the sub-line shares the count's baseline with
        // "+N today". Each stops kGap short of what is to its left.
        const int countEnd = kCardLeft + textWidth(*t_.fontTitle, t.cardCount);
        const int leftEnd = countEnd + (t.cardNew[0] ? 5 + textWidth(kFontSmall, t.cardNew) : 0);
        const int typeW = std::min(kTypeMaxW, StatsView::kCardRight - countEnd - kGap);
        const int subW = std::min(kTypeMaxW, StatsView::kCardRight - leftEnd - kGap);
        ui::ellipsize(t.cardType, sizeof t.cardType, kFontBody, name ? name : s.newestType, typeW);
        // Callsign and raw code under the name, as on the mockup; the code alone is the name already.
        // Where both do not fit, the callsign alone (the name above already says what type it is).
        char sub[24];
        if (s.newestCallsign[0] && name) std::snprintf(sub, sizeof sub, "%s · %s", s.newestCallsign, s.newestType);
        else std::snprintf(sub, sizeof sub, "%s", s.newestCallsign);
        if (name && textWidth(kFontSmall, sub) > subW) std::snprintf(sub, sizeof sub, "%s", s.newestCallsign);
        ui::ellipsize(t.cardSub, sizeof t.cardSub, kFontSmall, sub, subW);
    } else {
        std::snprintf(t.cardWhen, sizeof t.cardWhen, "NEWEST TYPE");
        std::snprintf(t.cardType, sizeof t.cardType, "None yet");
    }
}

void StatsView::update(float, DamageList& damage) {
    if (!text_) return;
    text_.check();
    if (dirty_) {
        dirty_ = false;
        compose(text_[0]);
        const Text& t = text_[0];
        auto rowsSig = [](const Row* r, int n, uint32_t h) {
            for (int i = 0; i < n; ++i) h = ui::hashBytes(&r[i], sizeof r[i], h);
            return h;
        };
        const uint32_t chartSeed = uint32_t(t.chart) + uint32_t(t.hour) * 7u + uint32_t(t.barMax) * 131u +
                                   uint32_t(t.chartRows) * 100003u;
        const uint32_t sig[4] = {
            ui::hashStr(t.title, ui::hashStr(t.clock, ui::hashStr(t.note, ui::hashStr(t.count,
                        ui::hashStr(t.unit, ui::hashStr(t.rightLabel, ui::hashStr(t.rightValue))))))),
            ui::hashBytes(t.bars, sizeof t.bars,
                          ui::hashStr(t.chartTitle, ui::hashStr(t.message, rowsSig(t.chartRow, t.chartRows, chartSeed)))),
            ui::hashStr(t.listTitle, ui::hashStr(t.listEmpty, rowsSig(t.listRow, t.listRows, uint32_t(t.listRows)))),
            ui::hashStr(t.cardCount, ui::hashStr(t.cardNew, ui::hashStr(t.cardWhen, ui::hashStr(t.cardType,
                        ui::hashStr(t.cardSub, uint32_t(t.cardNewUp) + 2u * t.cardNewest))))),
        };
        const Rect boxes[4] = {kHeader, kChart, kList, kCard};
        for (int i = 0; i < 4; ++i) {
            if (!full_ && sig[i] != sig_[i]) damage.add(boxes[i]);
            sig_[i] = sig[i];
        }
    }
    if (full_) damage.add({0, 0, 240, 320});
    full_ = false;
}

void StatsView::drawRows(Canvas& c, const Row* r, int n, int y0, int step, bool airlines) const {
    for (int i = 0; i < n; ++i) {
        const int y = y0 + i * step;
        int x = 14;
        if (airlines) ui::stripe(c, 14, y - 9, 11, r[i].colour), x = 23;
        drawText(c, kFontSmall, x, y, r[i].name, t_.text);
        const Rgb bar = airlines ? r[i].colour : t_.statsBar;  // brand colour (neutral if unknown), as mockup 6A
        fillRoundRect(c, {kBarX, y - 6, r[i].barW, 4}, t_.flat ? 0.f : 2.f, t_.statsTrack);
        const int w = r[i].barW * r[i].frac / 1000;
        if (w > 0) fillRoundRect(c, {kBarX, y - 6, std::max(w, 2), 4}, t_.flat ? 0.f : 2.f, bar);
        drawText(c, kFontSmall, kRight, y, r[i].count, t_.textSoft, Align::Right);
    }
}

void StatsView::draw(Canvas& c) {
    if (!text_) return;
    text_.check();
    const Text& t = text_[0];
    Rgb bg[] = {t_.boardBgTop, t_.boardBgBottom};
    float pos[] = {0.f, 1.f};
    ui::vFill(c, {0, 0, 240, 320}, bg, pos, 2, t_);
    if (c.clip().intersects(kHeader)) {
        drawText(c, kFontSmall, 14, 18, t.title, t_.textSoft);
        if (t.clock[0]) drawText(c, kFontBody, 226, 18, t.clock, t_.textSoft, Align::Right);
        else if (t.note[0]) drawText(c, kFontSmall, 226, 18, t.note, t_.textDim, Align::Right);
        const int x = drawText(c, *t_.fontDisplay, 14, 54, t.count, t_.text);
        // The unit only where it clears the right-hand value ("18,420" next to "12 Sep 2026" does not).
        const int room = 226 - (t.rightValue[0] ? textWidth(kFontBody, t.rightValue) + 8 : 0);
        if (x + 5 + textWidth(kFontBody, t.unit) <= room) drawText(c, kFontBody, x + 5, 54, t.unit, t_.textSoft);
        if (t.rightLabel[0]) {
            drawText(c, kFontSmall, 226, 38, t.rightLabel, t_.textDim, Align::Right);
            drawText(c, kFontBody, 226, 54, t.rightValue, t_.accent, Align::Right);
        }
    }
    if (c.clip().intersects(kChart)) {
        if (t.chart) {
            for (int h = 0; h < 24; ++h) {
                const int x = 12 + h * 9;
                const int bh = t.bars[h] ? std::max(2, t.bars[h] * kChartMaxH / t.barMax) : 0;
                const Rgb col = h < t.hour ? t_.statsPast : h == t.hour ? t_.statsNow : t_.statsFuture;
                // The baseline under every hour, so empty and future hours still read as a timeline.
                c.fill({x, kChartBase, 7, 2}, rgb565(h > t.hour ? t_.statsFuture : t_.statsTrack));
                if (bh) fillRoundRect(c, {x, kChartBase - bh, 7, bh}, t_.flat ? 0.f : 1.5f, col);
            }
            char l[4];
            for (int h = 0; h <= 24; h += 6) {
                std::snprintf(l, sizeof l, "%02d", h);
                if (h == 24) drawText(c, kFontSmall, 226, 149, l, t_.textDim, Align::Right);
                else drawText(c, kFontSmall, 12 + h * 9, 149, l, t_.textDim);
            }
        } else if (t.chartRows) {
            drawText(c, kFontSmall, 14, kTypesTitleY, t.chartTitle, t_.textSoft);
            drawRows(c, t.chartRow, t.chartRows, kTypesRowY, kTypesStep, false);
        } else {
            drawText(c, kFontBody, 120, 112, t.message, t_.textDim, Align::Center);
        }
    }
    if (c.clip().intersects(kList)) {
        drawText(c, kFontSmall, 14, kListTitleY, t.listTitle, t_.textSoft);
        if (t.listRows) drawRows(c, t.listRow, t.listRows, kListRowY, kRowStep, true);
        else drawText(c, kFontBody, 120, 212, t.listEmpty, t_.textDim, Align::Center);
    }
    if (c.clip().intersects(kCard)) {
        const Rect& p = StatsView::kCardPanel;
        ui::panel(c, p, 9, t_.statsCard, t_.statsCardEdge, t_);
        drawText(c, kFontSmall, kCardLeft, p.y + 17, "TYPES SEEN", t_.textDim);
        const int x = drawText(c, *t_.fontTitle, kCardLeft, p.y + 44, t.cardCount, t_.text);
        if (t.cardNew[0]) drawText(c, kFontSmall, x + 5, p.y + 44, t.cardNew, t.cardNewUp ? t_.climb : t_.textDim);
        const int r = kCardRight;
        drawText(c, kFontSmall, r, p.y + 17, t.cardWhen, t.cardNewest ? t_.accent : t_.textDim, Align::Right);
        drawText(c, kFontBody, r, p.y + 33, t.cardType, t.cardNewest ? t_.text : t_.textDim, Align::Right);
        if (t.cardSub[0]) drawText(c, kFontSmall, r, p.y + 47, t.cardSub, t_.textSoft, Align::Right);
    }
}

}  // namespace scr
