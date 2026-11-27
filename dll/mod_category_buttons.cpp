#include "mod_state.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace edgemod {

int32_t __cdecl GetModeSelectRowCount() {
    return 6;
}

int32_t __cdecl GetScoresSelectRowCount() {
    return (g_turboMode != 0) ? 5 : 6;
}

MenuRowRect __cdecl GetModeSelectRowRect(int row, int bx, int by, int panelH, int menuW) {
    const int backTop  = by + panelH - 0x3e;
    const int backH    = 0x32;
    const int turboH   = 0x2a;
    const int turboTop = backTop - turboH - 4;
    if (row == 5) {
        return {bx + 2, backTop, menuW - 4, backH, false};
    }
    if (row == 4) {
        return {bx + 2, turboTop, menuW - 4, turboH, false};
    }

    const int topMin = by + 44;
    const int avail  = (std::max)(0, (turboTop - 8) - topMin);
    int cardH = 62;
    int gap   = 10;
    if (4 * cardH + 3 * gap > avail) {
        gap = (std::max)(4, (avail - 4 * cardH) / 3);
        if (4 * cardH + 3 * gap > avail) {
            gap = 4;
            cardH = (std::max)(44, (avail - 3 * gap) / 4);
        }
    }
    const int totalH = 4 * cardH + 3 * gap;
    const int startY = (totalH <= avail && (by + 50 + totalH) <= (turboTop - 8))
        ? (by + 50)
        : (topMin + (std::max)(0, (avail - totalH) / 2));

    const int r = std::clamp(row, 0, 3);
    return {bx + 10, startY + r * (cardH + gap), menuW - 20, cardH, true};
}

MenuRowRect __cdecl GetScoresSelectRowRect(int row, int bx, int by, int panelH, int menuW) {
    const int backTop  = by + panelH - 0x3e;
    const int backH    = 0x32;
    const int turboH   = 0x2a;
    const int turboTop = backTop - turboH - 4;
    const int backRow  = (g_turboMode != 0) ? 4 : 5;
    const int turboRow = (g_turboMode != 0) ? 3 : 4;
    if (row == backRow) {
        return {bx + 2, backTop, menuW - 4, backH, false};
    }
    if (row == turboRow) {
        return {bx + 2, turboTop, menuW - 4, turboH, false};
    }

    const int topMin = by + 44;
    const int avail  = (std::max)(0, (turboTop - 8) - topMin);
    int cardH = 62;
    int gap   = 10;
    int lbGap = 12;
    int lbH   = 44;
    if (3 * cardH + 2 * gap + lbGap + lbH > avail) {
        gap   = 6;
        lbGap = 8;
        lbH   = 40;
        if (3 * cardH + 2 * gap + lbGap + lbH > avail) {
            gap   = 4;
            lbGap = 6;
            lbH   = 36;
            cardH = (std::max)(44, (avail - 2 * gap - lbGap - lbH) / 3);
        }
    }
    const int totalH = 3 * cardH + 2 * gap + lbGap + lbH;
    const int startY = (totalH <= avail && (by + 50 + totalH) <= (turboTop - 8))
        ? (by + 50)
        : (topMin + (std::max)(0, (avail - totalH) / 2));

    if (g_turboMode == 0 && row == 3) {
        return {bx + 2, startY + 3 * cardH + 2 * gap + lbGap, menuW - 4, lbH, false};
    }
    const int r = std::clamp(row, 0, 2);
    return {bx + 10, startY + r * (cardH + gap), menuW - 20, cardH, true};
}

} // namespace edgemod

namespace {

using namespace edgemod;

static inline void DrawQuadRgb(int x, int y, int w, int h, uint32_t rgb) {
    if (w <= 0 || h <= 0) return;
    uint8_t rgba[4] = {
        static_cast<uint8_t>((rgb >> 16) & 0xffu),
        static_cast<uint8_t>((rgb >> 8) & 0xffu),
        static_cast<uint8_t>(rgb & 0xffu),
        0xffu
    };
    Game_DrawQuad(x, y, w, h, rgba);
}

static inline void SetTextRgb(uint32_t rgb) {
    uint8_t rgba[4] = {
        static_cast<uint8_t>((rgb >> 16) & 0xffu),
        static_cast<uint8_t>((rgb >> 8) & 0xffu),
        static_cast<uint8_t>(rgb & 0xffu),
        0xffu
    };
    std::memcpy(reinterpret_cast<void*>(0x5f4268), rgba, 4);
}

static inline const wchar_t* GetWStrPtr(const EdgeWString* ws) {
    if (ws == nullptr || ws->size == 0) return L"";
    const wchar_t* p = (ws->cap >= 8) ? ws->ptr : ws->buf;
    return (p != nullptr) ? p : L"";
}

struct CategoryProgress {
    int completed = 0;
    int sOrBetter = 0;
    int sPlus     = 0;
    int total     = 0;
};

static CategoryProgress QueryCategoryProgress(int levelType) {
    CategoryProgress out{};
    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr) return out;

    using GetCountFn    = int(__thiscall*)(void*, int);
    using GetInfoFn     = uint8_t*(__thiscall*)(void*, int, int);
    using GetSaveInfoFn = uint8_t*(__thiscall*)(void*, int32_t);
    auto getCount    = reinterpret_cast<GetCountFn>(0x4a4550);
    auto getInfo     = reinterpret_cast<GetInfoFn>(0x4a4620);
    auto getSaveInfo = reinterpret_cast<GetSaveInfoFn>(0x4920e0);

    out.total = (std::max)(0, getCount(sm + 0x24c, levelType));
    for (int i = 0; i < out.total; ++i) {
        const uint8_t* lvlInfo = getInfo(sm + 0x24c, levelType, i);
        if (lvlInfo == nullptr) continue;
        int32_t levelId = *reinterpret_cast<const int32_t*>(lvlInfo + 0x3c);
        const uint8_t* saveInfo = getSaveInfo(sm + 0x298, levelId);
        if (saveInfo != nullptr && saveInfo[0] != 0) {
            ++out.completed;
            int32_t rank = *reinterpret_cast<const int32_t*>(saveInfo + 0x0c);
            if (rank >= 4) ++out.sOrBetter;
            if (rank >= 5) ++out.sPlus;
        }
    }
    return out;
}

static void BuildContinueSubtitle(wchar_t* outBuf, size_t cap) {
    if (outBuf == nullptr || cap == 0) return;
    outBuf[0] = L'\0';

    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr) return;

    using GetCountFn   = int(__thiscall*)(void*, int);
    using GetInfoFn    = uint8_t*(__thiscall*)(void*, int, int);
    using GetLastIdxFn = int(__thiscall*)(void*, int);
    auto getCount   = reinterpret_cast<GetCountFn>(0x4a4550);
    auto getInfo    = reinterpret_cast<GetInfoFn>(0x4a4620);
    auto getLastIdx = reinterpret_cast<GetLastIdxFn>(0x492060);

    int mode  = GetEffectiveLastPlayedType();
    int count = getCount(sm + 0x24c, mode);
    if (count <= 0) {
        mode  = 0;
        count = getCount(sm + 0x24c, 0);
    }
    int idx = (count > 0) ? std::clamp(getLastIdx(sm + 0x298, mode), 0, count - 1) : 0;

    static const wchar_t* const kModeNames[3] = {L"normal", L"extended", L"bonus"};
    const wchar_t* modeLabel = kModeNames[std::clamp(mode, 0, 2)];

    const uint8_t* lvlInfo = (count > 0) ? getInfo(sm + 0x24c, mode, idx) : nullptr;
    if (lvlInfo != nullptr) {
        const EdgeWString* nameWs = reinterpret_cast<const EdgeWString*>(lvlInfo + 0x40);
        const wchar_t* lvlName = GetWStrPtr(nameWs);
        if (lvlName[0] != L'\0') {
            std::swprintf(outBuf, cap, L"%s %02d: %s", modeLabel, idx + 1, lvlName);
            return;
        }
    }
    std::swprintf(outBuf, cap, L"%s %02d", modeLabel, idx + 1);
}

static void DrawCategoryProgressBar(
    int x, int y, int w, int h, const CategoryProgress& prog, int frame, int fadePct, int cardIdx
) {
    if (w < 6 || h < 6) return;

    // Outer 1px border & unfilled track both in exact Android #3B3B3B
    DrawQuadRgb(x, y, w, h, Game_ColorLerp(0xFFFFFFu, 0x3B3B3Bu, fadePct));
    const int innerX = x + 1;
    const int innerY = y + 1;
    const int innerW = w - 2;
    const int innerH = h - 2;
    if (prog.total <= 0) return;

    auto calcFillW = [&](int cnt) -> int {
        if (cnt <= 0) return 0;
        if (cnt >= prog.total) return innerW;
        return std::clamp((innerW * cnt + prog.total / 2) / prog.total, 2, innerW);
    };

    const int wComp  = calcFillW(prog.completed);
    const int wS     = (std::min)(wComp, calcFillW(prog.sOrBetter));
    const int wSPlus = (std::min)(wS, calcFillW(prog.sPlus));

    // Layer 1: Normal completed fill (exact Android #AAAAAA -> #FFFFFF horizontal gradient)
    if (wComp > wS) {
        for (int dx = wS; dx < wComp; dx += 2) {
            int sw  = (std::min)(2, wComp - dx);
            int pct = (wComp > 1) ? ((dx * 100) / (wComp - 1)) : 100;
            uint32_t base = Game_ColorLerp(0xAAAAAAu, 0xFFFFFFu, pct);
            DrawQuadRgb(innerX + dx, innerY, sw, innerH, Game_ColorLerp(0xFFFFFFu, base, fadePct));
        }
    }

    // Layer 2: S-rank fill (smooth animated rainbow)
    if (wS > wSPlus) {
        const uint32_t* kPal = reinterpret_cast<const uint32_t*>(0x589bb8);
        const float tFrame = static_cast<float>(frame) + std::clamp(g_uiAlpha, 0.0f, 1.0f);
        for (int dx = wSPlus; dx < wS; dx += 2) {
            int sw = (std::min)(2, wS - dx);
            float pos = std::fmod(
                tFrame * 0.22f + (static_cast<float>(dx) / static_cast<float>(innerW)) * 13.0f,
                13.0f
            );
            if (pos < 0.0f) pos += 13.0f;
            int i0 = static_cast<int>(pos) % 13;
            int i1 = (i0 + 1) % 13;
            int fracPct = std::clamp(static_cast<int>((pos - std::floor(pos)) * 100.0f), 0, 100);
            uint32_t base = Game_ColorLerp(kPal[i0], kPal[i1], fracPct);
            DrawQuadRgb(innerX + dx, innerY, sw, innerH, Game_ColorLerp(0xFFFFFFu, base, fadePct));
        }
    }

    // Layer 3: S+ rank fill (exact main menu border sprite stretched full bar height)
    if (wSPlus > 0 && fadePct > 0) {
        uint8_t* renderer = GetRenderer();
        uint8_t* sm       = GetScreenMgr();
        uint8_t* hGrp     = (sm != nullptr) ? *reinterpret_cast<uint8_t**>(sm + 0x58) : nullptr;
        int16_t  cnt      = (hGrp != nullptr) ? *reinterpret_cast<const int16_t*>(hGrp + 8) : 0;
        uint8_t** hSprites = (hGrp != nullptr) ? *reinterpret_cast<uint8_t***>(hGrp + 0x20) : nullptr;
        int alpha = std::clamp((fadePct * 255) / 100, 0, 255);
        if (renderer != nullptr && hSprites != nullptr && cnt > 0 && alpha > 0) {
            if (fadePct < 100) {
                DrawQuadRgb(innerX, innerY, wSPlus, innerH, 0xFFFFFFu);
            }
            int32_t savedSrc = *reinterpret_cast<const int32_t*>(renderer + 0x18);
            int32_t savedDst = *reinterpret_cast<const int32_t*>(renderer + 0x1c);
            Game_SetBlend(renderer, 0);
            const uint32_t ucnt = static_cast<uint32_t>(cnt);
            uint32_t idx = static_cast<uint32_t>(frame) + static_cast<uint32_t>(cardIdx);
            DrawSmoothRainbowSprite(hSprites, ucnt, idx, wSPlus, innerH, innerX, innerY, alpha);
            Game_SetD3DBlend(renderer, savedSrc, savedDst);
        }
    }

    // Small 1px white separator bars between adjacent filled segments
    if (wSPlus > 0 && wS > wSPlus) {
        DrawQuadRgb(innerX + wSPlus, innerY, 1, innerH, 0xFFFFFFu);
    }
    if (wS > 0 && wComp > wS) {
        DrawQuadRgb(innerX + wS, innerY, 1, innerH, 0xFFFFFFu);
    }
}

static void DrawCategoryCard(
    const MenuRowRect& r, const EdgeWString* titleWs, int levelType,
    bool isSelected, bool isExitConfirm, int frame, int fadePct,
    uint32_t selBorderRgb, uint32_t selFillRgb, int cardIdx,
    bool solidHeader, int unfoldPct
) {
    const int fullInnerH = r.h - 4;
    const int headerH    = std::clamp((fullInnerH * 41) / 100, 20, 24);
    const int minCardH   = headerH + 4;
    const int clampedUnfold = std::clamp(unfoldPct, 0, 100);
    const int curH = (clampedUnfold >= 100)
        ? r.h
        : (minCardH + ((r.h - minCardH) * clampedUnfold + 50) / 100);
    const int curBodyH = curH - 4 - headerH;

    const int innerCardX = r.x + 2;
    const int innerCardY = r.y + 2;
    const int innerCardW = r.w - 4;
    const int bodyY      = innerCardY + headerH;

    const int hdrFade = solidHeader ? 100 : fadePct;

    // Full 2px outer border matching Android DrawLevelPackButton & Menu::drawButton
    const uint32_t borderRgb = (isExitConfirm && isSelected && !solidHeader)
        ? selBorderRgb
        : Game_ColorLerp(0xFFFFFFu, 0x7D7D7Du, hdrFade);
    DrawQuadRgb(r.x, r.y, r.w, curH, borderRgb);

    // Dark #3B3B3B header strip
    DrawQuadRgb(innerCardX, innerCardY, innerCardW, headerH, Game_ColorLerp(0xFFFFFFu, 0x3B3B3Bu, hdrFade));

    // Body background: pure white when unhovered, rainbow tint on hover/click
    if (curBodyH > 0) {
        uint32_t bodyBase = 0xFFFFFFu;
        if (isSelected) {
            if (isExitConfirm) {
                bodyBase = (selFillRgb == 0x808080u) ? 0x808080u : GetSmoothMenuRainbow(frame);
            } else if (!IsMenuRenderForceWhite()) {
                uint32_t rb = ApplyMenuSlotRainbowStyle(GetSmoothMenuRainbow(frame));
                if (rb != 0xFFFFFFu) {
                    bodyBase = rb;
                }
            }
        }
        const uint32_t bodyRgb = Game_ColorLerp(0xFFFFFFu, bodyBase, fadePct);
        DrawQuadRgb(innerCardX, bodyY, innerCardW, curBodyH, bodyRgb);
    }

    // Header title (white text on dark header, left+vcenter, light font, no '>' arrow)
    SetTextRgb(0xFFFFFFu);
    DrawTextAutoFit(titleWs, innerCardX + 12, innerCardY + headerH / 2, 0x1800, 0x1800, 0, 0, 0x06, innerCardW - 24);

    // Body content (fades smoothly after the body unfolds enough to avoid clipping)
    const int contentFade = solidHeader
        ? std::clamp(((clampedUnfold - 35) * 100) / 65, 0, 100)
        : fadePct;
    if (curBodyH >= 12 && contentFade > 0) {
        const int bodyCenterY = bodyY + curBodyH / 2;
        if (levelType >= 0) {
            const CategoryProgress prog = QueryCategoryProgress(levelType);
            const int barX = innerCardX + 12;
            const int barH = (curBodyH >= 28) ? 10 : 8;
            const int barY = bodyY + (curBodyH - barH) / 2;
            const int barW = innerCardW - 12 - 60;

            DrawCategoryProgressBar(barX, barY, barW, barH, prog, frame, contentFade, cardIdx);

            wchar_t cntBuf[32]{};
            std::swprintf(cntBuf, 32, L"%d/%d", prog.completed, prog.total);
            EdgeWString cntWs{};
            cntWs.set_literal(cntBuf, static_cast<uint32_t>(std::wcslen(cntBuf)));
            SetTextRgb(Game_ColorLerp(0xFFFFFFu, 0x3B3B3Bu, contentFade));
            Game_DrawText(&cntWs, barX + barW + 6, bodyCenterY, 0x1000, 0x1000, 0, 0, 0x06);
        } else {
            wchar_t subBuf[160]{};
            BuildContinueSubtitle(subBuf, 160);
            EdgeWString subWs{};
            subWs.set_literal(subBuf, static_cast<uint32_t>(std::wcslen(subBuf)));
            SetTextRgb(Game_ColorLerp(0xFFFFFFu, 0x3B3B3Bu, contentFade));
            DrawTextAutoFit(&subWs, innerCardX + 12, bodyCenterY, 0x1800, 0x1800, 0, 0, 0x06, innerCardW - 24);
        }
    }
}

static void DrawMorphingCardHeader(
    const MenuRowRect& r, const EdgeWString* titleWs, int bx, int by, int menuW,
    float t, uint32_t selBorderRgb, uint32_t selFillRgb, uint32_t selTextRgb
) {
    t = std::clamp(t, 0.0f, 1.0f);
    if (r.isCard) {
        const int headerH = std::clamp(((r.h - 4) * 41) / 100, 20, 24);
        const int startX  = bx + 2;
        const int startY  = by + 2;
        const int startW  = menuW - 4;
        const int startH  = 0x22;
        const int endH    = headerH + 4;

        auto lerpI = [&](int a, int b) -> int {
            return static_cast<int>(std::lrint(static_cast<float>(a) + static_cast<float>(b - a) * t));
        };

        const int drawX = lerpI(startX, r.x);
        const int drawY = lerpI(startY, r.y);
        const int drawW = lerpI(startW, r.w);
        const int drawH = lerpI(startH, endH);
        const int tPct  = std::clamp(static_cast<int>(std::lrint(t * 100.0f)), 0, 100);

        const uint32_t borderRgb = Game_ColorLerp(0x808080u, 0x7D7D7Du, tPct);
        const uint32_t fillRgb   = Game_ColorLerp(0xFFFFFFu, 0x3B3B3Bu, tPct);
        const uint32_t textRgb   = Game_ColorLerp(0x808080u, 0xFFFFFFu, tPct);

        DrawQuadRgb(drawX, drawY, drawW, drawH, borderRgb);
        DrawQuadRgb(drawX + 2, drawY + 2, drawW - 4, drawH - 4, fillRgb);

        const int textX     = lerpI(bx + 0x1e, r.x + 14);
        const int textY     = drawY + drawH / 2;
        const int textScale = lerpI(0x2000, 0x1800);
        SetTextRgb(textRgb);
        DrawTextAutoFit(titleWs, textX, textY, textScale, textScale, 0, 0, 0x06, drawW - 28);
    } else {
        const int drawY = static_cast<int>(std::lrint(
            static_cast<float>(by + 2) + static_cast<float>(r.y - (by + 2)) * t
        ));
        if (selBorderRgb != selFillRgb) {
            DrawQuadRgb(r.x, drawY - 2, r.w, r.h + 4, selBorderRgb);
            DrawQuadRgb(r.x + 2, drawY, r.w - 4, r.h, selFillRgb);
        } else {
            DrawQuadRgb(r.x, drawY, r.w, r.h, selFillRgb);
        }
        SetTextRgb(selTextRgb);
        DrawTextAutoFit(titleWs, bx + 0x1e, drawY + (r.h - 18) / 2, 0x2000, 0x2000, 1, 0, 0x04, 330);
    }
}

static void DrawStandardRow(
    const MenuRowRect& r, const EdgeWString* titleWs, int bx, bool isSelected,
    uint32_t selBorderRgb, uint32_t selFillRgb, uint32_t selTextRgb, uint32_t normalTextRgb
) {
    if (isSelected) {
        if (selBorderRgb != selFillRgb) {
            DrawQuadRgb(r.x, r.y - 2, r.w, r.h + 4, selBorderRgb);
            DrawQuadRgb(r.x + 2, r.y, r.w - 4, r.h, selFillRgb);
        } else {
            DrawQuadRgb(r.x, r.y, r.w, r.h, selFillRgb);
        }
        SetTextRgb(selTextRgb);
    } else {
        SetTextRgb(normalTextRgb);
    }
    DrawTextAutoFit(titleWs, bx + 0x1e, r.y + (r.h - 18) / 2, 0x2000, 0x2000, 1, 0, 0x04, 330);
}

static void DrawTurboToggleRow(
    const MenuRowRect& r, int bx, int menuW, bool isSelected,
    uint32_t selBorderRgb, uint32_t selFillRgb, uint32_t selTextRgb, uint32_t normalTextRgb
) {
    if (isSelected) {
        if (selBorderRgb != selFillRgb) {
            DrawQuadRgb(r.x, r.y - 2, r.w, r.h + 4, selBorderRgb);
            DrawQuadRgb(r.x + 2, r.y, r.w - 4, r.h, selFillRgb);
        } else {
            DrawQuadRgb(r.x, r.y, r.w, r.h, selFillRgb);
        }
    }

    const int rightBound = bx + menuW - 8;
    const int labelX     = bx + 0x1e;
    const int valX       = bx + 0xf0;
    constexpr int kMinColGap = 6;
    const int rDy        = (0x2a - r.h) / 2;
    const int lblTextY   = r.y + 0xc - rDy;
    const int valTextY   = r.y + 0x7 - rDy;
    const int indY       = r.y + (std::max)(0x16, 0x20 - (0x2a - r.h));

    const uint32_t fgRgb  = isSelected ? selTextRgb : normalTextRgb;
    const uint32_t bgRgb  = isSelected ? selFillRgb : 0xffffffu;
    const uint32_t dimRgb = Game_ColorLerp(bgRgb, fgRgb, 28);

    SetTextRgb(fgRgb);
    EDGE_STATIC_WSTR(kTurboLabel, "turbo mode");
    DrawTextAutoFit(&kTurboLabel, labelX, lblTextY, 0x2000, 0x2000, 1, 0, 0x14, (valX - kMinColGap) - labelX);

    EdgeWString valWs{};
    valWs.clear_sso();
    Game_GetLocalizedWString(&valWs, (g_turboMode != 0) ? 0xb : 0xc); // "on" : "off"
    DrawTextAutoFit(&valWs, valX, valTextY, 0x2000, 0x2000, 1, 0, 0x14, rightBound - valX);
    valWs.free_if_heap();

    constexpr int kToggleW = 22;
    constexpr int kKnobW   = 11;
    DrawQuadRgb(valX, indY - 1, kToggleW, 5, dimRgb);
    DrawQuadRgb(valX + (g_turboMode ? (kToggleW - kKnobW) : 0), indY - 1, kKnobW, 5, fgRgb);
}

static void __cdecl RenderModeSelectRows_Impl(
    const uint8_t* menu, int frame, int bx, int by,
    uint32_t selBorderRgb, uint32_t selFillRgb, uint32_t selTextRgb, uint32_t normalTextRgb
) {
    if (menu == nullptr) return;
    const int32_t sel    = *reinterpret_cast<const int32_t*>(menu + 0x10);
    const int32_t panelH = reinterpret_cast<int32_t(__cdecl*)()>(0x496060)();
    const int32_t menuW  = *reinterpret_cast<const int32_t*>(0x589ddc);

    uint8_t* sm = GetScreenMgr();
    const int32_t prevScreen  = (sm != nullptr) ? *reinterpret_cast<const int32_t*>(sm + 0x54) : 2;
    const int32_t enterFrames = *reinterpret_cast<const int32_t*>(menu + 0x4);
    const int32_t exitTimer   = *reinterpret_cast<const int32_t*>(menu + 0x8);
    const int32_t exitDest    = *reinterpret_cast<const int32_t*>(menu + 0xc);

    float sm4 = 8.0f, sm8 = -1.0f;
    GetSmoothSubmenuCounters(menu, &sm4, &sm8);

    const bool isEnterChild  = (enterFrames < 8 && prevScreen != 2);
    const bool isExitChild   = (enterFrames >= 8 && exitTimer >= 0 && exitDest != 2);
    const bool isExitConfirm = isExitChild;

    static const int kStrIds[6]   = {2, 1, 0x3f, 0x2c, -1, 0};
    static const int kLvlTypes[6] = {-1, 0, 1, 2, -1, -1};

    auto getRowStrId = [&](int row) -> int {
        if (row < 0 || row >= 6) return 0;
        if (row == 0) {
            if (sm != nullptr) {
                auto hasAny = reinterpret_cast<uint8_t(__thiscall*)(void*, int)>(0x494450);
                if (hasAny(sm + 0x2e8, 0) != 0 || hasAny(sm + 0x2e8, 1) != 0 || hasAny(sm + 0x2e8, 2) != 0) {
                    return 2; // "continue"
                }
            }
            return 7; // "new game"
        }
        return kStrIds[row];
    };

    if (isEnterChild && sm4 < 4.0f) {
        if (sel >= 0 && sel < 6 && sel != 4 && (sel < 1 || sel > 3 || menu[0x14] != 0)) {
            MenuRowRect r = GetModeSelectRowRect(sel, bx, by, panelH, menuW);
            EdgeWString ws{};
            ws.clear_sso();
            Game_GetLocalizedWString(&ws, getRowStrId(sel));
            DrawMorphingCardHeader(r, &ws, bx, by, menuW, sm4 / 4.0f, selBorderRgb, selFillRgb, selTextRgb);
            ws.free_if_heap();
        }
        return;
    }

    if (isExitChild && sm8 <= 4.0f) {
        if (sel >= 0 && sel < 6 && sel != 4 && (sel < 1 || sel > 3 || menu[0x14] != 0)) {
            MenuRowRect r = GetModeSelectRowRect(sel, bx, by, panelH, menuW);
            EdgeWString ws{};
            ws.clear_sso();
            Game_GetLocalizedWString(&ws, getRowStrId(sel));
            DrawMorphingCardHeader(r, &ws, bx, by, menuW, sm8 / 4.0f, selBorderRgb, selFillRgb, selTextRgb);
            ws.free_if_heap();
        }
        return;
    }

    int32_t fadePct = GetSmoothSubmenuFadePct(menu, 1);
    bool morphUnfold = false;
    if (isEnterChild) {
        fadePct = std::clamp(static_cast<int32_t>(std::lrint((sm4 - 4.0f) * 25.0f)), 0, 100);
        morphUnfold = true;
    } else if (isExitChild) {
        fadePct = std::clamp(static_cast<int32_t>(std::lrint((sm8 - 4.0f) * 25.0f)), 0, 100);
        morphUnfold = true;
    }

    const int rowCount = GetModeSelectRowCount();
    for (int i = 0; i < rowCount; ++i) {
        if (i >= 1 && i <= 3 && menu[0x14] == 0) continue;
        MenuRowRect r = GetModeSelectRowRect(i, bx, by, panelH, menuW);
        const bool isSel = (sel == i);
        if (i == 4) {
            DrawTurboToggleRow(r, bx, menuW, isSel, selBorderRgb, selFillRgb, selTextRgb, normalTextRgb);
            continue;
        }
        EdgeWString ws{};
        ws.clear_sso();
        Game_GetLocalizedWString(&ws, getRowStrId(i));
        if (r.isCard) {
            const bool solidHdr = (morphUnfold && isSel);
            const int  unfold   = solidHdr ? fadePct : 100;
            DrawCategoryCard(
                r, &ws, kLvlTypes[i],
                isSel, isExitConfirm, frame, fadePct, selBorderRgb, selFillRgb, i,
                solidHdr, unfold
            );
        } else {
            DrawStandardRow(r, &ws, bx, isSel, selBorderRgb, selFillRgb, selTextRgb, normalTextRgb);
        }
        ws.free_if_heap();
    }
}

static void __cdecl RenderScoresSelectRows_Impl(
    const uint8_t* menu, int frame, int bx, int by,
    uint32_t selBorderRgb, uint32_t selFillRgb, uint32_t selTextRgb, uint32_t normalTextRgb
) {
    if (menu == nullptr) return;
    const int32_t sel    = *reinterpret_cast<const int32_t*>(menu + 0x10);
    const int32_t panelH = reinterpret_cast<int32_t(__cdecl*)()>(0x496060)();
    const int32_t menuW  = *reinterpret_cast<const int32_t*>(0x589ddc);

    uint8_t* sm = GetScreenMgr();
    const int32_t prevScreen  = (sm != nullptr) ? *reinterpret_cast<const int32_t*>(sm + 0x54) : 2;
    const int32_t enterFrames = *reinterpret_cast<const int32_t*>(menu + 0x4);
    const int32_t exitTimer   = *reinterpret_cast<const int32_t*>(menu + 0x8);
    const int32_t exitDest    = *reinterpret_cast<const int32_t*>(menu + 0xc);

    float sm4 = 8.0f, sm8 = -1.0f;
    GetSmoothSubmenuCounters(menu, &sm4, &sm8);

    const bool isEnterChild  = (enterFrames < 8 && prevScreen != 2);
    const bool isExitChild   = (enterFrames >= 8 && exitTimer >= 0 && exitDest != 2);
    const bool isExitConfirm = isExitChild;

    const int rowCount = GetScoresSelectRowCount();
    const int turboRow = (g_turboMode != 0) ? 3 : 4;
    const int backRow  = (g_turboMode != 0) ? 4 : 5;

    auto getScoresStrId = [&](int row) -> int {
        if (row == 0) return 0x3b; // normal scores
        if (row == 1) return 0x46; // extended scores
        if (row == 2) return 0x37; // bonus scores
        if (row == backRow) return 0; // back
        if (g_turboMode == 0 && row == 3) return 0x3a; // leaderboards
        return 0;
    };

    if (isEnterChild && sm4 < 4.0f) {
        if (sel >= 0 && sel < rowCount && sel != turboRow) {
            MenuRowRect r = GetScoresSelectRowRect(sel, bx, by, panelH, menuW);
            EdgeWString ws{};
            ws.clear_sso();
            Game_GetLocalizedWString(&ws, getScoresStrId(sel));
            DrawMorphingCardHeader(r, &ws, bx, by, menuW, sm4 / 4.0f, selBorderRgb, selFillRgb, selTextRgb);
            ws.free_if_heap();
        }
        return;
    }

    if (isExitChild && sm8 <= 4.0f) {
        if (sel >= 0 && sel < rowCount && sel != turboRow) {
            MenuRowRect r = GetScoresSelectRowRect(sel, bx, by, panelH, menuW);
            EdgeWString ws{};
            ws.clear_sso();
            Game_GetLocalizedWString(&ws, getScoresStrId(sel));
            DrawMorphingCardHeader(r, &ws, bx, by, menuW, sm8 / 4.0f, selBorderRgb, selFillRgb, selTextRgb);
            ws.free_if_heap();
        }
        return;
    }

    const int32_t fadeMode = (enterFrames >= 8 && exitTimer >= 0 && exitDest == 0x10) ? 4 : 1;
    int32_t fadePct = GetSmoothSubmenuFadePct(menu, fadeMode);
    bool morphUnfold = false;
    if (isEnterChild) {
        fadePct = std::clamp(static_cast<int32_t>(std::lrint((sm4 - 4.0f) * 25.0f)), 0, 100);
        morphUnfold = true;
    } else if (isExitChild && exitDest != 0x10) {
        fadePct = std::clamp(static_cast<int32_t>(std::lrint((sm8 - 4.0f) * 25.0f)), 0, 100);
        morphUnfold = true;
    }

    for (int i = 0; i < rowCount; ++i) {
        MenuRowRect r = GetScoresSelectRowRect(i, bx, by, panelH, menuW);
        const bool isSel = (sel == i);
        if (i == turboRow) {
            DrawTurboToggleRow(r, bx, menuW, isSel, selBorderRgb, selFillRgb, selTextRgb, normalTextRgb);
            continue;
        }
        EdgeWString ws{};
        ws.clear_sso();
        Game_GetLocalizedWString(&ws, getScoresStrId(i));
        if (r.isCard) {
            const bool solidHdr = (morphUnfold && isSel);
            const int  unfold   = solidHdr ? fadePct : 100;
            DrawCategoryCard(
                r, &ws, i,
                isSel, isExitConfirm, frame, fadePct, selBorderRgb, selFillRgb, i,
                solidHdr, unfold
            );
        } else {
            DrawStandardRow(r, &ws, bx, isSel, selBorderRgb, selFillRgb, selTextRgb, normalTextRgb);
        }
        ws.free_if_heap();
    }
}

static int32_t __cdecl HitTestModeSelect_Impl(uint8_t* menu, const int32_t* pt) {
    if (menu == nullptr || pt == nullptr) return -1;
    int basePt[2] = {0, 0};
    reinterpret_cast<void(__cdecl*)(int*)>(0x496030)(basePt);
    int panelH = reinterpret_cast<int32_t(__cdecl*)()>(0x496060)();
    int menuW  = *reinterpret_cast<const int32_t*>(0x589ddc);
    const int mx = pt[0];
    const int my = pt[1];
    const int count = GetModeSelectRowCount();

    for (int i = 0; i < count; ++i) {
        if (i >= 1 && i <= 3 && menu[0x14] == 0) continue;
        MenuRowRect r = GetModeSelectRowRect(i, basePt[0], basePt[1], panelH, menuW);
        if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) {
            int32_t& curSel = *reinterpret_cast<int32_t*>(menu + 0x10);
            if (curSel != i) {
                uint8_t* sm = GetScreenMgr();
                if (sm != nullptr) Game_PlaySfx(sm, 2);
                curSel = i;
            }
            return i;
        }
    }
    return -1;
}

static int32_t __cdecl HitTestScoresSelect_Impl(uint8_t* menu, const int32_t* pt) {
    if (menu == nullptr || pt == nullptr) return -1;
    int basePt[2] = {0, 0};
    reinterpret_cast<void(__cdecl*)(int*)>(0x496030)(basePt);
    int panelH = reinterpret_cast<int32_t(__cdecl*)()>(0x496060)();
    int menuW  = *reinterpret_cast<const int32_t*>(0x589ddc);
    const int mx = pt[0];
    const int my = pt[1];
    const int count = GetScoresSelectRowCount();

    for (int i = 0; i < count; ++i) {
        MenuRowRect r = GetScoresSelectRowRect(i, basePt[0], basePt[1], panelH, menuW);
        if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) {
            int32_t& curSel = *reinterpret_cast<int32_t*>(menu + 0x10);
            if (curSel != i) {
                uint8_t* sm = GetScreenMgr();
                if (sm != nullptr) Game_PlaySfx(sm, 2);
                curSel = i;
            }
            return i;
        }
    }
    return -1;
}

static void __cdecl ToggleTurboModeInMenu(uint8_t* menu, int isScoresSelect) {
    g_turboMode = (g_turboMode != 0) ? 0 : 1;
    if (isScoresSelect != 0 && menu != nullptr) {
        *reinterpret_cast<int32_t*>(menu + 0x10) = (g_turboMode != 0) ? 3 : 4;
    }
    EnsureTurboScoresLoaded();
    SaveModSettingsPublic();
    if (uint8_t* sm = GetScreenMgr()) {
        Game_PlaySfx(sm, 2);
    }
}

static void __cdecl ModeSelectUpdate_Impl(uint8_t* menu, float dt) {
    if (menu == nullptr) return;
    int32_t& enterFrames = *reinterpret_cast<int32_t*>(menu + 4);
    int32_t& exitTimer   = *reinterpret_cast<int32_t*>(menu + 8);
    int32_t& sel         = *reinterpret_cast<int32_t*>(menu + 0x10);
    const int count      = GetModeSelectRowCount();
    if (sel < 0 || sel >= count) sel = 0;

    uint8_t* sm = GetScreenMgr();
    if (enterFrames >= 8 && exitTimer < 0 && sm != nullptr) {
        if (sm[0xad] != 0) {
            Game_PlaySfx(sm, 2);
            do {
                sel = (sel > 0) ? (sel - 1) : (count - 1);
            } while (sel >= 1 && sel <= 3 && menu[0x14] == 0);
        } else if (sm[0xb0] != 0) {
            Game_PlaySfx(sm, 2);
            do {
                sel = (sel + 1 < count) ? (sel + 1) : 0;
            } while (sel >= 1 && sel <= 3 && menu[0x14] == 0);
        } else if ((sm[0xb3] != 0 || sm[0xb6] != 0) && sel == 4) {
            ToggleTurboModeInMenu(menu, 0);
        } else if (sm[0xc8] != 0) {
            reinterpret_cast<void(__thiscall*)(void*)>(0x4b8ab0)(menu);
        } else if (sm[0xc5] != 0) {
            reinterpret_cast<void(__thiscall*)(void*)>(0x4b8c10)(menu);
        }
    }
    if (enterFrames < 8) {
        ++enterFrames;
    }
    reinterpret_cast<void(__thiscall*)(void*, float)>(0x495e70)(menu, dt);
}

static void __cdecl ScoresSelectUpdate_Impl(uint8_t* menu, float dt) {
    if (menu == nullptr) return;
    int32_t& enterFrames = *reinterpret_cast<int32_t*>(menu + 4);
    int32_t& exitTimer   = *reinterpret_cast<int32_t*>(menu + 8);
    int32_t& sel         = *reinterpret_cast<int32_t*>(menu + 0x10);
    const int count      = GetScoresSelectRowCount();
    const int turboRow   = (g_turboMode != 0) ? 3 : 4;
    if (sel < 0 || sel >= count) sel = count - 1;

    uint8_t* sm = GetScreenMgr();
    if (enterFrames >= 8 && exitTimer < 0 && sm != nullptr) {
        if (sm[0xad] != 0) {
            Game_PlaySfx(sm, 2);
            sel = (sel > 0) ? (sel - 1) : (count - 1);
        } else if (sm[0xb0] != 0) {
            Game_PlaySfx(sm, 2);
            sel = (sel + 1 < count) ? (sel + 1) : 0;
        } else if ((sm[0xb3] != 0 || sm[0xb6] != 0) && sel == turboRow) {
            ToggleTurboModeInMenu(menu, 1);
        } else if (sm[0xc8] != 0) {
            reinterpret_cast<void(__thiscall*)(void*)>(0x4b1a10)(menu);
        } else if (sm[0xc5] != 0) {
            reinterpret_cast<void(__thiscall*)(void*)>(0x4b23d0)(menu);
        }
    }
    if (enterFrames < 8) {
        ++enterFrames;
    }
    reinterpret_cast<void(__thiscall*)(void*, float)>(0x495e70)(menu, dt);
}

static int32_t __cdecl ScoresSelectActivate_Impl(uint8_t* menu) {
    if (menu == nullptr) return -1;
    const int32_t sel = *reinterpret_cast<const int32_t*>(menu + 0x10);
    if (sel >= 0 && sel <= 2) {
        return sel; // 0=normal(0xc), 1=bonus(0xd), 2=extended(0xe)
    }
    if (g_turboMode == 0) {
        if (sel == 3) return 3; // leaderboards URL
        if (sel == 4) {
            ToggleTurboModeInMenu(menu, 1);
            return -1;
        }
        if (sel == 5) return 4; // back -> MainMenu (2)
    } else {
        if (sel == 3) {
            ToggleTurboModeInMenu(menu, 1);
            return -1;
        }
        if (sel == 4) return 4; // back -> MainMenu (2)
    }
    return -1;
}

static EdgeWString* __cdecl GetLocalizedOrTurbo_Impl(EdgeWString* out, int32_t strId) {
    if (g_turboMode != 0 && out != nullptr) {
        out->set_literal(L"turbo", 5);
        return out;
    }
    Game_GetLocalizedWString(out, strId);
    return out;
}

static void __declspec(naked) hk_modeSelectHitTest() {
    __asm {
        push DWORD PTR [esp + 4]
        push ecx
        call HitTestModeSelect_Impl
        add esp, 8
        ret 4
    }
}

static void __declspec(naked) hk_scoresSelectHitTest() {
    __asm {
        push DWORD PTR [esp + 4]
        push ecx
        call HitTestScoresSelect_Impl
        add esp, 8
        ret 4
    }
}

static void __declspec(naked) hk_modeSelectRenderRows() {
    __asm {
        push DWORD PTR [ebp - 0x6c] // normalTextRgb
        push DWORD PTR [ebp - 0x60] // selTextRgb
        push DWORD PTR [ebp - 0x58] // selFillRgb
        push ebx                    // selBorderRgb
        push DWORD PTR [ebp - 0x50] // by
        push DWORD PTR [ebp - 0x54] // bx
        push DWORD PTR [ebp - 0x68] // frame
        push DWORD PTR [ebp - 0x74] // menu
        call RenderModeSelectRows_Impl
        add esp, 32
        mov edi, DWORD PTR [ebp - 0x74]
        mov eax, 0x4b963d
        jmp eax
    }
}

static void __declspec(naked) hk_scoresSelectRenderRows() {
    __asm {
        push DWORD PTR [ebp - 0x70] // normalTextRgb
        push DWORD PTR [ebp - 0x50] // selTextRgb
        push DWORD PTR [ebp - 0x48] // selFillRgb
        push ebx                    // selBorderRgb
        push DWORD PTR [ebp - 0x5c] // by
        push DWORD PTR [ebp - 0x60] // bx
        push DWORD PTR [ebp - 0x74] // frame
        push DWORD PTR [ebp - 0x6c] // menu
        call RenderScoresSelectRows_Impl
        add esp, 32
        mov eax, 0x4b237e
        jmp eax
    }
}

static void __declspec(naked) hk_modeSelectUpdate() {
    __asm {
        push DWORD PTR [esp + 4]
        push ecx
        call ModeSelectUpdate_Impl
        add esp, 8
        ret 4
    }
}

static void __declspec(naked) hk_modeSelectActivateTail() {
    __asm {
        cmp eax, 4
        jne ms_act_check_back
        push 0
        push esi
        call ToggleTurboModeInMenu
        add esp, 8
        mov eax, 0x4b8d2c
        jmp eax
    ms_act_check_back:
        mov ecx, 0x4b8d11
        jmp ecx
    }
}

static void __declspec(naked) hk_scoresSelectUpdate() {
    __asm {
        push DWORD PTR [esp + 4]
        push ecx
        call ScoresSelectUpdate_Impl
        add esp, 8
        ret 4
    }
}

static void __declspec(naked) hk_scoresSelectActivate() {
    __asm {
        push esi
        call ScoresSelectActivate_Impl
        add esp, 4
        cmp eax, 4
        ja ss_act_done
        mov ecx, 0x4b23ef
        jmp ecx
    ss_act_done:
        mov ecx, 0x4b2486
        jmp ecx
    }
}

} // namespace

void RegisterCategoryButtons() {
    using namespace edgemod;

    // 1. ModeSelect::hitTest (0x4b8b60)
    static const uint8_t exMsHit[8] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x0c, 0x53, 0x56};
    RegRelJmp(0x4b8b60, exMsHit, 8, 8, &hk_modeSelectHitTest, 0x90);

    // 2. ScoresSelect::hitTest (0x4b1970)
    static const uint8_t exSsHit[8] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x0c, 0x53, 0x56};
    RegRelJmp(0x4b1970, exSsHit, 8, 8, &hk_scoresSelectHitTest, 0x90);

    // 3. ModeSelect::render rows (0x4b93f6..0x4b963d)
    static const uint8_t exMsRen[10] = {0x8b, 0x7d, 0x8c, 0x83, 0x7f, 0x10, 0x05, 0x75, 0x0a, 0xe8};
    RegRelJmp(0x4b93f6, exMsRen, 10, 5, &hk_modeSelectRenderRows, 0x90, 0x4b963d);

    // 4. ScoresSelect::render rows (0x4b2166..0x4b237e)
    static const uint8_t exSsRen[10] = {0x8b, 0x55, 0x94, 0x8b, 0x72, 0x10, 0x83, 0xfe, 0x04, 0x75};
    RegRelJmp(0x4b2166, exSsRen, 10, 5, &hk_scoresSelectRenderRows, 0x90, 0x4b237e);

    // 5. ModeSelect::update (0x4b8d30) & activate tail (0x4b8cf4) for 6-row layout + turbo mode toggle
    static const uint8_t exMsUpd[8] = {0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1, 0x83, 0x7e};
    RegRelJmp(0x4b8d30, exMsUpd, 8, 8, &hk_modeSelectUpdate, 0x90);

    static const uint8_t exMsAct[7] = {0x83, 0xf8, 0x04, 0x75, 0x18, 0x8b, 0x0d};
    RegRelJmp(0x4b8cf4, exMsAct, 7, 5, &hk_modeSelectActivateTail, 0x90, 0x4b8d11);

    // 6. ScoresSelect::update (0x4b24b0) & activate switch (0x4b23e3) for turbo mode toggle & leaderboards hiding
    static const uint8_t exSsUpd[8] = {0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1, 0x83, 0x7e};
    RegRelJmp(0x4b24b0, exSsUpd, 8, 8, &hk_scoresSelectUpdate, 0x90);

    static const uint8_t exSsAct[8] = {0x8b, 0x46, 0x10, 0x83, 0xf8, 0x04, 0x0f, 0x87};
    RegRelJmp(0x4b23e3, exSsAct, 8, 5, &hk_scoresSelectActivate, 0x90, 0x4b23ef);

    // 7. Replace "level" label calls with "turbo" while g_turboMode != 0 (Debrief, Pause, ScoresMenu, LevelSelect)
    static const uint8_t exDebLbl[5] = {0xe8, 0xc3, 0x61, 0xff, 0xff};
    RegRelCall(0x4a9ba8, exDebLbl, 5, 5, &GetLocalizedOrTurbo_Impl);

    static const uint8_t exPauLbl[5] = {0xe8, 0x95, 0x1a, 0xff, 0xff};
    RegRelCall(0x4ae2d6, exPauLbl, 5, 5, &GetLocalizedOrTurbo_Impl);

    static const uint8_t exScoLblF[5] = {0xe8, 0x2a, 0xfd, 0xfe, 0xff};
    RegRelCall(0x4b0041, exScoLblF, 5, 5, &GetLocalizedOrTurbo_Impl);

    static const uint8_t exScoLblB[5] = {0xe8, 0x76, 0xf1, 0xfe, 0xff};
    RegRelCall(0x4b0bf5, exScoLblB, 5, 5, &GetLocalizedOrTurbo_Impl);

    static const uint8_t exLsLblU[5] = {0xe8, 0x28, 0x8e, 0xfe, 0xff};
    RegRelCall(0x4b6f43, exLsLblU, 5, 5, &GetLocalizedOrTurbo_Impl);

    static const uint8_t exLsLblC[5] = {0xe8, 0x40, 0x8c, 0xfe, 0xff};
    RegRelCall(0x4b712b, exLsLblC, 5, 5, &GetLocalizedOrTurbo_Impl);
}
