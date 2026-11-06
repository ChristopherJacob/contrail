#pragma once
// Stats (mockup 6A, "spotter's logbook"): today's count and busiest hour, the 24-hour chart, the top
// airlines and the types card. A vertical swipe switches to all-time: the top types and the all-time
// airline totals take the places of the chart and today's airlines. Until the clock is set the
// logbook cannot tell whether its saved day is today, so the screen shows the all-time view and says
// the clock is not set (spec §6: counts, no hour chart). The text lives in the screen arena
// while the screen is up; each region is redrawn when its text changes.
#include "Screen.h"
#include "ScreenArena.h"
#include "Theme.h"

namespace scr {

class StatsView : public gfx::Layer {
   public:
    static constexpr gfx::Rect kHeader{0, 0, 240, 62}, kChart{0, 62, 240, 90}, kList{0, 152, 240, 100},
        kCard{0, 252, 240, 60};
    static constexpr int kRows = StatsSummary::kTop;
    // Layout the tests check widths against: text ends at kRight; rows' bars start at kBarX and are
    // at most kBarW long; the card's text runs from kCardLeft to kCardRight; kGap is the least
    // space kept between two runs of text (or a bar and its count) on one line.
    static constexpr int kRight = 226, kBarX = 112, kBarW = 76, kGap = 6;
    static constexpr gfx::Rect kCardPanel{8, 254, 224, 56};
    static constexpr int kCardLeft = 18, kCardRight = 222;
    explicit StatsView(const Theme& t) : t_(t) {}
    struct Row {
        char name[24], count[12];
        gfx::Rgb colour;  // airline stripe and bar
        uint16_t frac;    // the bar's length, 0..1000 of the leader's
        uint8_t barW;     // the track's length: kBarW, shortened to clear a long count
    };
    struct Text {
        char title[12], clock[8], note[24], count[16], unit[10], rightLabel[12], rightValue[24];
        bool chart;  // today's hour chart (the clock is valid)
        uint16_t bars[24], barMax;
        int hour;
        char chartTitle[20];  // all-time: the top types in the chart's place
        int chartRows;
        Row chartRow[kRows];
        char message[32];  // in the chart's place when there is neither
        char listTitle[24];
        int listRows;
        Row listRow[kRows];
        char listEmpty[32];
        char cardCount[8], cardNew[20], cardWhen[16], cardType[32], cardSub[24];
        bool cardNewUp, cardNewest;  // types seen grew today; there is a first sighting to show
    };
    // The text is scratch `owner` claimed from the arena.
    void attach(Text* t, const ScreenArena& a, const void* owner) { text_.attach(t, a, owner); }
    void setScene(const Scene* s) { scene_ = s, dirty_ = true; }
    void reset() { full_ = dirty_ = true, allTime_ = false; }
    // Ignored before the clock is set: there is only the all-time view then.
    void toggle() {
        if (scene_ && scene_->stats.valid) allTime_ = !allTime_, dirty_ = true;
    }
    const Text* text() const { return text_.data(); }  // what is on screen (tests)
    bool allTime() const { return allTime_; }
    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;

   private:
    void compose(Text& t) const;
    int rows(const StatsEntry* e, int n, bool airlines, Row* out) const;
    void drawRows(gfx::Canvas& c, const Row* r, int n, int y0, int step, bool airlines) const;

    const Theme& t_;
    const Scene* scene_ = nullptr;
    ArenaArray<Text, 1> text_;
    bool full_ = true, dirty_ = true, allTime_ = false;
    uint32_t sig_[4] = {};
};

class StatsScreen : public Screen {
   public:
    StatsScreen(const Theme& t, ScreenArena& a) : view_(t), arena_(a) {}
    ScreenId id() const override { return ScreenId::Stats; }
    void setScene(const Scene* s) override { view_.setScene(s); }
    void enter() override { view_.attach(arena_.claim<StatsView::Text>(this), arena_, this), view_.reset(); }
    void onVerticalSwipe(int) override { view_.toggle(); }
    bool allTime() const { return view_.allTime(); }
    const StatsView& view() const { return view_; }
    int layerCount() const override { return 1; }
    gfx::Layer* const* layers() override { return layers_; }

   private:
    StatsView view_;
    ScreenArena& arena_;
    gfx::Layer* layers_[1] = {&view_};
};

}  // namespace scr
