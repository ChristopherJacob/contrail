#pragma once
// Every colour a screen uses. Screens never hard-code colours, so a second theme (Bold
// Editorial, `editorial()`) is a second instance of this struct; `ScreenApp` draws from its own copy.
#include "gfx/Color.h"
#include "gfx/Font.h"

namespace scr {

struct Theme {
    gfx::Rgb text, textSoft, textDim;
    gfx::Rgb accent;                   // route arrows, progress, selection
    gfx::Rgb climb, descend, level;    // vertical trend
    gfx::Rgb card, cardEdge, tile, rowHighlight, rowFill;
    gfx::Rgb veil;                     // transition dip colour
    gfx::Rgb shadow;
    gfx::Rgb boardBgTop, boardBgBottom;
    gfx::Rgb radarBg[3];               // centre, middle, edge
    gfx::Rgb radarLine, radarText, radarBlip, radarHeader, radarCard, radarCardEdge;
    gfx::Rgb radarBlipCore;            // the bright dot at a blip's centre
    gfx::Rgb tree, moon, moonGlow, beacon, home;
    gfx::Rgb sunGlow, sunCore, sunDisc;
    // Look-Up Sky View: stars, the moon's earthlit side, planes and their lights, and the
    // contrails by day, at sunset (lit from below the horizon) and by moonlight.
    gfx::Rgb star, moonDark, skyPlane, skyPlaneGlow, navPort, navStarboard;
    gfx::Rgb contrailDay, contrailDusk, contrailNight;
    gfx::Rgb airglow;      // the night sky's faint glow low down, which shows the treeline up
    gfx::Rgb beltOfVenus;  // the pink band opposite a low sun

    // Pass 2. The display roles: the biggest callsigns and counts, headings, and callsigns in
    // lists and airport codes. Body and small text are Inter in every theme (gfx::kFontBody,
    // gfx::kFontSmall), so they are not roles.
    const gfx::FontData* fontDisplay;
    const gfx::FontData* fontTitle;
    const gfx::FontData* fontLabel;
    bool flat;        // solid fills: no gradients, glows or dithering (the ui:: fill helpers honour it)
    bool brandFill;   // airline colours fill areas (Closest's sky band, Board rows, Map planes, Stats bars)
    bool upperHeads;  // headings in capitals ("6 FLIGHTS")
    gfx::Rgb brandNeutral;  // the fill or stripe for an unknown airline
    // Live Map (night chart).
    gfx::Rgb mapWater, mapLand, mapCoast, mapRing, mapText, mapAirport, mapRunway;
    gfx::Rgb mapLow, mapMid, mapHigh;  // planes below 10,000 ft, 10,000-25,000 ft, above
    gfx::Rgb mapCard, mapCardEdge;
    // Live Map from a map pack (5b): major and minor roads, rivers, town dots and names.
    gfx::Rgb mapRoad, mapRoadMinor, mapRiver, mapTown;
    // Stats. statsBar fills the top types' bars; the hour chart is statsPast / statsNow / statsFuture.
    gfx::Rgb statsBar, statsPast, statsNow, statsFuture, statsTrack, statsCard, statsCardEdge;
};

const Theme& cinematicSky();
const Theme& editorial();
// The `theme` setting: "editorial" is Bold Editorial; anything else (or null) Cinematic Sky.
const Theme& themeByName(const char* name);

}  // namespace scr
