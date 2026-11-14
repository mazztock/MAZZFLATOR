/*
 * MAZZFLATOR - interface (skin 2026)
 * Copyright (C) 2026 Mazztock - GPL v3 (voir LICENSE)
 */
#include "PluginEditor.h"
#include "BinaryData.h"

using Engine = mazz::MazzflatorEngine;
using juce::Rectangle;

namespace {
// --- géométrie du skin (repère 1280 x 628, mêmes cotes que le générateur v9 / layout MPC moins 86) ---
constexpr float ZT = 150, ZB = 570;                 // haut et bas des logements
constexpr float YS = 238, YE = 482;                 // course de la poignée (centre) : haut = 1, bas = 0
const Rectangle<float> haloRect[4] = {{334, 34, 612, 88}, {334, 540, 612, 88}, {334, 122, 88, 418}, {858, 122, 88, 418}};
const char* const haloName[4] = {"top", "bottom", "left", "right"};
const Rectangle<float> doseRect {540, 84, 200, 50};
const Rectangle<float> metersInRect {226, 150, 105, 420}, metersOutRect {1120, 150, 105, 420};
const float meterSplit[2] = {278.5f, 1172.5f};      // séparation gauche / droite de chaque paire
const Rectangle<float> clipRect {258, 114, 40, 40};
const Rectangle<float> readoutRect {948, 17, 112, 42};
constexpr float kDoseLedX0 = 565, kDoseLedStep = 12;

juce::Image loadImage(const juce::String& file) {
    int size = 0;
    auto name = file.replaceCharacter('.', '_').replaceCharacter('-', '_');
    if (auto* data = BinaryData::getNamedResource(name.toRawUTF8(), size))
        return juce::ImageCache::getFromMemory(data, size);
    jassertfalse;
    return {};
}
} // namespace

MazzflatorEditor::MazzflatorEditor(MazzflatorProcessor& p) : AudioProcessorEditor(&p), proc(p) {
    setOpaque(true);
    bg = loadImage("bg.png");
    cap = loadImage("cap.png");
    metersIn = loadImage("meters_in.png");
    metersOut = loadImage("meters_out.png");
    clipOn = loadImage("clip_on.png");
    for (int i = 0; i < 8; i++) {
        for (int k = 0; k < 4; k++) halo[k][i] = loadImage(juce::String("halo_") + haloName[k] + "_" + juce::String(i) + ".png");
        dose[i] = loadImage("dose_" + juce::String(i) + ".png");
    }
    monoFace = juce::Typeface::createSystemTypefaceFor(BinaryData::DejaVuSansMono_ttf, BinaryData::DejaVuSansMono_ttfSize);

    // faders
    const struct { const char* id; float cx; } fd[4] = {{"input", 112}, {"effect", 486}, {"curve", 794}, {"output", 1006}};
    for (int i = 0; i < 4; i++) {
        auto& f = faders[i];
        f.id = fd[i].id;
        f.cx = fd[i].cx;
        f.param = proc.apvts.getParameter(f.id);
        f.defaultValue = f.param->getDefaultValue();
        f.att = std::make_unique<juce::ParameterAttachment>(*f.param, [this, i](float v) {
            faders[i].value = v;
            repaintLogical(wellRect(faders[i]));
        });
        f.att->sendInitialUpdate();
    }

    // boutons
    const struct { const char* id; const char* img; float cx, cy, w, h; bool inv; } td[6] = {
        {"clip", "clip", 640, 220, 86, 86, false}, {"bandsplit", "split", 640, 308, 86, 86, false},
        {"band", "band", 640, 396, 86, 86, false}, {"bypass", "fxon", 640, 500, 86, 86, true},
        {"ag", "ag", 1140, 38, 106, 54, false},     {"aa", "aa", 1218, 38, 56, 54, false}};
    for (int i = 0; i < 6; i++) {
        auto& t = toggles[i];
        t.id = td[i].id;
        t.r = {td[i].cx - td[i].w / 2, td[i].cy - td[i].h / 2, td[i].w, td[i].h};
        t.litWhenOff = td[i].inv;
        t.on = loadImage(juce::String("btn_") + td[i].img + "_on.png");
        t.off = loadImage(juce::String("btn_") + td[i].img + "_off.png");
        t.att = std::make_unique<juce::ParameterAttachment>(*proc.apvts.getParameter(t.id), [this, i](float v) {
            toggles[i].state = v > 0.5f;
            repaintLogical(toggles[i].r);
        });
        t.att->sendInitialUpdate();
    }

    // taille de la fenêtre : proportions fixes, de 50 % à 200 %
    setResizable(true, true);
    setResizeLimits(W / 2, H / 2, W * 2, H * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio((double)W / (double)H);
    double s = proc.apvts.state.getProperty("uiScale", 1.0);
    s = juce::jlimit(0.5, 2.0, s);
    setSize(juce::roundToInt(W * s), juce::roundToInt(H * s));

    startTimerHz(60);
}

MazzflatorEditor::~MazzflatorEditor() { stopTimer(); }

void MazzflatorEditor::resized() {
    proc.apvts.state.setProperty("uiScale", (double)getWidth() / (double)W, nullptr);
}

void MazzflatorEditor::repaintLogical(Rectangle<float> r) {
    repaint((r * scale()).getSmallestIntegerContainer().expanded(2));
}

float MazzflatorEditor::faderY(float v) { return YS + (YE - YS) * (1.0f - v); }
Rectangle<float> MazzflatorEditor::capRect(const Fader& f) const { return {f.cx - 35, faderY(f.value) - 49.5f, 70, 100}; }
Rectangle<float> MazzflatorEditor::wellRect(const Fader& f) const { return {f.cx - 60, ZT - 2, 120, ZB - ZT + 16}; }

// ---------------------------------------------------------------- animation (60 images/s)
void MazzflatorEditor::timerCallback() {
    for (int i = 0; i < 4; i++) {
        int s = proc.meterStep[i].load(std::memory_order_relaxed);
        if (s != meterShown[i]) {
            meterShown[i] = s;
            const auto& r = (i < 2) ? metersInRect : metersOutRect;
            repaintLogical(r);
        }
    }
    // halo + dosage : même mesure (fx_dose 0..35) ; halo adouci entre ses 8 images
    int d = proc.meterStep[4].load(std::memory_order_relaxed);
    float target = (float)d / (float)Engine::DOSE_STEPS * 7.0f;
    float h = haloShown + (target - haloShown) * 0.35f;
    if (std::abs(target - h) < 0.01f) h = target;
    int lit = juce::roundToInt((float)d / (float)Engine::DOSE_STEPS * 13.0f);
    if (std::abs(h - haloShown) > 0.003f || lit != doseShown) {
        haloShown = h;
        doseShown = lit;
        for (auto& r : haloRect) repaintLogical(r);
        repaintLogical(doseRect);
    }
    bool c = proc.meterStep[5].load(std::memory_order_relaxed) != 0;
    if (c != clipShown) { clipShown = c; repaintLogical(clipRect); }

    double db = Engine::outputNormToDb(faders[3].value);
    if (db > -0.05) db = 0.0;
    auto txt = juce::String(db, 1);
    if (txt != readoutShown) { readoutShown = txt; repaintLogical(readoutRect); }
}

// ---------------------------------------------------------------- dessin
void MazzflatorEditor::drawHalo(juce::Graphics& g, int frame, int doseLit) {
    for (int k = 0; k < 4; k++) g.drawImage(halo[k][frame], haloRect[k]);
    if (doseLit > 0) {
        g.saveState();
        float xr = kDoseLedX0 + kDoseLedStep * (float)(doseLit - 1) + 11.0f;
        g.reduceClipRegion(Rectangle<float>(doseRect.getX(), doseRect.getY(), xr - doseRect.getX(), doseRect.getHeight()).toNearestInt());
        g.drawImage(dose[frame], doseRect);
        g.restoreState();
    }
}

void MazzflatorEditor::drawReadout(juce::Graphics& g) {
    juce::Font f(juce::FontOptions(monoFace).withPointHeight(25.0f));
    g.setFont(f);
    const Rectangle<float> area(952, 21, 98, 34);
    g.setColour(juce::Colour(0xffffd000).withAlpha(0.10f));                // lueur (text-shadow 6 px)
    for (float a = 0; a < 6.28f; a += 0.785f)
        for (float rr : {1.5f, 3.0f})
            g.drawText(readoutShown, area.translated(std::cos(a) * rr, std::sin(a) * rr), juce::Justification::centredRight, false);
    g.setColour(juce::Colour(0xffffe23a));
    g.drawText(readoutShown, area, juce::Justification::centredRight, false);
}

void MazzflatorEditor::paint(juce::Graphics& g) {
    g.addTransform(juce::AffineTransform::scale(scale()));
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(bg, Rectangle<float>(0, 0, (float)W, (float)H));

    // halo (cadre central) + LED de dosage
    int hi = juce::jlimit(0, 7, (int)std::floor(haloShown));
    float frac = haloShown - (float)hi;
    drawHalo(g, hi, juce::jmax(0, doseShown));
    if (hi < 7 && frac > 0.01f) {
        g.saveState();
        g.setOpacity(frac);
        drawHalo(g, hi + 1, juce::jmax(0, doseShown));
        g.restoreState();
    }

    // vu-mètres : segments allumés de bas en haut (segment i : y = 544 - 14 i)
    for (int ch = 0; ch < 4; ch++) {
        int n = meterShown[ch];
        if (n <= 0) continue;
        const auto& r = (ch < 2) ? metersInRect : metersOutRect;
        float mid = meterSplit[ch / 2];
        float x0 = (ch % 2 == 0) ? r.getX() : mid, x1 = (ch % 2 == 0) ? mid : r.getRight();
        float top = 544.0f - 14.0f * (float)(n - 1) - 2.0f;
        g.saveState();
        g.reduceClipRegion(Rectangle<float>(x0, top, x1 - x0, r.getBottom() - top).toNearestInt());
        g.drawImage(ch < 2 ? metersIn : metersOut, r);
        g.restoreState();
    }

    if (clipShown) g.drawImage(clipOn, clipRect);

    for (auto& t : toggles) {
        bool lit = t.litWhenOff ? !t.state : t.state;
        g.drawImage(lit ? t.on : t.off, t.r);
    }

    for (auto& f : faders)
        g.drawImage(cap, Rectangle<float>(f.cx - 55, faderY(f.value) - 69.5f, 110, 150));

    drawReadout(g);
}

// ---------------------------------------------------------------- souris
// Fader : on attrape la poignée et on la glisse ; clic ailleurs dans le logement = la poignée y saute.
// Maj = réglage fin ; Alt-clic ou double-clic = valeur par défaut ; molette = petits pas. Clic droit = taille.
void MazzflatorEditor::setFaderFromDrag(Fader& f, float y, bool fine) {
    if (fine != dragFine) { dragFine = fine; dragStartY = y; dragStartValue = f.value; }
    float v = dragStartValue - (y - dragStartY) / (YE - YS) * (fine ? 0.2f : 1.0f);
    f.att->setValueAsPartOfGesture(juce::jlimit(0.0f, 1.0f, v));
}

void MazzflatorEditor::mouseDown(const juce::MouseEvent& e) {
    auto p = toLogical(e.position);
    if (e.mods.isPopupMenu()) { showSizeMenu(); return; }
    for (auto& t : toggles)
        if (t.r.contains(p)) { t.att->setValueAsCompleteGesture(t.state ? 0.0f : 1.0f); return; }
    for (auto& f : faders) {
        if (!wellRect(f).reduced(4, 2).contains(p)) continue;
        dragging = &f;
        f.att->beginGesture();
        if (e.mods.isAltDown()) {
            f.att->setValueAsPartOfGesture(f.defaultValue);
        } else if (!capRect(f).contains(p)) {
            f.att->setValueAsPartOfGesture(juce::jlimit(0.0f, 1.0f, 1.0f - (p.y - YS) / (YE - YS)));
        }
        dragFine = e.mods.isShiftDown();
        dragStartY = p.y;
        dragStartValue = f.value;
        return;
    }
}

void MazzflatorEditor::mouseDrag(const juce::MouseEvent& e) {
    if (dragging) setFaderFromDrag(*dragging, toLogical(e.position).y, e.mods.isShiftDown());
}

void MazzflatorEditor::mouseUp(const juce::MouseEvent&) {
    if (dragging) { dragging->att->endGesture(); dragging = nullptr; }
}

void MazzflatorEditor::mouseDoubleClick(const juce::MouseEvent& e) {
    auto p = toLogical(e.position);
    for (auto& f : faders)
        if (wellRect(f).reduced(4, 2).contains(p)) { f.att->setValueAsCompleteGesture(f.defaultValue); return; }
}

void MazzflatorEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    auto p = toLogical(e.position);
    for (auto& f : faders)
        if (wellRect(f).reduced(4, 2).contains(p)) {
            float step = (w.isReversed ? -w.deltaY : w.deltaY) * (e.mods.isShiftDown() ? 0.02f : 0.1f);
            f.att->setValueAsCompleteGesture(juce::jlimit(0.0f, 1.0f, f.value + step));
            return;
        }
}

void MazzflatorEditor::showSizeMenu() {
    juce::PopupMenu m;
    m.addSectionHeader("Taille de la fenetre");
    const int sizes[] = {50, 75, 100, 125, 150, 200};
    int cur = juce::roundToInt(scale() * 100.0f);
    for (int s : sizes) m.addItem(s, juce::String(s) + " %", true, s == cur);
    juce::Component::SafePointer<MazzflatorEditor> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe](int r) {
        if (safe != nullptr && r > 0) safe->setSize(juce::roundToInt(W * r / 100.0), juce::roundToInt(H * r / 100.0));
    });
}
