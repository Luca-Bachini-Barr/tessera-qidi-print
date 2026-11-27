#include "qidi_print.h"
#include "esphome/core/hal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <strings.h>

namespace esphome::qidi_print {

using tessera::CardColor;
using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

// Icons of Tessera's fixed set (screen_manager/app/tile_icons.py).
static constexpr uint32_t ICON_PRINTER = 0xF042B;    // printer-3d
static constexpr uint32_t ICON_PAUSE = 0xF03E4;      // pause
static constexpr uint32_t ICON_DONE = 0xF05E0;       // check-circle
static constexpr uint32_t ICON_CANCELLED = 0xF015A;  // close-circle-outline
static constexpr uint32_t ICON_ERROR = 0xF0028;      // alert-circle
static constexpr uint32_t ICON_OFF = 0xF06A6;        // power-plug-off
static constexpr uint32_t ICON_LAYERS = 0xF0996;     // progress-clock
static constexpr uint32_t ICON_TIMER = 0xF051B;      // timer-outline
static constexpr uint32_t ICON_NOZZLE = 0xF0238;     // fire
static constexpr uint32_t ICON_BED = 0xF0438;        // radiator
static constexpr uint32_t ICON_CHAMBER = 0xF050F;    // thermometer
static constexpr uint32_t ICON_SPEED = 0xF04C5;      // speedometer
static constexpr uint32_t ICON_PLAY = 0xF040A;       // play
static constexpr uint32_t ICON_STOP = 0xF04DB;       // stop
static constexpr uint32_t ICON_CAMERA = 0xF07AE;     // cctv
static constexpr uint32_t ICON_CHANGE = 0xF18F2;     // refresh-auto
static constexpr uint32_t ICON_WARNING = 0xF002A;    // alert-outline
static constexpr uint32_t ICON_DRYING = 0xF058E;     // water-percent
static constexpr uint32_t STOP_CONFIRM_MS = 5000;    // Stop cancels only on a second tap within this

static void copy(char *to, size_t size, const char *from) {
  strncpy(to, from ? from : "", size - 1);
  to[size - 1] = '\0';
}

static void show(lv_obj_t *part, bool on) {
  if (!part) return;
  if (on) lv_obj_remove_flag(part, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(part, LV_OBJ_FLAG_HIDDEN);
}

// ---- What the sensors say ----

enum class Phase : uint8_t { CHOOSE, TOO_LARGE, OFF, STANDBY, PRINTING, PAUSED, COMPLETE, CANCELLED, ERROR };

struct Slot {
  char type[24];
  uint8_t r, g, b;
  bool shown, has_color, empty, active;
};
// The four slots of a QIDI Box, one Moonraker Box Slot N Filament sensor each. More slots would not fit the tile's
// 2.6 KB of data.
static constexpr int SLOTS = 4;
static constexpr int SLOTS_PER_BOX = 4;
static const char *const SLOT_KEYS[SLOTS] = {"slot1", "slot2", "slot3", "slot4"};

// An entity the person chose that Home Assistant has now: there, and not unavailable. A button's state is the
// moment it was last pressed, "unknown" before that, so unknown counts as there.
static bool present(JsonObjectConst e) {
  if (e.isNull()) return false;
  const char *s = e["state"] | "";
  return *s && strcmp(s, "unavailable") != 0;
}

// A sensor's number, false while it has none ("unavailable", "unknown").
static bool number(JsonObjectConst e, float &out) {
  const char *s = e["state"] | "";
  if (!*s) return false;
  char *end = nullptr;
  const float v = strtof(s, &end);
  if (end == s || !std::isfinite(v)) return false;
  out = v;
  return true;
}

static bool known(const char *state) {
  return *state && strcmp(state, "unavailable") != 0 && strcmp(state, "unknown") != 0;
}

// Seconds in one of the sensor's units: Moonraker's time left is in hours, its duration in minutes.
static float seconds_per(JsonObjectConst e, float fallback) {
  const char *u = e["attributes"]["unit_of_measurement"] | "";
  if (!strcmp(u, "h")) return 3600;
  if (!strcmp(u, "min")) return 60;
  if (!strcmp(u, "s")) return 1;
  if (!strcmp(u, "d")) return 86400;
  if (!strcmp(u, "ms")) return 0.001f;
  return fallback;
}

static int hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// "#RRGGBB" (the QIDI Box integration's colour of a spool).
static bool parse_color(const char *s, uint8_t &r, uint8_t &g, uint8_t &b) {
  if (!s || s[0] != '#' || strlen(s) < 7) return false;
  int v[6];
  for (int i = 0; i < 6; ++i)
    if ((v[i] = hex(s[i + 1])) < 0) return false;
  r = v[0] * 16 + v[1];
  g = v[2] * 16 + v[3];
  b = v[4] * 16 + v[5];
  return true;
}

struct Print {
  Phase phase = Phase::CHOOSE;
  char file[64] = "";
  float progress = NAN;                  // 0 to 100
  float left = NAN, elapsed = NAN;       // seconds
  uint32_t left_at = 0, elapsed_at = 0;  // when Home Assistant heard them
  int layer = -1, layers = -1;
  float filament = NAN;                  // metres
  float nozzle = NAN, bed = NAN, chamber = NAN, speed = NAN;
  Slot slot[SLOTS]{};
  int slots = 0;  // one more than the last slot shown, 0 without a Box
  // The keys' entities, there now: a printer without a camera, or a key's button gone, shows no key.
  bool has_pause = false, has_resume = false, has_cancel = false, has_camera = false;
  // The Box's own sensors: the step of a filament change ("" when none runs), the slot it loads (0-based, -1 when
  // not known), how many warnings the Box reports, and whether it dries its filament.
  char change[16] = "";
  int change_slot = -1;
  // Kept from one reading to the next while a change runs: whether one of its steps (cutting, unloading, loading,
  // eject) was seen, so "changing" after them is the purge and before them the start; and the slot it loads, which
  // the sensor drops when the task ends, before the purge.
  bool change_stepped = false;
  int change_target = -1;
  int warnings = 0;
  bool drying = false;

  bool running() const { return phase == Phase::PRINTING || phase == Phase::PAUSED; }
  // A layer worth showing: the file says how many there are, and the current one lies within them. Without a total
  // Moonraker guesses the layer from the nozzle's height, which jumps while the printer probes, wipes and parks.
  bool layer_known() const { return layers > 1 && layer >= 0 && layer <= layers; }
  bool online() const { return phase >= Phase::STANDBY; }

  int active() const {
    for (int i = 0; i < slots; ++i)
      if (slot[i].shown && slot[i].active && !slot[i].empty) return i;
    return -1;
  }
  bool any_slot() const { return slots > 0; }

  // The time left now: Moonraker's number counted down from when it came, while the print goes on.
  float left_now(uint32_t epoch) const {
    if (!std::isfinite(left)) return NAN;
    if (phase != Phase::PRINTING || !left_at || !epoch || epoch <= left_at) return left;
    return std::max(0.0f, left - static_cast<float>(epoch - left_at));
  }
  float elapsed_now(uint32_t epoch) const {
    if (!std::isfinite(elapsed)) return NAN;
    if (phase != Phase::PRINTING || !elapsed_at || !epoch || epoch <= elapsed_at) return elapsed;
    return elapsed + static_cast<float>(std::min<uint32_t>(epoch - elapsed_at, 600));
  }

  void read(JsonObjectConst data) {
    const bool stepped = change_stepped;
    const int target_before = change_target;
    *this = Print{};
    const char *wait = data["wait"] | "";
    if (!strcmp(wait, "too_large")) {
      phase = Phase::TOO_LARGE;
      return;
    }
    JsonObjectConst es = data["entities"].as<JsonObjectConst>();
    JsonObjectConst st = es["print_state"].as<JsonObjectConst>();
    if (st.isNull()) return;  // CHOOSE
    const char *s = st["state"] | "";
    if (!strcmp(s, "printing")) phase = Phase::PRINTING;
    else if (!strcmp(s, "paused")) phase = Phase::PAUSED;
    else if (!strcmp(s, "complete")) phase = Phase::COMPLETE;
    else if (!strcmp(s, "cancelled")) phase = Phase::CANCELLED;
    else if (!strcmp(s, "error")) phase = Phase::ERROR;
    else if (known(s)) phase = Phase::STANDBY;
    else phase = Phase::OFF;

    // The file without its folder and its .gcode.
    const char *f = es["filename"]["state"] | "";
    if (known(f)) {
      const char *slash = strrchr(f, '/');
      copy(file, sizeof(file), slash ? slash + 1 : f);
      char *dot = strrchr(file, '.');
      if (dot && (!strcasecmp(dot, ".gcode") || !strcasecmp(dot, ".gco") || !strcasecmp(dot, ".g"))) *dot = '\0';
    }

    float v;
    if (number(es["progress"].as<JsonObjectConst>(), v)) progress = std::max(0.0f, std::min(100.0f, v));
    JsonObjectConst e = es["time_left"].as<JsonObjectConst>();
    if (number(e, v)) {
      left = std::max(0.0f, v * seconds_per(e, 3600));
      left_at = e["updated"] | 0u;
    }
    e = es["duration"].as<JsonObjectConst>();
    if (number(e, v)) {
      elapsed = std::max(0.0f, v * seconds_per(e, 60));
      elapsed_at = e["updated"] | 0u;
    }
    if (number(es["current_layer"].as<JsonObjectConst>(), v)) layer = static_cast<int>(lroundf(v));
    if (number(es["total_layer"].as<JsonObjectConst>(), v)) layers = static_cast<int>(lroundf(v));
    e = es["filament_used"].as<JsonObjectConst>();
    if (number(e, v)) {
      const char *u = e["attributes"]["unit_of_measurement"] | "m";
      filament = !strcmp(u, "mm") ? v / 1000 : !strcmp(u, "cm") ? v / 100 : v;
    }
    number(es["nozzle"].as<JsonObjectConst>(), nozzle);
    number(es["bed"].as<JsonObjectConst>(), bed);
    number(es["chamber"].as<JsonObjectConst>(), chamber);
    number(es["speed"].as<JsonObjectConst>(), speed);

    // The Box: a sensor per slot. One not chosen, or unavailable (the Box off), is not shown.
    for (int i = 0; i < SLOTS; ++i) {
      JsonObjectConst se = es[SLOT_KEYS[i]].as<JsonObjectConst>();
      const char *name = se["state"] | "";
      if (!known(name)) continue;
      Slot &sl = slot[i];
      sl.shown = true;
      sl.empty = !strcmp(name, "Empty");
      JsonObjectConst a = se["attributes"].as<JsonObjectConst>();
      sl.active = a["active"] | false;
      const char *type = a["filament_type"] | "";
      copy(sl.type, sizeof(sl.type), *type ? type : name);
      sl.has_color = !sl.empty && parse_color(a["color"] | "", sl.r, sl.g, sl.b);
      slots = i + 1;
    }

    // A filament change: Moonraker's Box Filament Change sensor, idle without one.
    e = es["change"].as<JsonObjectConst>();
    const char *c = e["state"] | "";
    if (known(c) && strcmp(c, "idle") != 0) {
      copy(change, sizeof(change), c);
      const int target = e["attributes"]["target_slot"] | 0;
      change_target = target >= 1 && target <= SLOTS ? target - 1 : target_before;
      change_slot = change_target;
      change_stepped = stepped || (strcmp(c, "changing") != 0 && strcmp(c, "starting") != 0 && strcmp(c, "paused") != 0);
    }
    if (number(es["warnings"].as<JsonObjectConst>(), v) && v > 0) warnings = static_cast<int>(lroundf(v));
    drying = !strcmp(es["drying"]["state"] | "", "on");

    has_pause = present(es["pause"].as<JsonObjectConst>());
    has_resume = present(es["resume"].as<JsonObjectConst>());
    has_cancel = present(es["cancel"].as<JsonObjectConst>());
    has_camera = present(es["camera"].as<JsonObjectConst>());
  }
};

// The entities the keys use, from the tile's options (the card gets them from the tile that opened it).
struct Controls {
  char pause[96] = "", resume[96] = "", cancel[96] = "", camera[96] = "";

  void read(JsonObjectConst options) {
    copy(pause, sizeof(pause), options["pause"] | "");
    copy(resume, sizeof(resume), options["resume"] | "");
    copy(cancel, sizeof(cancel), options["cancel"] | "");
    copy(camera, sizeof(camera), options["camera"] | "");
  }
};

// ---- Words ----

static std::string duration_text(const tessera::Plugin *p, float s) {
  if (!std::isfinite(s) || s < 0) return "";
  if (s < 60) return p->text("under_minute");
  const long mins = lroundf(s / 60);
  if (mins < 60) return tessera::fill(p->text("minutes"), "m", std::to_string(mins));
  const std::string t = tessera::fill(p->text("hours_minutes"), "h", std::to_string(mins / 60));
  return tessera::fill(t.c_str(), "m", std::to_string(mins % 60));
}

static std::string degrees(float t) {
  char b[16];
  snprintf(b, sizeof(b), "%.0f°", t);
  return b;
}

static std::string metres(const tessera::Plugin *p, float m) {
  char b[16];
  snprintf(b, sizeof(b), m < 10 ? "%.1f" : "%.0f", m);
  return tessera::fill(p->text("metres"), "m", b);
}

// ---- One line of details: an icon, a colour (the spool's) and its words ----

struct Row {
  lv_obj_t *icon{}, *swatch{}, *words{};
  int x = 0, y = 0, w = 0, h = 0;

  void make(lv_obj_t *parent, int x0, int y0, int width, int height) {
    x = x0, y = y0, w = width, h = height;
    const int ih = ui::line_height(Font::ICON_SMALL), lh = ui::line_height(Font::BODY_LARGE);
    const int iw = ih + ui::px(2);
    icon = ui::label(parent, Font::ICON_SMALL, theme::SLATE);
    lv_obj_set_size(icon, iw, ih);
    lv_obj_set_pos(icon, x, y + (h - ih) / 2);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    const int sw = std::max(ui::px(12), ih * 3 / 4);
    swatch = ui::block(parent, theme::TRACK);
    lv_obj_set_size(swatch, sw, sw);
    lv_obj_set_pos(swatch, x + (iw - sw) / 2, y + (h - sw) / 2);
    lv_obj_set_style_radius(swatch, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(swatch, std::max(1, ui::px(1)), 0);
    words = ui::label(parent, Font::BODY_LARGE, theme::INK);
    const int indent = iw + ui::px(8);
    lv_obj_set_size(words, std::max(1, w - indent), lh);
    lv_obj_set_pos(words, x + indent, y + (h - lh) / 2);
    hide();
  }

  void paint(uint32_t glyph, const std::string &text, const Slot *spool = nullptr) {
    show(icon, spool == nullptr);
    show(swatch, spool != nullptr);
    show(words, true);
    if (spool) {
      lv_obj_set_style_border_color(swatch, ui::color(theme::LINE), 0);
      lv_obj_set_style_bg_color(swatch, spool->has_color ? lv_color_make(spool->r, spool->g, spool->b)
                                                         : ui::color(theme::TRACK), 0);
    } else {
      ui::set_color(icon, theme::SLATE);
      ui::set_text(icon, ui::icon(glyph));
    }
    ui::set_color(words, theme::INK);
    ui::set_text(words, text);
  }

  void hide() {
    show(icon, false);
    show(swatch, false);
    show(words, false);
  }
};

// What a row may say, in the order they get the room.
enum class Detail : uint8_t { WARNING, CHANGE, LAYER, ELAPSED, FILAMENT, DRYING, NOZZLE, BED, CHAMBER, SPEED };
static constexpr int DETAILS = 10;

// A slot as the printer's screen names it: 1A to 1D for the first Box.
static std::string slot_name(int i) {
  char b[4];
  snprintf(b, sizeof(b), "%d%c", i / SLOTS_PER_BOX + 1, 'A' + i % SLOTS_PER_BOX);
  return b;
}

// ---- The face: the same parts on the tile and the card, laid out by the room ----

class Face {
 public:
  explicit Face(QidiPrint *plugin) : plugin_(plugin) {}

  Print print;

  void build(lv_obj_t *parent, int w, int h, bool card, const std::string &entity) {
    card_ = card;
    w_ = w;
    const int gap = ui::px(8);
    compact_ = !card && h < ui::mm(32);

    // The thumbnail: on the left, as high as the room, where the words still have room beside it.
    int side = 0;
    const bool camera = entity.rfind("camera.", 0) == 0;
    if (camera && ui::pictures()) {
      if (card) side = std::min(h * 3 / 4, w * 2 / 5);
      else if (w >= h * 3 / 2) side = h;
    }
    if (side > 0) {
      pic_ = ui::picture(parent);
      lv_obj_set_size(pic_, side, side);
      lv_obj_set_pos(pic_, 0, 0);
      ui::set_picture(pic_, "camera:" + entity, tessera::Fit::CONTAIN);
      // A printer over the place while the thumbnail is not there (no print, the printer off).
      pic_icon_ = ui::label(parent, Font::ICON, theme::MUTED);
      const int ih = ui::line_height(Font::ICON);
      lv_obj_set_size(pic_icon_, side, ih);
      lv_obj_set_pos(pic_icon_, 0, (side - ih) / 2);
      lv_obj_set_style_text_align(pic_icon_, LV_TEXT_ALIGN_CENTER, 0);
      ui::set_text(pic_icon_, ui::icon(ICON_PRINTER));
    }
    const int x0 = side > 0 ? side + gap * 2 : 0;
    const int cw = std::max(1, w - x0);
    x0_ = x0;
    cw_ = cw;

    // The file, and the state under it.
    int y = 0;
    const Font title_font = card ? Font::HEADLINE : Font::TITLE;
    const int th = ui::line_height(title_font);
    title_ = ui::label(parent, title_font, theme::INK);
    lv_obj_set_size(title_, cw, th);
    lv_obj_set_pos(title_, x0, y);
    y += th + ui::px(2);

    const int bh = ui::line_height(Font::BODY_LARGE), ih = ui::line_height(Font::ICON_SMALL);
    const int sh = std::max(bh, ih);
    state_icon_ = ui::label(parent, Font::ICON_SMALL, theme::INK);
    lv_obj_set_size(state_icon_, ih + ui::px(2), ih);
    lv_obj_set_pos(state_icon_, x0, y + (sh - ih) / 2);
    state_ = ui::label(parent, Font::BODY_LARGE, theme::INK);
    lv_obj_set_size(state_, std::max(1, cw - ih - ui::px(8)), bh);
    lv_obj_set_pos(state_, x0 + ih + ui::px(8), y + (sh - bh) / 2);
    y += sh + gap;

    bar_h_ = std::max(ui::px(6), ui::mm(compact_ ? 1 : 2));
    if (compact_) {
      // Three lines: the file, the state with the percent and the time left, the bar.
      bar_y_ = std::max(y, h - bar_h_);
      make_bar(parent);
      return;
    }

    // The percent large, and the time left and the hour it is done beside it.
    const int vh = ui::line_height(Font::VALUE);
    pct_ = ui::label(parent, Font::VALUE, theme::INK);
    pct_w_ = ui::text_width("100%", Font::VALUE) + ui::px(6);
    lv_obj_set_size(pct_, pct_w_, vh);
    lv_obj_set_pos(pct_, x0, y);
    const int rx = x0 + pct_w_ + gap, rw = std::max(1, cw - pct_w_ - gap);
    const int lh = ui::line_height(Font::BODY_LARGE), eh = ui::line_height(Font::BODY);
    const int top = y + std::max(0, (vh - lh - eh - ui::px(2)) / 2);
    left_ = ui::label(parent, Font::BODY_LARGE, theme::INK);
    lv_obj_set_size(left_, rw, lh);
    lv_obj_set_pos(left_, rx, top);
    eta_ = ui::label(parent, Font::BODY, theme::SLATE);
    lv_obj_set_size(eta_, rw, eh);
    lv_obj_set_pos(eta_, rx, top + lh + ui::px(2));
    y += vh + ui::px(4);

    bar_y_ = y;
    make_bar(parent);
    y += bar_h_ + gap * 3 / 2;

    // The details, in two columns where they are wide enough, as many as fit; on the card the Box's slots at the
    // bottom of the room.
    int bottom = h;
    // One row of four slots for each Box the printer has (the card opens with the tile's readings, so it knows);
    // none without a Box.
    const int boxes = rows_for(print.slots);
    box_rows_ = card ? boxes : 0;
    if (card && boxes > 0) {
      const int box_gap = ui::px(6);
      const int row = std::max(boxes > 2 ? ui::mm(7) : ui::mm(9), ui::line_height(Font::BODY_LARGE) + ui::px(10));
      box_h_ = boxes * row + (boxes - 1) * box_gap;
      bottom = h - box_h_ - gap;
      make_box(parent, x0, h - box_h_, cw, row, box_gap, boxes * SLOTS_PER_BOX);
    }
    // The keys (Pause or Resume, Stop, Camera) above that, on the card and on a tall tile.
    const int key_h = std::max(ui::mm(11), ui::line_height(Font::BODY_LARGE) + ui::px(20));
    if (card || bottom - y >= key_h * 3) {
      bottom -= key_h;
      make_keys(parent, bottom, key_h);
      bottom -= gap;
    }
    const int row_h = std::max(ui::line_height(Font::BODY_LARGE), ih) + ui::px(card ? 12 : 6);
    const int cols = cw >= ui::mm(card ? 90 : 75) ? 2 : 1;
    const int col_w = (cw - (cols - 1) * gap) / cols;
    rows_ = 0;
    for (int r = 0; rows_ < DETAILS && y + (r + 1) * row_h <= bottom; ++r)
      for (int c = 0; c < cols && rows_ < DETAILS; ++c) row_[rows_++].make(parent, x0 + c * (col_w + gap), y + r * row_h, col_w, row_h);
  }

  bool shows_picture() const { return pic_ != nullptr; }
  // The Box rows the card was laid out for, and how many the readings need now.
  int box_rows() const { return box_rows_; }
  static int rows_for(int slots) { return (std::min(slots, SLOTS) + SLOTS_PER_BOX - 1) / SLOTS_PER_BOX; }

  Controls ctl;

  // A tap at x, y: true when it was on one of the keys (and the key did its work).
  bool press(int x, int y, int tile) {
    const int k = key_at(x, y);
    if (k < 0) return false;
    const Print &p = print;
    // What the key does is checked again against the readings now: one that changed since the last paint (the
    // button gone, the print over) does nothing but paint the keys again.
    if (!key_ok(static_cast<KeyId>(k))) {
      paint(tessera::epoch());
      return true;
    }
    switch (static_cast<KeyId>(k)) {
      case KeyId::PAUSE:
        tessera::action("button.press", p.phase == Phase::PAUSED ? ctl.resume : ctl.pause);
        break;
      case KeyId::STOP:
        // The second tap cancels only the print the first tap was for: the same file, still running.
        if (stop_armed_ && millis() - stop_armed_at_ < STOP_CONFIRM_MS && p.running() &&
            strcmp(stop_file_, p.file) == 0) {
          tessera::action("button.press", ctl.cancel);
          stop_armed_ = false;
        } else if (p.running()) {
          stop_armed_ = true;
          stop_armed_at_ = millis();
          copy(stop_file_, sizeof(stop_file_), p.file);
        }
        break;
      case KeyId::CAMERA:
        tessera::open_card(plugin_->plugin_id(), "camera", ctl.camera, tile, plugin_->text("camera"));
        return true;
    }
    paint(tessera::epoch());
    return true;
  }

  void on_picture(lv_obj_t *place, tessera::PictureState state) {
    if (place == pic_) {
      pic_shown_ = state == tessera::PictureState::SHOWN;
      show(pic_icon_, !pic_shown_);
    }
  }

  void paint(uint32_t epoch) {
    if (!title_) return;
    const Print &p = print;
    if (!card_) {
      ui::set_card_color(p.phase == Phase::ERROR    ? CardColor::RED
                         : p.phase == Phase::PAUSED ? CardColor::YELLOW
                                                    : CardColor::NONE);
    }

    // The state: an icon in its tone and a word.
    uint32_t glyph = ICON_PRINTER;
    const char *key = "standby";
    Tone tone = Tone::NORMAL;
    switch (p.phase) {
      case Phase::CHOOSE: glyph = ICON_ERROR; key = "choose"; break;
      case Phase::TOO_LARGE: glyph = ICON_ERROR; key = "too_much"; tone = Tone::ALERT; break;
      case Phase::OFF: glyph = ICON_OFF; key = "off"; break;
      case Phase::STANDBY: break;
      case Phase::PRINTING: key = "printing"; tone = Tone::ACCENT; break;
      case Phase::PAUSED: glyph = ICON_PAUSE; key = "paused"; tone = Tone::BUSY; break;
      case Phase::COMPLETE: glyph = ICON_DONE; key = "complete"; tone = Tone::GOOD; break;
      case Phase::CANCELLED: glyph = ICON_CANCELLED; key = "cancelled"; break;
      case Phase::ERROR: glyph = ICON_ERROR; key = "error"; tone = Tone::ALERT; break;
    }
    ui::set_text(state_icon_, ui::icon(glyph));
    ui::set_tone(state_icon_, tone, p.phase == Phase::OFF ? theme::MUTED : theme::INK);

    const float left = p.left_now(epoch);
    const bool has_pct = std::isfinite(p.progress);
    const bool finished = p.phase == Phase::COMPLETE;
    std::string pct;
    if (finished) pct = "100%";
    else if (p.running() && has_pct) pct = std::to_string(static_cast<int>(p.progress)) + "%";

    // The file, or what the tile is while there is none.
    const bool has_file = p.file[0] && p.online();
    ui::set_text(title_, has_file ? std::string(p.file)
                                  : std::string(plugin_->text(p.online() ? "no_file" : "printer")));
    ui::set_color(title_, has_file && !p.running() && !finished ? theme::SLATE : theme::INK);

    // The state's line; on a compact tile also the percent, the time left and the layer.
    std::string line = plugin_->text(key);
    if (compact_ && p.running()) {
      if (!pct.empty()) line = pct;
      if (std::isfinite(left)) line += " · " + left_text(left);
      if (p.layer_known() && w_ >= ui::mm(50)) line += " · " + layer_short(p);
    } else if (compact_ && finished && std::isfinite(p.elapsed)) {
      line += " · " + duration_text(plugin_, p.elapsed);
    }
    ui::set_text(state_, line);
    ui::set_tone(state_, p.phase == Phase::PAUSED || p.phase == Phase::ERROR ? tone : Tone::NORMAL,
                 p.phase == Phase::OFF ? theme::MUTED : theme::INK);

    // The bar.
    const bool bar = p.running() || finished;
    show(track_, bar);
    const float fraction = finished ? 1.0f : has_pct ? p.progress / 100.0f : 0.0f;
    const int fw = static_cast<int>(cw_ * fraction);
    show(fill_, bar && fw > 0);
    if (fw > 0) lv_obj_set_width(fill_, std::max(bar_h_, fw));
    ui::set_fill(fill_, finished ? Tone::GOOD : p.phase == Phase::PAUSED ? Tone::BUSY : Tone::ACCENT, theme::ACCENT);
    if (compact_) return;

    // The percent, the time left and the hour it is done.
    show(pct_, !pct.empty());
    ui::set_text(pct_, pct);
    std::string first, second;
    if (p.running() && std::isfinite(left)) {
      first = left < 60 ? plugin_->text("almost") : tessera::fill(plugin_->text("left"), "time", left_text(left));
      if (epoch && left >= 60) second = done_text(epoch + static_cast<uint32_t>(left));
    } else if (finished && std::isfinite(p.elapsed)) {
      first = tessera::fill(plugin_->text("took"), "time", duration_text(plugin_, p.elapsed));
    }
    // Without a percent the words take its place.
    const int lx = pct.empty() ? x0_ : x0_ + pct_w_ + ui::px(8);
    lv_obj_set_x(left_, lx);
    lv_obj_set_x(eta_, lx);
    lv_obj_set_width(left_, std::max(1, x0_ + cw_ - lx));
    lv_obj_set_width(eta_, std::max(1, x0_ + cw_ - lx));
    show(left_, !first.empty());
    show(eta_, !second.empty());
    ui::set_text(left_, first);
    ui::set_text(eta_, second);
    ui::set_color(eta_, theme::SLATE);

    // The details that have something to say, in order, in the rows there are.
    int r = 0;
    for (int d = 0; d < DETAILS && r < rows_; ++d)
      if (paint_detail(static_cast<Detail>(d), row_[r], epoch)) ++r;
    for (; r < rows_; ++r) row_[r].hide();

    if (card_) paint_box();
    paint_keys();
  }

 private:
  enum class KeyId : uint8_t { PAUSE, STOP, CAMERA };
  static constexpr int KEYS = 3;
  struct Key {
    lv_obj_t *block{}, *icon{}, *words{};
    int x = 0, w = 0;
    bool shown = false;
  };

  void make_keys(lv_obj_t *parent, int y, int h) {
    keys_y_ = y;
    keys_h_ = h;
    for (Key &k : key_) {
      k.block = ui::block(parent, theme::RAISED);
      lv_obj_set_size(k.block, 1, h);
      lv_obj_set_pos(k.block, x0_, y);
      k.icon = ui::label(k.block, Font::ICON_SMALL, theme::INK);
      lv_obj_set_size(k.icon, ui::line_height(Font::ICON_SMALL) + ui::px(2), ui::line_height(Font::ICON_SMALL));
      k.words = ui::label(k.block, Font::BODY_LARGE, theme::INK);
      lv_obj_set_size(k.words, 1, ui::line_height(Font::BODY_LARGE));
      show(k.block, false);
    }
    has_keys_ = true;
  }

  void paint_keys() {
    if (!has_keys_) return;
    const Print &p = print;
    if (stop_armed_ && millis() - stop_armed_at_ >= STOP_CONFIRM_MS) stop_armed_ = false;
    if (!p.running() || strcmp(stop_file_, p.file) != 0) stop_armed_ = false;
    const bool paused = p.phase == Phase::PAUSED;
    for (int i = 0; i < KEYS; ++i) key_[i].shown = key_ok(static_cast<KeyId>(i));
    // The shown keys share the row.
    int n = 0;
    for (const Key &k : key_) n += k.shown;
    const int gap = ui::px(8);
    const int kw = n ? (cw_ - (n - 1) * gap) / n : 0;
    int at = x0_;
    for (int i = 0; i < KEYS; ++i) {
      Key &k = key_[i];
      show(k.block, k.shown);
      if (!k.shown) continue;
      k.x = at;
      k.w = kw;
      at += kw + gap;
      uint32_t glyph = ICON_CAMERA;
      std::string words = plugin_->text("camera");
      bool alert = false;
      if (i == 0) {
        glyph = paused ? ICON_PLAY : ICON_PAUSE;
        words = plugin_->text(paused ? "resume" : "pause");
      } else if (i == 1) {
        glyph = ICON_STOP;
        alert = stop_armed_;
        words = plugin_->text(stop_armed_ ? "stop_confirm" : "stop");
      }
      lv_obj_set_pos(k.block, k.x, keys_y_);
      lv_obj_set_width(k.block, kw);
      ui::set_fill(k.block, alert ? Tone::ALERT : Tone::NORMAL, theme::RAISED);
      // The icon and the words together in the middle of the key.
      const int iw = ui::line_height(Font::ICON_SMALL) + ui::px(2), pad = ui::px(10);
      const int ww = std::min(ui::text_width(words, Font::BODY_LARGE) + ui::px(4), std::max(1, kw - iw - 3 * pad));
      const int left = std::max(pad, (kw - iw - pad - ww) / 2);
      lv_obj_set_pos(k.icon, left, (keys_h_ - ui::line_height(Font::ICON_SMALL)) / 2);
      lv_obj_set_pos(k.words, left + iw + pad, (keys_h_ - ui::line_height(Font::BODY_LARGE)) / 2);
      lv_obj_set_width(k.words, ww);
      ui::set_text(k.icon, ui::icon(glyph));
      ui::set_text(k.words, words);
      const bool stop = i == 1;
      ui::set_tone(k.icon, alert ? Tone::NORMAL : stop ? Tone::ALERT : Tone::NORMAL, alert ? theme::ON_ACCENT : theme::INK);
      ui::set_color(k.words, alert ? theme::ON_ACCENT : theme::INK);
    }
  }

  // Whether a key has something to do now: its entity chosen and there, and for Pause, Resume and Stop a print.
  bool key_ok(KeyId k) const {
    const Print &p = print;
    switch (k) {
      case KeyId::PAUSE:
        return p.running() && (p.phase == Phase::PAUSED ? *ctl.resume && p.has_resume : *ctl.pause && p.has_pause);
      case KeyId::STOP:
        return p.running() && *ctl.cancel && p.has_cancel;
      case KeyId::CAMERA:
        return *ctl.camera && p.has_camera;
    }
    return false;
  }

  int key_at(int x, int y) const {
    if (!has_keys_ || y < keys_y_ - ui::px(4) || y > keys_y_ + keys_h_ + ui::px(4)) return -1;
    for (int i = 0; i < KEYS; ++i)
      if (key_[i].shown && x >= key_[i].x - ui::px(4) && x <= key_[i].x + key_[i].w + ui::px(4)) return i;
    return -1;
  }

  void make_bar(lv_obj_t *parent) {
    track_ = ui::block(parent, theme::TRACK);
    lv_obj_set_size(track_, cw_, bar_h_);
    lv_obj_set_pos(track_, x0_, bar_y_);
    lv_obj_set_style_radius(track_, bar_h_ / 2, 0);
    fill_ = ui::block(parent, theme::ACCENT);
    lv_obj_set_size(fill_, bar_h_, bar_h_);
    lv_obj_set_pos(fill_, x0_, bar_y_);
    lv_obj_set_style_radius(fill_, bar_h_ / 2, 0);
  }

  void make_box(lv_obj_t *parent, int x, int top, int w, int h, int row_gap, int cells) {
    const int gap = ui::px(8);
    const int cell = (w - (SLOTS_PER_BOX - 1) * gap) / SLOTS_PER_BOX;
    const int sw = std::max(ui::px(8), std::min(h - ui::px(12), ui::mm(6)));
    const int lh = ui::line_height(Font::BODY_LARGE);
    for (int i = 0; i < cells && i < SLOTS; ++i) {
      const int cx = x + (i % SLOTS_PER_BOX) * (cell + gap);
      const int y = top + (i / SLOTS_PER_BOX) * (h + row_gap);
      box_cell_[i] = ui::block(parent, theme::CARD);
      lv_obj_set_size(box_cell_[i], cell, h);
      lv_obj_set_pos(box_cell_[i], cx, y);
      lv_obj_set_style_border_width(box_cell_[i], std::max(1, ui::px(2)), 0);
      box_swatch_[i] = ui::block(parent, theme::TRACK);
      lv_obj_set_size(box_swatch_[i], sw, sw);
      lv_obj_set_pos(box_swatch_[i], cx + ui::px(8), y + (h - sw) / 2);
      lv_obj_set_style_radius(box_swatch_[i], LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(box_swatch_[i], std::max(1, ui::px(1)), 0);
      box_words_[i] = ui::label(parent, Font::BODY_LARGE, theme::INK);
      const int indent = ui::px(8) + sw + ui::px(8);
      lv_obj_set_size(box_words_[i], std::max(1, cell - indent - ui::px(4)), lh);
      lv_obj_set_pos(box_words_[i], cx + indent, y + (h - lh) / 2);
      show(box_cell_[i], false);
      show(box_swatch_[i], false);
      show(box_words_[i], false);
    }
  }

  void paint_box() {
    const Print &p = print;
    const int active = p.active();
    for (int i = 0; i < SLOTS; ++i) {
      if (!box_cell_[i]) continue;
      const Slot &s = p.slot[i];
      const bool on = i < p.slots && s.shown && p.online();
      show(box_cell_[i], on);
      show(box_swatch_[i], on);
      show(box_words_[i], on);
      if (!on) continue;
      lv_obj_set_style_bg_color(box_cell_[i], ui::color(theme::CARD), 0);
      lv_obj_set_style_border_color(box_cell_[i], ui::color(i == active ? theme::ACCENT : theme::LINE), 0);
      lv_obj_set_style_border_color(box_swatch_[i], ui::color(theme::LINE), 0);
      lv_obj_set_style_bg_color(box_swatch_[i], s.has_color ? lv_color_make(s.r, s.g, s.b) : ui::color(theme::TRACK), 0);
      ui::set_text(box_words_[i], s.empty ? std::string(plugin_->text("empty")) : std::string(s.type));
      ui::set_color(box_words_[i], s.empty ? theme::MUTED : i == active ? theme::INK : theme::SLATE);
    }
  }

  bool paint_detail(Detail d, Row &row, uint32_t epoch) {
    const Print &p = print;
    const bool busy = p.running() || p.phase == Phase::COMPLETE;
    switch (d) {
      case Detail::WARNING: {
        if (!p.online() || p.warnings <= 0) return false;
        row.paint(ICON_WARNING, p.warnings == 1 ? std::string(plugin_->text("box_warning"))
                                                : tessera::fill(plugin_->text("box_warnings"), "n",
                                                                std::to_string(p.warnings)));
        ui::set_tone(row.icon, Tone::ALERT, theme::SLATE);
        return true;
      }
      case Detail::LAYER: {
        if (!busy || !p.layer_known()) return false;
        std::string t = tessera::fill(plugin_->text("layer"), "layer", std::to_string(p.layer));
        t = tessera::fill(t.c_str(), "layers", std::to_string(p.layers));
        row.paint(ICON_LAYERS, t);
        return true;
      }
      case Detail::ELAPSED: {
        if (!p.running()) return false;
        const float e = p.elapsed_now(epoch);
        if (!std::isfinite(e)) return false;
        row.paint(ICON_TIMER, tessera::fill(plugin_->text("elapsed"), "time", duration_text(plugin_, e)));
        return true;
      }
      case Detail::CHANGE: {
        // A filament change while it runs: its step and the slot it loads, with that spool's colour.
        if (!p.change[0] || !p.online()) return false;
        const int to = p.change_slot;
        const Slot *spool = to >= 0 && to < p.slots && p.slot[to].shown ? &p.slot[to] : nullptr;
        row.paint(ICON_CHANGE, change_text(p.change, to, p.change_stepped), spool);
        return true;
      }
      case Detail::FILAMENT: {
        // The spool that feeds the toolhead, and how much of it this print used.
        const int a = p.active();
        const bool used = busy && std::isfinite(p.filament) && p.filament > 0;
        if (a < 0 && !used) return false;
        std::string t;
        if (a >= 0) t = p.slot[a].type;
        else if (p.any_slot() && p.online()) t = plugin_->text("external");
        if (used) t += (t.empty() ? "" : " · ") + metres(plugin_, p.filament);
        row.paint(ICON_PRINTER, t, a >= 0 ? &p.slot[a] : nullptr);
        return true;
      }
      case Detail::DRYING:
        if (!p.online() || !p.drying) return false;
        row.paint(ICON_DRYING, plugin_->text("box_drying"));
        return true;
      case Detail::NOZZLE:
        if (!p.online() || !std::isfinite(p.nozzle)) return false;
        row.paint(ICON_NOZZLE, tessera::fill(plugin_->text("nozzle"), "t", degrees(p.nozzle)));
        return true;
      case Detail::BED:
        if (!p.online() || !std::isfinite(p.bed)) return false;
        row.paint(ICON_BED, tessera::fill(plugin_->text("bed"), "t", degrees(p.bed)));
        return true;
      case Detail::CHAMBER:
        if (!p.online() || !std::isfinite(p.chamber)) return false;
        row.paint(ICON_CHAMBER, tessera::fill(plugin_->text("chamber"), "t", degrees(p.chamber)));
        return true;
      case Detail::SPEED: {
        if (!p.running() || !std::isfinite(p.speed)) return false;
        char b[12];
        snprintf(b, sizeof(b), "%.0f", p.speed);
        row.paint(ICON_SPEED, tessera::fill(plugin_->text("speed"), "v", b));
        return true;
      }
    }
    return false;
  }

  // "Cutting filament", "Loading 1D", "Purging 1D"...: the step's words, and the slot for the steps that work on it.
  // "changing" is the purge once a step ran, and "Changing filament" before the task starts; a step without words
  // of its own says "Changing filament" too.
  std::string change_text(const char *step, int to, bool stepped) const {
    struct Step { const char *step, *key; bool slot; };
    static const Step STEPS[] = {
        {"starting", "change_starting", false}, {"cutting", "change_cutting", false},
        {"unloading", "change_unloading", false}, {"loading", "change_loading", true},
        {"eject", "change_ejecting", true},       {"paused", "change_paused", false},
    };
    const char *key = "change_other";
    bool slot = false;
    if (!strcmp(step, "changing")) {
      if (stepped) key = "change_purging", slot = true;
    } else {
      for (const auto &s : STEPS)
        if (!strcmp(step, s.step)) key = s.key, slot = s.slot;
    }
    std::string t = plugin_->text(key);
    if (slot && to >= 0) t += " " + slot_name(to);
    return t;
  }

  std::string left_text(float left) const {
    return left < 60 ? std::string(plugin_->text("under_minute")) : duration_text(plugin_, left);
  }

  std::string layer_short(const Print &p) const {
    const std::string t = tessera::fill(plugin_->text("layer_short"), "layer", std::to_string(p.layer));
    return tessera::fill(t.c_str(), "layers", std::to_string(p.layers));
  }

  std::string done_text(uint32_t at) const {
    const std::string time = tessera::clock_text(at);
    const int32_t days = tessera::days_from_today(at);
    if (days == 0 || days == INT32_MIN) return tessera::fill(plugin_->text("done_at"), "time", time);
    const std::string t = tessera::fill(plugin_->text("done_on"), "day", tessera::date_text(at));
    return tessera::fill(t.c_str(), "time", time);
  }

  QidiPrint *plugin_;
  bool card_ = false, compact_ = false, pic_shown_ = false;
  int w_ = 0, x0_ = 0, cw_ = 1, bar_y_ = 0, bar_h_ = 1, pct_w_ = 0, box_h_ = 0, rows_ = 0;
  lv_obj_t *pic_{}, *pic_icon_{}, *title_{}, *state_icon_{}, *state_{}, *pct_{}, *left_{}, *eta_{}, *track_{}, *fill_{};
  Row row_[DETAILS];
  Key key_[KEYS];
  bool has_keys_ = false, stop_armed_ = false;
  int box_rows_ = 0;
  int keys_y_ = 0, keys_h_ = 0;
  uint32_t stop_armed_at_ = 0;
  char stop_file_[64] = "";
  lv_obj_t *box_cell_[SLOTS]{}, *box_swatch_[SLOTS]{}, *box_words_[SLOTS]{};
};

// What the card shows when it opens: the readings of the tile that opened it (a card's context has none of its own).
static Print opened_print;
static Controls opened_controls;

// ---- The tile ----

class PrintTile : public tessera::Tile {
 public:
  explicit PrintTile(QidiPrint *plugin) : plugin_(plugin), face_(plugin) {}

  void create(const tessera::TileContext &c) override {
    tile_ = c.tile;
    copy(entity_, sizeof(entity_), c.entity);
    face_.ctl.read(c.options);
    face_.build(c.parent, c.width, c.height, false, entity_);
  }

  void on_state(JsonObjectConst data) override { face_.print.read(data); }
  void on_tick(uint32_t epoch) override { face_.paint(epoch); }
  void on_theme() override { face_.paint(tessera::epoch()); }
  void on_picture(lv_obj_t *place, tessera::PictureState state) override { face_.on_picture(place, state); }

  void on_tap() override {
    const tessera::Point at = tessera::tap_point();
    if (face_.press(at.x, at.y, tile_)) return;
    if (face_.print.phase == Phase::CHOOSE) return;
    opened_print = face_.print;
    opened_controls = face_.ctl;
    tessera::open_card(plugin_->plugin_id(), "detail", entity_, tile_);
  }

 private:
  QidiPrint *plugin_;
  Face face_;
  int tile_ = -1;
  char entity_[96] = "";
};

// ---- The card: everything at the size of the glass ----

class DetailCard : public tessera::Card {
 public:
  explicit DetailCard(QidiPrint *plugin) : plugin_(plugin), face_(plugin) {}

  void open(const tessera::CardContext &c) override {
    tile_ = c.tile;
    copy(entity_, sizeof(entity_), c.entity);
    face_.print = opened_print;
    face_.ctl = opened_controls;
    face_.build(c.parent, c.width, c.height, true, c.entity ? c.entity : "");
    face_.paint(tessera::epoch());
  }
  void on_state(JsonObjectConst data) override {
    // A tile of Tessera's own sends {"state", "name"}: only a plugin tile's data has the sensors.
    if (data["entities"].isNull() && data["wait"].isNull()) return;
    face_.print.read(data);
    // A Box more or less than the card was laid out for (one switched on or off): open the card again, so its rows
    // of slots fit. The card leaves the glass at once and gets no call after this one.
    if (Face::rows_for(face_.print.slots) != face_.box_rows()) {
      opened_print = face_.print;
      opened_controls = face_.ctl;
      tessera::open_card(plugin_->plugin_id(), "detail", entity_, tile_);
      return;
    }
    face_.paint(tessera::epoch());
  }
  void on_tick(uint32_t epoch) override { face_.paint(epoch); }
  void on_theme() override { face_.paint(tessera::epoch()); }
  void on_picture(lv_obj_t *place, tessera::PictureState state) override { face_.on_picture(place, state); }
  void on_tap() override {
    const tessera::Point at = tessera::tap_point();
    if (!face_.press(at.x, at.y, tile_)) tessera::close_card();
  }

 private:
  QidiPrint *plugin_;
  Face face_;
  int tile_ = -1;
  char entity_[96] = "";
};

// ---- The camera: the printer's camera over the whole room, a tap closes it ----

class CameraCard : public tessera::Card {
 public:
  explicit CameraCard(QidiPrint *plugin) : plugin_(plugin) {}

  void open(const tessera::CardContext &c) override {
    const std::string entity = c.entity ? c.entity : "";
    note_ = ui::label(c.parent, Font::BODY_LARGE, theme::MUTED);
    lv_obj_set_size(note_, c.width, ui::line_height(Font::BODY_LARGE));
    lv_obj_set_pos(note_, 0, (c.height - ui::line_height(Font::BODY_LARGE)) / 2);
    lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
    show(note_, false);
    if (!ui::pictures() || entity.rfind("camera.", 0) != 0) {
      ui::set_text(note_, plugin_->text("no_camera"));
      show(note_, true);
      return;
    }
    place_ = ui::picture(c.parent);
    lv_obj_set_size(place_, c.width, c.height);
    lv_obj_set_pos(place_, 0, 0);
    ui::set_picture(place_, "camera:" + entity, tessera::Fit::CONTAIN);
  }
  void on_picture(lv_obj_t *place, tessera::PictureState state) override {
    if (place != place_) return;
    ui::set_text(note_, plugin_->text("no_camera"));
    show(note_, state != tessera::PictureState::SHOWN);
  }
  void on_theme() override { ui::set_color(note_, theme::MUTED); }
  void on_tap() override { tessera::close_card(); }

 private:
  QidiPrint *plugin_;
  lv_obj_t *place_{}, *note_{};
};

// ---- The plugin ----

void QidiPrint::setup() {
  add_tile("print", [this]() { return new PrintTile(this); });
  add_card("detail", [this]() { return new DetailCard(this); }, true);
  add_card("camera", [this]() { return new CameraCard(this); }, true);
}

}  // namespace esphome::qidi_print
