/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2020 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "../../inc/MarlinConfigPre.h"

#if HAS_MARLINUI_MENU

#include "menu.h"
#include "../../module/planner.h"
#include "../../module/motion.h"
#include "../../module/printcounter.h"
#include "../../module/temperature.h"
#include "../../gcode/queue.h"

#if HAS_GRAPHICAL_TFT
  #include "../tft/tft.h"
#endif

#if HAS_SOUND
  #include "../../libs/buzzer.h"
#endif

#if ENABLED(BABYSTEP_ZPROBE_OFFSET)
  #include "../../module/probe.h"
#endif

#if ENABLED(LEVEING_CALIBRATION_MODULE)
  #include "../../HAL/STM32/autoGetZoffset.h"
#endif

#if HAS_LEVELING
  #include "../../feature/bedlevel/bedlevel.h"
#endif

////////////////////////////////////////////
///////////// Global Variables /////////////
////////////////////////////////////////////

#if HAS_LEVELING && ANY(LCD_BED_TRAMMING, PROBE_OFFSET_WIZARD, X_AXIS_TWIST_COMPENSATION)
  bool menu_leveling_was_active; // = false
#endif
#if ANY(PROBE_MANUALLY, MESH_BED_LEVELING, X_AXIS_TWIST_COMPENSATION)
  uint8_t manual_probe_index; // = 0
#endif

// Menu Navigation
int8_t encoderTopLine, encoderLine, screen_items;

typedef struct {
  screenFunc_t menu_function;     // The screen's function
  uint32_t encoder_position;      // The position of the encoder
  int8_t top_line, items;         // The amount of scroll, and the number of items
  #if HAS_SCREEN_TIMEOUT
    bool sticky;                  // The screen is sticky
  #endif
} menuPosition;
menuPosition screen_history[6];
uint8_t screen_history_depth = 0;

int8_t MenuItemBase::itemIndex;         // Index number for draw and action
FSTR_P MenuItemBase::itemStringF;       // A string for substitution
const char *MenuItemBase::itemStringC;
chimera_t editable;                     // Value Editing

// Menu Edit Items
FSTR_P       MenuEditItemBase::editLabel;
void*        MenuEditItemBase::editValue;
int32_t      MenuEditItemBase::minEditValue,
             MenuEditItemBase::maxEditValue;
screenFunc_t MenuEditItemBase::callbackFunc;
bool         MenuEditItemBase::liveEdit;

#if ENABLED(TFT_COLOR_UI)
  #if ENABLED(TOUCH_SCREEN)
    float    MenuEditItemBase::valueStep;
  #endif
  intptr_t   MenuEditItemBase::valueToString;
  bool       MenuEditItemBase::itemEdit; // = false
#endif

////////////////////////////////////////////
//////// Menu Navigation & History /////////
////////////////////////////////////////////

void MarlinUI::return_to_status() { goto_screen(status_screen); }

void MarlinUI::push_current_screen() {
  if (screen_history_depth < COUNT(screen_history))
    screen_history[screen_history_depth++] = { currentScreen, encoderPosition, encoderTopLine, screen_items OPTARG(HAS_SCREEN_TIMEOUT, screen_is_sticky()) };
}

void MarlinUI::_goto_previous_screen(TERN_(TURBO_BACK_MENU_ITEM, const bool is_back/*=false*/)) {
  IF_DISABLED(TURBO_BACK_MENU_ITEM, constexpr bool is_back = false);
  TERN_(HAS_TOUCH_BUTTONS, on_edit_screen = false);
  if (screen_history_depth > 0) {
    menuPosition &sh = screen_history[--screen_history_depth];
    goto_screen(sh.menu_function,
      is_back ? 0 : sh.encoder_position,
      is_back ? 0 : sh.top_line,
      sh.items
    );
    defer_status_screen(TERN_(HAS_SCREEN_TIMEOUT, sh.sticky));
  }
  else
    return_to_status();
}

////////////////////////////////////////////
/////////// Menu Editing Actions ///////////
////////////////////////////////////////////

// All Edit Screens run the same way, but `draw_edit_screen` is implementation-specific
void MenuEditItemBase::edit_screen(strfunc_t strfunc, loadfunc_t loadfunc) {
  // Reset repeat_delay for Touch Buttons
  TERN_(HAS_TOUCH_BUTTONS, ui.repeat_delay = BUTTON_DELAY_EDIT);
  // Constrain ui.encoderPosition to 0 ... maxEditValue (calculated in encoder steps)
  ui.encoderPosition = constrain(int32_t(ui.encoderPosition), 0, maxEditValue);
  // If drawing is flagged then redraw the (whole) edit screen
  if (ui.should_draw())
    draw_edit_screen(strfunc(ui.encoderPosition + minEditValue));
  // If there was a click or "live editing" and encoder moved...
  if (ui.lcd_clicked || (liveEdit && ui.should_draw())) {
    // Pass the editValue pointer to the loadfunc along with the encoder plus min
    if (editValue) loadfunc(editValue, ui.encoderPosition + minEditValue);
    // If a callbackFunc was set, call it for click or always for "live editing"
    if (callbackFunc && (liveEdit || ui.lcd_clicked)) (*callbackFunc)();
    // Use up the click to finish editing and go to the previous screen
    if (ui.use_click()) ui.goto_previous_screen();
  }
}

// Going to an edit screen sets up some persistent values first
void MenuEditItemBase::goto_edit_screen(
    FSTR_P const el       // Edit label
  , void * const ev       // Edit value pointer
  , const int32_t minv    // Encoder minimum
  , const int32_t maxv    // Encoder maximum
  OPTARG(TFT_COLOR_UI, intptr_t to_string)  // Value-to-string conversion function
  OPTARG(TFT_COLOR_TOUCH, const float step) // Encoder units per displayed unit
  , const uint32_t ep     // Initial encoder value
  , const screenFunc_t cs // MenuItem_type::draw_edit_screen => MenuEditItemBase::edit()
  , const screenFunc_t cb // Callback after edit
  , const bool le         // Flag to call cb() during editing
) {
  TERN_(HAS_TOUCH_BUTTONS, ui.on_edit_screen = true);
  ui.screen_changed = true;
  ui.push_current_screen();
  ui.refresh();
  editLabel = el;
  editValue = ev;
  minEditValue = minv;
  maxEditValue = maxv;
  TERN_(TFT_COLOR_UI, valueToString = to_string);
  TERN_(TFT_COLOR_TOUCH, valueStep = step);
  TERN_(TFT_COLOR_TOUCH, reset_edit_screen_state());
  ui.encoderPosition = ep;
  ui.currentScreen = cs;
  callbackFunc = cb;
  liveEdit = le;
}

////////////////////////////////////////////
///////////////// Menu Tree ////////////////
////////////////////////////////////////////

#include "../../MarlinCore.h"

/**
 * General function to go directly to a screen
 */
void MarlinUI::goto_screen(screenFunc_t screen, const uint16_t encoder/*=0*/, const uint8_t top/*=0*/, const uint8_t items/*=0*/) {
  if (currentScreen == screen) return;

  wake_display();

  thermalManager.set_menu_cold_override(false);

  TERN_(IS_DWIN_MARLINUI, did_first_redraw = false);

  TERN_(HAS_TOUCH_BUTTONS, repeat_delay = BUTTON_DELAY_MENU);

  TERN_(SET_PROGRESS_PERCENT, progress_reset());

  /**
   * Double-click on the status screen is a shortcut for one of these:
   *   - Babystep the Probe Z Offset
   *   - Babystep the Z axis
   *   - Move the Z axis
   */
  #if ALL(DOUBLECLICK_FOR_Z_BABYSTEPPING, BABYSTEPPING)
    static millis_t doubleclick_expire_ms = 0;
    if (screen == menu_main) {
      if (on_status_screen())
        doubleclick_expire_ms = millis() + DOUBLECLICK_MAX_INTERVAL;
    }
    else if (
      screen == status_screen
      && currentScreen == menu_main
      && encoderPosition == 0
      && PENDING(millis(), doubleclick_expire_ms)
    ) {
      if (BABYSTEP_ALLOWED())
        screen = TERN(BABYSTEP_ZPROBE_OFFSET, lcd_babystep_zoffset, lcd_babystep_z);
      else {
        #if ENABLED(MOVE_Z_WHEN_IDLE)
          ui.manual_move.menu_scale = MOVE_Z_IDLE_MULTIPLICATOR;
          screen = []{ lcd_move_axis(Z_AXIS); };
        #endif
      }
    }
  #endif

  //
  // Clear alerts when exiting the Status Screen to the Main Menu
  //

  if (currentScreen == status_screen && screen == menu_main) {
    reset_alert_level();
    reset_status();
  }

  //
  // Go to the new screen
  //

  currentScreen = screen;
  encoderPosition = encoder;
  encoderTopLine = top;
  screen_items = items;
  if (on_status_screen()) {
    defer_status_screen(false);
    clear_menu_history();
    TERN_(AUTO_BED_LEVELING_UBL, bedlevel.lcd_map_control = false);
  }

  clear_for_drawing();

  // Re-initialize custom characters that may be re-used
  #if HAS_MARLINUI_HD44780
    if (TERN1(AUTO_BED_LEVELING_UBL, !bedlevel.lcd_map_control))
      set_custom_characters(on_status_screen() ? CHARSET_INFO : CHARSET_MENU);
  #endif

  refresh(LCDVIEW_CALL_REDRAW_NEXT);
  screen_changed = true;
  TERN_(HAS_MARLINUI_U8GLIB, drawing_screen = false);

  TERN_(HAS_MARLINUI_MENU, encoder_direction_normal());
  enable_encoder_multiplier(false);

  set_selection(false);
}

////////////////////////////////////////////
///////////// Manual Movement //////////////
////////////////////////////////////////////

//
// Display a "synchronize" screen with a custom message until
// all moves are finished. Go back to calling screen when done.
//
void MarlinUI::synchronize(FSTR_P const fmsg/*=nullptr*/) {
  push_current_screen();

  // Hijack 'editable' for the string pointer
  editable.fstr = fmsg ?: GET_TEXT_F(MSG_MOVING);
  goto_screen([]{
    if (should_draw()) MenuItem_static::draw(LCD_HEIGHT >= 4, editable.fstr);
  });

  defer_status_screen();
  planner.synchronize(); // idle() is called until moves complete
  goto_previous_screen_no_defer();
}

/**
 * Scrolling for menus and other line-based screens
 *
 *   encoderLine is the position based on the encoder
 *   encoderTopLine is the top menu line to display
 *   screen_items is the total number of items in the menu (after one call)
 */
void scroll_screen(const uint8_t limit, const bool is_menu) {
  ui.encoder_direction_menus();
  if (int32_t(ui.encoderPosition) < 0) ui.encoderPosition = 0;
  if (ui.first_page) {
    encoderLine = ui.encoderPosition / (ENCODER_STEPS_PER_MENU_ITEM);
    ui.screen_changed = false;
  }
  if (screen_items > 0 && encoderLine >= screen_items - limit) {
    encoderLine = _MAX(0, screen_items - limit);
    ui.encoderPosition = encoderLine * (ENCODER_STEPS_PER_MENU_ITEM);
  }
  if (is_menu) {
    NOMORE(encoderTopLine, encoderLine);
    if (encoderLine >= encoderTopLine + LCD_HEIGHT)
      encoderTopLine = encoderLine - LCD_HEIGHT + 1;
  }
  else
    encoderTopLine = encoderLine;
}

/**
 * Called when a screen pass ends and the true item count is known.
 *
 * A menu can have fewer items than when it was last shown - the leveling menu
 * drops "Auto Home" once the machine is homed - while it returns scrolled just
 * as it was left, leaving the view hanging past the last item and a blank row
 * at the bottom. Pull the view back onto the list and draw the screen again.
 */
void scroll_screen_end() {
  if (screen_items <= 0) return;
  const int8_t top_max = _MAX(0, screen_items - (LCD_HEIGHT));
  if (encoderTopLine <= top_max) return;
  encoderTopLine = top_max;
  // Abandon the frame in progress. A paged display would otherwise keep the
  // passes already drawn at the old offset and finish at the new one.
  TERN_(HAS_MARLINUI_U8GLIB, ui.drawing_screen = false);
  ui.refresh(LCDVIEW_CALL_REDRAW_NEXT);
}

#if HAS_LINE_TO_Z

  void line_to_z(const float z) {
    motion.position.z = z;
    motion.goto_current_position(manual_feedrate_mm_s.z);
  }

#endif

#if ENABLED(BABYSTEP_ZPROBE_OFFSET)

  #include "../../feature/babystep.h"

  void lcd_babystep_zoffset() {
    if (ui.use_click()) return ui.goto_previous_screen_no_defer();
    ui.defer_status_screen();
    const bool do_probe = DISABLED(BABYSTEP_HOTEND_Z_OFFSET) || motion.extruder == 0;
    if (ui.encoderPosition) {
      const int16_t babystep_increment = int16_t(ui.encoderPosition) * (BABYSTEP_SIZE_Z);
      ui.encoderPosition = 0;

      const float diff = planner.mm_per_step[Z_AXIS] * babystep_increment,
                  new_probe_offset = probe.offset.z + diff,
                  new_offs = TERN(BABYSTEP_HOTEND_Z_OFFSET
                    , do_probe ? new_probe_offset : motion.active_hotend_offset().z - diff
                    , new_probe_offset
                  );
      if (WITHIN(new_offs, PROBE_OFFSET_ZMIN, PROBE_OFFSET_ZMAX)) {

        babystep.add_steps(Z_AXIS, babystep_increment);

        if (do_probe)
          probe.offset.z = new_offs;
        else
          TERN(BABYSTEP_HOTEND_Z_OFFSET, motion.active_hotend_offset().z = new_offs, NOOP);

        ui.refresh(LCDVIEW_CALL_REDRAW_NEXT);
      }
    }
    if (ui.should_draw()) {
      if (do_probe) {
        MenuEditItemBase::draw_edit_screen(GET_TEXT_F(MSG_BABYSTEP_PROBE_Z), BABYSTEP_TO_STR(probe.offset.z));
        TERN_(BABYSTEP_GFX_OVERLAY, ui.zoffset_overlay(probe.offset.z));
      }
      else {
        #if ENABLED(BABYSTEP_HOTEND_Z_OFFSET)
          MenuEditItemBase::draw_edit_screen(GET_TEXT_F(MSG_HOTEND_OFFSET_Z), ftostr54sign(motion.active_hotend_offset().z));
        #endif
      }
    }
  }

#endif // BABYSTEP_ZPROBE_OFFSET

void _lcd_draw_homing() {
  if (ui.should_draw()) {
    constexpr uint8_t line = (LCD_HEIGHT - 1) / 2;
    MenuItem_static::draw(line, GET_TEXT_F(MSG_LEVEL_BED_HOMING));
  }
}

#if HAS_LEVELING
  void _lcd_toggle_bed_leveling() { set_bed_leveling_enabled(!planner.leveling_active); }
#endif

//
// Selection screen presents a prompt and two options
//
bool MarlinUI::selection; // = false
bool MarlinUI::update_selection() {
  encoder_direction_select();
  if (encoderPosition) {
    selection = int16_t(encoderPosition) > 0;
    encoderPosition = 0;
  }
  return selection;
}

uint8_t MarlinUI::multi_selection; // = 0
uint8_t MarlinUI::update_multi_selection(const uint8_t num) {
  if (int16_t(encoderPosition) >= 1) {
    multi_selection++;
    if (multi_selection >= num) multi_selection = num;
    encoderPosition = 0;
  }
  else if (int16_t(encoderPosition) <= -1) {
    if (multi_selection == 0)
      multi_selection = 0;
    else
      multi_selection--;
    encoderPosition = 0;
  }
  return multi_selection;
}

void MenuItem_confirm::select_screen(
  FSTR_P const yes, FSTR_P const no,
  selectFunc_t yesFunc, selectFunc_t noFunc,
  FSTR_P const fpre, const char * const string/*=nullptr*/, FSTR_P const fsuf/*=nullptr*/
) {
  ui.defer_status_screen();
  const bool ui_selection = !yes ? false : !no || ui.update_selection(),
             got_click = ui.use_click();
  if (got_click || ui.should_draw()) {
    ui.last_confirm_windown_enabled = ui.confirm_windown_enabled;
    ui.confirm_windown_enabled = true;
    draw_select_screen(yes, no, ui_selection, fpre, string, fsuf);
    if (got_click) {
      ui.confirm_windown_enabled = false;
      selectFunc_t callFunc = !ui_selection ? yesFunc : noFunc; // Factory: the encoder selects Cancel first
      if (callFunc) callFunc();
      else { ui.goto_previous_screen(); ui.previous_callbackFunc(); }
    }
  }
}

// Factory (Anycubic) screens

void tft_stop_print() {
  ui.defer_status_screen();
  const bool ui_selection = ui.update_selection(), got_click = ui.use_click();
  if (got_click || ui.should_draw()) {
    ui.last_confirm_windown_enabled = ui.confirm_windown_enabled;
    ui.confirm_windown_enabled = true;
    MenuItem_confirm::draw_select_screen(
      GET_TEXT_F(MSG_BUTTON_STOP), GET_TEXT_F(MSG_BACK),
      ui_selection,
      GET_TEXT_F(MSG_STOP_PRINT), (const char *)nullptr, nullptr
    );
    if (got_click) {
      ui.confirm_windown_enabled = false;
      selectFunc_t callFunc = !ui_selection ? ui.abort_print : ui.return_to_status;
      if (callFunc) callFunc();
      else ui.goto_previous_screen();
    }
  }
}

void tft_pause_print() {
  ui.defer_status_screen();
  const bool ui_selection = ui.update_selection(), got_click = ui.use_click();
  if (got_click || ui.should_draw()) {
    ui.last_confirm_windown_enabled = ui.confirm_windown_enabled;
    ui.confirm_windown_enabled = true;
    MenuItem_confirm::draw_select_screen(
      GET_TEXT_F(MSG_BUTTON_STOP), GET_TEXT_F(MSG_BACK),
      ui_selection,
      GET_TEXT_F(MSG_PAUSE_PRINT), (const char *)nullptr, nullptr
    );
    if (got_click) {
      ui.confirm_windown_enabled = false;
      ui.return_to_status();                     // Leave the dialog so it cannot re-queue commands
      if (!ui_selection) {                       // First button pressed
        if (marlin.wait_for_user || marlin.printingIsPaused() || did_pause_print)
          ui.resume_print();                     // Already paused or waiting: continue the print
        else if (!ui.pause_pending)
          ui.pause_print();                      // Otherwise request a pause
      }
    }
  }
}

void runout_sensor() {
  if (ui.use_click())
    return ui.return_to_status();

  if (ui.should_draw()) {
    tft.canvas(18, 81, 284, 32);
    tft.set_background(COLOR_BACKGROUND);
    tft_string.set(GET_TEXT_F(MSG_RUNOUT_SENSOR));
    tft.add_text(tft_string.center(284), 5, COLOR_WHITE, tft_string);

    tft.canvas(105, 136, 110, 44);
    tft.set_background(COLOR_BACKGROUND);
    tft.add_image(0, 0, imgConfirm, COLOR_BLUE);
  }
}

void sd_card_removed() {
  if (ui.use_click()) {
    ui.start_print_status = false;
    ui.print_task_done = false;
    marlin.end_waiting();
    did_pause_print = 0;
    return ui.return_to_status();
  }

  if (ui.should_draw()) {
    ui.flexible_clear_lcd(0, 0, 50, TFT_HEIGHT);
    tft.canvas(18, 52, 284, 32);
    tft.set_background(COLOR_BACKGROUND);
    tft_string.set(GET_TEXT_F(MSG_TF_CARD_REMOVED));
    tft.add_text(tft_string.center(284), tft_string.center(32), COLOR_WHITE, tft_string);

    tft.canvas(105, 136, 110, 104);
    tft.set_background(COLOR_BACKGROUND);
    tft.add_image(0, 0, imgConfirm, COLOR_BLUE);
  }
}

// Factory (Anycubic) edit screens for the status screen tiles

void draw_zoffset_select_screen(uint16_t back_color, uint16_t upcolor, uint16_t downcolor, float zoffset, uint16_t selection) {
  if (selection) {
    ui.fresh_flag = false;
    tft.canvas(124, 26, 100, 20);
    tft.set_background(COLOR_BACKGROUND);
    tft_string.set(GET_TEXT_F(MSG_UBL_Z_OFFSET));
    tft.add_text(0, 0, COLOR_WHITE, tft_string);

    tft.canvas(34, 74, 136, 136);
    tft.set_background(COLOR_BACKGROUND);
    tft.add_image(0, 0, imgZoffsetTip, COLOR_GREY);
    tft.add_image(0, 90, imgZoffsetTip1, 0x07FE);
  }

  tft.canvas(22, 22, 40, 40);
  tft.set_background(COLOR_BACKGROUND);
  tft.add_image(0, 0, imgBack, back_color);

  tft.canvas(224, 63, 62, 56);
  tft.set_background(COLOR_BACKGROUND);
  tft.add_image(0, 0, imgUp, upcolor);

  tft.canvas(216, 129, 78, 32);
  tft.set_background(COLOR_BACKGROUND);
  tft_string.set(ftostr42_52(zoffset));
  tft_string.add("mm");
  tft.add_text(tft_string.center(78), tft_string.center(32), COLOR_WHITE, tft_string);

  tft.canvas(224, 170, 62, 56);
  tft.set_background(COLOR_BACKGROUND);
  tft.add_image(0, 0, imgDown, downcolor);
}

#if ENABLED(BABYSTEP_ZPROBE_OFFSET)

  void tft_babystep_zoffset() {
    const uint8_t selection = ui.update_multi_selection(3 - 1);
    const bool got_click = ui.use_click();
    uint16_t back_color = COLOR_GREY, up_color = COLOR_GREY, down_color = COLOR_GREY;
    static int16_t direction;

    ui.defer_status_screen();

    if (got_click || ui.should_draw()) {
      switch (selection) {
        case 0:
          back_color = COLOR_WHITE;
          up_color = down_color = COLOR_GREY;
          break;
        case 1: // UP
          up_color = COLOR_WHITE;
          back_color = down_color = COLOR_GREY;
          direction = 1;
          break;
        case 2: // DOWN
          down_color = COLOR_WHITE;
          back_color = up_color = COLOR_GREY;
          direction = -1;
          break;
      }
      if (got_click) {
        if (selection) {
          const float zoffset = ui.getzoffset();
          const int16_t babystep_increment = direction * BABYSTEP_SIZE_Z;
          float diff = planner.mm_per_step[Z_AXIS] * babystep_increment;
          float new_probe_offset = zoffset + diff;

          if (new_probe_offset < PROBE_OFFSET_ZMIN) new_probe_offset = PROBE_OFFSET_ZMIN;
          if (new_probe_offset > PROBE_OFFSET_ZMAX) new_probe_offset = PROBE_OFFSET_ZMAX;
          if (WITHIN(new_probe_offset, PROBE_OFFSET_ZMIN, PROBE_OFFSET_ZMAX)) {
            babystep.add_steps(Z_AXIS, babystep_increment);
            ui.setzoffset(new_probe_offset);
          }
        }
        else {
          #if ENABLED(LEVEING_CALIBRATION_MODULE)
            if (planner.leveling_active) {
              autoProbe.calibration_positon.z += ui.getzoffset() - ui.temp_probe_zoffset;
              autoProbe.need_save_data = true;
            }
          #endif
          ui.goto_previous_screen();
          ui.clear_all = false;
          if (ui.temp_probe_zoffset != ui.getzoffset())
            queue.inject("M500");
          return;
        }
      }
      draw_zoffset_select_screen(back_color, up_color, down_color, ui.getzoffset(), ui.fresh_flag);
    }
  }

#endif // BABYSTEP_ZPROBE_OFFSET

void draw_edit_temp_screen(FSTR_P const fstr, uint16_t maxlimit, uint16_t tempdata) {
  tft.canvas(0, 20, TFT_WIDTH, 32);
  tft.set_background(COLOR_BACKGROUND);
  tft_string.set(fstr);
  tft.add_text(tft_string.center(TFT_WIDTH), 0, COLOR_WHITE, tft_string);

  tft.canvas(131, 83, 58, 31);
  tft.set_background(COLOR_BACKGROUND);
  tft_string.set(i16tostr3rj(tempdata));
  tft_string.add(GET_TEXT_F(MSG_TEMP_UINT));
  tft.add_text(tft_string.center(58), tft_string.center(31), COLOR_WHITE, tft_string);

  tft.canvas(10, 131, 36, 36);
  tft.set_background(COLOR_BACKGROUND);
  tft.add_image(0, 0, imgLeftRound, COLOR_WHITE);

  tft.canvas(274, 131, 36, 36);
  tft.set_background(COLOR_BACKGROUND);
  tft.add_image(0, 0, imgRightRound, COLOR_WHITE);

  #define SLIDER_LENGTH 208
  tft.canvas(56, 141, SLIDER_LENGTH, 16);
  tft.set_background(COLOR_SLIDER_INACTIVE);
  tft.add_rectangle(0, 0, SLIDER_LENGTH, 16, COLOR_SLIDER_INACTIVE);
  tft.add_bar(1, 1, ((SLIDER_LENGTH - 2) * tempdata) / maxlimit, 14, COLOR_BLUE);
  #undef SLIDER_LENGTH
}

void tft_setTargetHotend() {
  static int16_t target_temp_data;
  static bool fresh_flag;

  ui.defer_status_screen();
  if (ui.use_click()) {
    ui.enable_encoder_multiplier(false);
    fresh_flag = false;
    ui.clear_all = false;
    thermalManager.temp_hotend[0].target = target_temp_data;
    return ui.goto_previous_screen_no_defer();
  }

  if (!fresh_flag) {
    ui.enable_encoder_multiplier(true);
    fresh_flag = true;
    target_temp_data = thermalManager.temp_hotend[0].target;
  }

  if (ui.encoderPosition) {
    target_temp_data += ui.encoderPosition;
    ui.encoderPosition = 0;
    if (marlin.printingIsActive())
      LIMIT(target_temp_data, 170, thermalManager.hotend_max_target(0));
    else
      LIMIT(target_temp_data, 0, thermalManager.hotend_max_target(0));
  }

  if (ui.should_draw())
    draw_edit_temp_screen(GET_TEXT_F(MSG_UBL_HOTEND_TEMP_CUSTOM), thermalManager.hotend_max_target(0), target_temp_data);
}

void tft_setTargetBed() {
  static int16_t target_temp_data;
  static bool fresh_flag;

  ui.defer_status_screen();
  if (ui.use_click()) {
    ui.enable_encoder_multiplier(false);
    fresh_flag = false;
    ui.clear_all = false;
    thermalManager.temp_bed.target = target_temp_data;
    return ui.goto_previous_screen_no_defer();
  }

  if (!fresh_flag) {
    ui.enable_encoder_multiplier(true);
    fresh_flag = true;
    target_temp_data = thermalManager.temp_bed.target;
  }

  if (ui.encoderPosition) {
    target_temp_data += ui.encoderPosition;
    ui.encoderPosition = 0;
    LIMIT(target_temp_data, 0, BED_MAX_TARGET);
  }

  if (ui.should_draw())
    draw_edit_temp_screen(GET_TEXT_F(MSG_UBL_BED_TEMP_CUSTOM), BED_MAX_TARGET, target_temp_data);
}

void tft_set_speed() {
  static uint16_t change = 1;
  static bool fresh_flag;
  static int16_t temp_feedrate_percentage = 100;

  if (ui.use_click()) {
    fresh_flag = false;
    ui.clear_all = false;
    motion.feedrate_percentage = temp_feedrate_percentage;
    return ui.goto_previous_screen_no_defer();
  }

  if (!fresh_flag) {
    fresh_flag = true;
    temp_feedrate_percentage = motion.feedrate_percentage;
    LIMIT(temp_feedrate_percentage, 80, 120);   // Limit 80..120
    change = (temp_feedrate_percentage - 80) / 20;
  }

  if (ui.encoderPosition) {
    temp_feedrate_percentage = temp_feedrate_percentage + 20 * ui.encoderPosition;
    ui.encoderPosition = 0;
    LIMIT(temp_feedrate_percentage, 80, 120);
    change = (temp_feedrate_percentage - 80) / 20;
  }

  if (ui.should_draw()) {
    tft.canvas(0, 20, TFT_WIDTH, 32);
    tft.set_background(COLOR_BACKGROUND);
    tft_string.set(GET_TEXT_F(MSG_SPEED));
    tft.add_text(tft_string.center(TFT_WIDTH), 0, COLOR_WHITE, tft_string);

    #define SPEED_BUTTON_WIDTH 50
    #define SPEED_BUTTON_HEIGHT 32
    tft.canvas(30, 83, SPEED_BUTTON_WIDTH, SPEED_BUTTON_HEIGHT);
    tft.set_background(COLOR_BACKGROUND);
    tft_string.set("80%");
    tft.add_text(tft_string.center(SPEED_BUTTON_WIDTH), tft_string.center(SPEED_BUTTON_HEIGHT), COLOR_WHITE, tft_string);

    tft.canvas(134, 83, SPEED_BUTTON_WIDTH, SPEED_BUTTON_HEIGHT);
    tft.set_background(COLOR_BACKGROUND);
    tft_string.set("100%");
    tft.add_text(tft_string.center(SPEED_BUTTON_WIDTH), tft_string.center(SPEED_BUTTON_HEIGHT), COLOR_WHITE, tft_string);

    tft.canvas(244, 83, SPEED_BUTTON_WIDTH, SPEED_BUTTON_HEIGHT);
    tft.set_background(COLOR_BACKGROUND);
    tft_string.set("120%");
    tft.add_text(tft_string.center(SPEED_BUTTON_WIDTH), tft_string.center(SPEED_BUTTON_HEIGHT), COLOR_WHITE, tft_string);
    #undef SPEED_BUTTON_WIDTH
    #undef SPEED_BUTTON_HEIGHT

    tft.canvas(10, 131, 36, 36);
    tft.set_background(COLOR_BACKGROUND);
    tft.add_image(0, 0, imgLeftRound, COLOR_WHITE);

    tft.canvas(274, 131, 36, 36);
    tft.set_background(COLOR_BACKGROUND);
    tft.add_image(0, 0, imgRightRound, COLOR_WHITE);

    #define SLIDER_LENGTH 208
    tft.canvas(56, 141, SLIDER_LENGTH, 16);
    tft.set_background(COLOR_SLIDER_INACTIVE);
    tft.add_rectangle(0, 0, SLIDER_LENGTH, 16, COLOR_SLIDER_INACTIVE);
    tft.add_bar(1, 1, ((SLIDER_LENGTH - 2) * change) / 2, 14, COLOR_BLUE);
    #undef SLIDER_LENGTH
  }
}

// Print finished screen (M1001)
void printinf_finish() {
  char buffer[22];
  duration_t(print_job_timer.duration()).toString(buffer);
  if (ui.use_click()) {
    ui.confirm_windown_enabled = false;
    ui.start_print_status = ui.print_task_done = false;
    return ui.return_to_status();
  }

  if (ui.should_draw()) {
    ui.flexible_clear_lcd(0, 0, 50, TFT_HEIGHT);
    tft.canvas(18, 38, 284, 32);
    tft.set_background(COLOR_BACKGROUND);
    tft_string.set(GET_TEXT_F(MSG_PRINT_FINISH));
    tft.add_text(tft_string.center(284), 5, COLOR_WHITE, tft_string);

    tft.canvas(18, 81, 284, 20);
    tft.set_background(COLOR_BACKGROUND);
    tft_string.set(buffer);
    tft.add_text(tft_string.center(284), tft_string.center(20), COLOR_WHITE, tft_string);

    tft.canvas(105, 136, 110, 44);
    tft.set_background(COLOR_BACKGROUND);
    tft.add_image(0, 0, imgConfirm, COLOR_BLUE);
  }
}

// Probe / calibration failure screen
void Probing_Failed() {
  if (ui.use_click()) {
    ui.clear_all = false;
    motion.soft_endstop._enabled = true; // The failure path left them off
    #if ENABLED(LEVEING_CALIBRATION_MODULE)
      autoProbe.LeveingFailSattue = false;
    #endif
    ui.flexible_clear_lcd(0, 0, 50, TFT_HEIGHT);
    ui.goto_previous_screen();
    ui.previous_callbackFunc();
    return;
  }

  if (ui.should_draw()) {
    tft.canvas(18, 81, 284, 32);
    tft.set_background(COLOR_BACKGROUND);
    #if ENABLED(LEVEING_CALIBRATION_MODULE)
      if (autoProbe.LeveingFailSattue)
        tft_string.set(GET_TEXT_F(MSG_MODULE_PROBE_FAILD));
      else
    #endif
        tft_string.set(GET_TEXT_F(MSG_PROBE_FAILD));
    tft.add_text(tft_string.center(284), 5, COLOR_WHITE, tft_string);

    tft.canvas(105, 136, 110, 44);
    tft.set_background(COLOR_BACKGROUND);
    tft.add_image(0, 0, imgConfirm, COLOR_BLUE);
  }
}

// React to special status strings set by G28/G29 and the calibration module.
// Only firmware statuses (persist == false) trigger screens, so a host cannot
// open a modal screen with M117 (which sets a persistent status).
void MarlinUI::StatusChange(const char * const msg, const bool persist) {
  reset_alert_level(); // Factory: an active alert must not block later factory screens
  if (persist) return;

  if (strcmp_P(msg, GET_TEXT(MSG_LCD_PROBING_FAILED)) == 0) {
    lcdLeveingstate = LEVEING_NONE;
    motion.soft_endstop._enabled = false;
    calibration_state = false;
    queue.inject_P(PSTR("G1 Z20 F500"));
    clear_all = true;
    goto_screen(Probing_Failed);
  }
  else if (strcmp_P(msg, GET_TEXT(MSG_CALIBRATION_START)) == 0) {
    push_current_screen();
    clear_all = true;
    goto_screen([]{
      if (should_draw()) MenuItem_static::draw(3, GET_TEXT_F(MSG_POSITION_CALIBRATION), 1, "...");
    });
  }
  else if (strcmp_P(msg, GET_TEXT(MSG_CALIBRATION_DONE)) == 0) {
    calibration_state = false; // Do not show the homing tip
    clear_all = false;
    goto_previous_screen();
    previous_callbackFunc();
  }
  else if (strcmp_P(msg, GET_TEXT(MSG_HOMING_START)) == 0) {
    if (calibration_state || lcdLeveingstate || start_print_status) return;
    push_current_screen();
    clear_all = true;
    goto_screen([]{
      if (should_draw()) MenuItem_static::draw(3, GET_TEXT_F(MSG_HOMING));
    });
  }
  else if (strcmp_P(msg, GET_TEXT(MSG_HOMING_DONE)) == 0) {
    if (calibration_state || lcdLeveingstate || start_print_status) return;
    clear_all = false;
    goto_previous_screen();
    previous_callbackFunc();
  }
}

bool calibration_state = false;

// Factory filament commands
_filament_cmd_t filament_cmd = FILA_NO_ACT;
bool unloaOrloaddfilamentstate = false, filament_staring = false;

// Factory Move Axis screen (single jog screen per axis)
void draw_edit_move_axis_screen(FSTR_P const fstr, int8_t axis, const char * const value, uint16_t pos) {
  if (ui.fresh_flag) {
    ui.fresh_flag = false;
    tft.canvas(0, 0, 50, TFT_HEIGHT);
    tft.set_background(COLOR_BACKGROUND);

    tft.canvas(0, 20, TFT_WIDTH, 32);
    tft.set_background(COLOR_BACKGROUND);
    tft_string.set(fstr, axis);
    tft.add_text(tft_string.center(TFT_WIDTH), 0, COLOR_WHITE, tft_string);

    tft.canvas(10, 131, 36, 36);
    tft.set_background(COLOR_BACKGROUND);
    tft.add_image(0, 0, imgLeftRound, COLOR_WHITE);

    tft.canvas(274, 131, 36, 36);
    tft.set_background(COLOR_BACKGROUND);
    tft.add_image(0, 0, imgRightRound, COLOR_WHITE);
  }

  tft.canvas(110, 83, 100, 31);
  tft.set_background(COLOR_BACKGROUND);
  tft_string.set(value);
  tft_string.add('m');
  tft_string.add('m');
  tft.add_text(tft_string.center(100), tft_string.center(31), COLOR_WHITE, tft_string);

  #define SLIDER_LENGTH 208
  const int16_t axis_max =
    #if HAS_X_AXIS
      axis == X_AXIS ? _MAX(1, int16_t(X_BED_SIZE)) :
    #endif
    #if HAS_Y_AXIS
      axis == Y_AXIS ? _MAX(1, int16_t(Y_BED_SIZE)) :
    #endif
    #if HAS_Z_AXIS
      axis == Z_AXIS ? _MAX(1, int16_t(Z_MAX_POS)) :
    #endif
    _MAX(1, int16_t(Z_MAX_POS)); // Never divide by zero for an unexpected axis
  LIMIT(pos, 0, axis_max);
  tft.canvas(56, 141, SLIDER_LENGTH, 16);
  tft.set_background(COLOR_SLIDER_INACTIVE);
  tft.add_rectangle(0, 0, SLIDER_LENGTH, 16, COLOR_SLIDER_INACTIVE);
  tft.add_bar(1, 1, ((SLIDER_LENGTH - 2) * (int16_t)pos) / axis_max, 14, COLOR_BLUE);
  #undef SLIDER_LENGTH
}

// Factory auto-level progress screen
void lcd_level_top_windown() {
  uint16_t preheating_color, wipe_nozzle_color, probe_color, confirm_color;
  uint16_t preheating_Fontcolor, wipe_nozzle_Fontcolor, probe_Fontcolor;
  char nozzle_buf[16];
  char bed_buf[16];
  sprintf(nozzle_buf, "E: %u/%u", (uint16_t)thermalManager.wholeDegHotend(0), (uint16_t)thermalManager.degTargetHotend(0));
  sprintf(bed_buf, "B: %u/%u", (uint16_t)thermalManager.wholeDegBed(), (uint16_t)thermalManager.degTargetBed());

  if (ui.lcdLeveingstate == LEVEING_DONE) {
    if (ui.use_click()) {
      ui.return_to_status();
      ui.clear_all = false;
      ui.lcdLeveingstate = LEVEING_NONE;
      return;
    }
  }
  if (ui.should_draw()) {
    if (ui.lcdLeveingstate == LEVEING_WIPE_NOZZLE) {
      wipe_nozzle_color = probe_color = confirm_color = COLOR_GREY;
      preheating_color = COLOR_GREEN;
      preheating_Fontcolor = wipe_nozzle_Fontcolor = COLOR_WHITE;
      probe_Fontcolor = COLOR_GREY;
    }
    else if (ui.lcdLeveingstate == LEVEING_PROBE) {
      probe_color = confirm_color = COLOR_GREY;
      wipe_nozzle_color = preheating_color = COLOR_GREEN;
      preheating_Fontcolor = wipe_nozzle_Fontcolor = probe_Fontcolor = COLOR_WHITE;
    }
    else if (ui.lcdLeveingstate == LEVEING_DONE) {
      wipe_nozzle_color = preheating_color = probe_color = COLOR_GREEN;
      confirm_color = COLOR_BLUE;
      preheating_Fontcolor = wipe_nozzle_Fontcolor = probe_Fontcolor = COLOR_WHITE;
    }
    else {
      wipe_nozzle_color = preheating_color = probe_color = confirm_color = COLOR_GREY;
      wipe_nozzle_Fontcolor = probe_Fontcolor = COLOR_GREY;
      preheating_Fontcolor = COLOR_WHITE;
    }

    // Nozzle / bed temperatures
    tft.canvas(20, 10, 100, 32);
    tft.set_background(COLOR_BACKGROUND);
    tft_string.set(nozzle_buf);
    tft_string.trim();
    tft.add_text(0, 0, COLOR_WHITE, tft_string);

    tft.canvas(173, 10, 100, 32);
    tft.set_background(COLOR_BACKGROUND);
    tft_string.set(bed_buf);
    tft_string.trim();
    tft.add_text(0, 0, COLOR_WHITE, tft_string);

    // Preheating
    tft.canvas(20, 52, 106, 31);
    tft.set_background(COLOR_BACKGROUND);
    tft_string.set(GET_TEXT(MSG_LEVEING_PREHEATING));
    tft_string.trim();
    tft.add_text(0, 5, preheating_Fontcolor, tft_string);

    // Wipe nozzle
    tft.canvas(20, 92, 130, 31);
    tft.set_background(COLOR_BACKGROUND);
    tft_string.set(GET_TEXT(MSG_LEVEING_WIPE));
    tft_string.trim();
    tft.add_text(0, 5, wipe_nozzle_Fontcolor, tft_string);

    // Probing
    tft.canvas(20, 132, 106, 31);
    tft.set_background(COLOR_BACKGROUND);
    tft_string.set(GET_TEXT(MSG_LEVEING_PROBE));
    tft_string.trim();
    tft.add_text(0, 5, probe_Fontcolor, tft_string);

    tft.canvas(276, 57, 24, 24);
    tft.set_background(COLOR_BACKGROUND);
    tft.add_image(0, 0, imgOK, preheating_color);

    tft.canvas(276, 97, 24, 24);
    tft.set_background(COLOR_BACKGROUND);
    tft.add_image(0, 0, imgOK, wipe_nozzle_color);

    tft.canvas(276, 137, 24, 24);
    tft.set_background(COLOR_BACKGROUND);
    tft.add_image(0, 0, imgOK, probe_color);

    tft.canvas(105, 180, 110, 44);
    tft.set_background(COLOR_BACKGROUND);
    tft.add_image(0, 0, imgConfirm, confirm_color);
  }
  ui.refresh(LCDVIEW_CALL_REDRAW_NEXT);
}

// Factory filament load/unload flow
void unload_load_filament() {
  if (!filament_staring) return;

  static millis_t return_ms = 0;
  #define RETURN_TIMEOUT_MS 25000

  if (filament_cmd == FILA_IN && unloaOrloaddfilamentstate == true) {
    return_ms = millis() + RETURN_TIMEOUT_MS;
    unloaOrloaddfilamentstate = false;
    queue.inject_P("M83\nG1 E100 F300\nM82");
  }
  else if (filament_cmd == FILA_OUT && unloaOrloaddfilamentstate == true) {
    return_ms = millis() + RETURN_TIMEOUT_MS;
    unloaOrloaddfilamentstate = false;
    queue.inject_P("M83\n G1 E30 F300\n G1 E-70 F400\nM82");
  }

  if (ELAPSED(millis(), return_ms)) {
    filament_cmd = FILA_NO_ACT;
    filament_staring = false;
    ui.clear_all = false;
    planner.quick_stop();
    ui.goto_previous_screen_no_defer();
    ui.previous_callbackFunc();
  }
}

void draw_unload_load_filament() {
  filament_staring = true;
  if (ui.use_click()) {
    ui.clear_all = false;
    filament_cmd = FILA_NO_ACT;
    filament_staring = false;
    planner.quick_stop();
    ui.goto_previous_screen_no_defer();
    ui.previous_callbackFunc();
    return;
  }

  if (ui.should_draw()) {
    tft.canvas(0, 68, TFT_WIDTH, 30);
    tft.set_background(COLOR_BACKGROUND);
    if (filament_cmd == FILA_IN) tft_string.set(GET_TEXT_F(MSG_FILAMENTLOADING));
    else if (filament_cmd == FILA_OUT) tft_string.set(GET_TEXT_F(MSG_FILAMENTUNLOADING));
    tft_string.trim();
    tft.add_text(tft_string.center(TFT_WIDTH), 5, COLOR_WHITE, tft_string);

    tft.canvas(80, 136, 160, 44);
    tft.set_background(COLOR_BACKGROUND);
    tft.add_image(0, 0, imgBtn160Rounded, COLOR_GREY);
    tft_string.set(GET_TEXT_F(MSG_FILAMENT_STOP));
    tft_string.trim();
    tft.add_text(tft_string.center(160), 12, COLOR_WHITE, tft_string);
  }
  ui.refresh(LCDVIEW_CALL_REDRAW_NEXT);
}

void preheat_to_move_E() {
  const int16_t currentTemperature = thermalManager.temp_hotend[0].celsius;
  const int16_t targetTemperature = thermalManager.temp_hotend[0].target;

  ui.defer_status_screen();
  if (ui.should_draw()) {
    if (currentTemperature < 205) {
      char str_buf[16];
      sprintf(str_buf, "E: %u/%u", (uint16_t)currentTemperature, (uint16_t)targetTemperature);

      tft.canvas(0, 72, 320, 32);
      tft.set_background(COLOR_BACKGROUND);
      tft_string.set(GET_TEXT_F(MSG_HEATING_NOZZLE));
      tft_string.trim();
      tft.add_text(tft_string.center(320), tft_string.center(32), COLOR_WHITE, tft_string);

      tft.canvas(0, 104, 320, 32);
      tft.set_background(COLOR_BACKGROUND);
      tft_string.set(GET_TEXT_F(MSG_PLEASE_WAIT));
      tft_string.trim();
      tft.add_text(tft_string.center(320), tft_string.center(32), COLOR_WHITE, tft_string);

      tft.canvas(0, 136, 320, 32);
      tft.set_background(COLOR_BACKGROUND);
      tft_string.set(str_buf);
      tft_string.trim();
      tft.add_text(tft_string.center(320), tft_string.center(32), COLOR_WHITE, tft_string);
    }
    else if (currentTemperature >= 205) {
      ui.goto_screen(draw_unload_load_filament);
    }
  }
  ui.refresh(LCDVIEW_CALL_REDRAW_NEXT);
}

// Factory About screen
void menu_about() {
  ui.defer_status_screen();
  if (ui.should_draw()) {
    tft.canvas(0, 0, 50, TFT_HEIGHT);
    tft.set_background(COLOR_BACKGROUND);

    tft.canvas(0, 40, TFT_WIDTH, 30);
    tft.set_background(COLOR_BACKGROUND);
    tft_string.set(DEVICE_NAME);
    tft_string.trim();
    tft.add_text(tft_string.center(TFT_WIDTH), 5, COLOR_MENU_TEXT, tft_string);

    tft.canvas(0, 80, TFT_WIDTH, 30);
    tft.set_background(COLOR_BACKGROUND);
    tft_string.set(FIRMWARE_PORT " / " FIRMWARE_AUTHOR);
    tft_string.trim();
    tft.add_text(tft_string.center(TFT_WIDTH), 5, COLOR_MENU_TEXT, tft_string);

    tft.canvas(0, 120, TFT_WIDTH, 30);
    tft.set_background(COLOR_BACKGROUND);
    tft_string.set(BUILD_VOLUME);
    tft_string.trim();
    tft.add_text(tft_string.center(TFT_WIDTH), 5, COLOR_MENU_TEXT, tft_string);

    tft.canvas(0, 160, TFT_WIDTH, 30);
    tft.set_background(COLOR_BACKGROUND);
    tft_string.set(TECH_SUPPORT);
    tft_string.trim();
    tft.add_text(tft_string.center(TFT_WIDTH), 5, COLOR_MENU_TEXT, tft_string);
  }
  if (ui.use_click()) {
    ui.goto_previous_screen();
    ui.previous_callbackFunc();
  }
}

#if ENABLED(AUTO_BED_LEVELING_BILINEAR)

  // Factory 7x7 BILINEAR mesh viewer
  void menu_mesh_view() {
    ui.defer_status_screen();
    if (ui.should_draw()) {
      ui.flexible_clear_lcd(0, 0, TFT_WIDTH, TFT_HEIGHT);

      constexpr uint8_t cell = _MIN((TFT_WIDTH - 8) / (GRID_MAX_POINTS_X), (TFT_HEIGHT - 34) / (GRID_MAX_POINTS_Y));
      constexpr int16_t gx = _MAX(0, (TFT_WIDTH - (GRID_MAX_POINTS_X) * cell) / 2),
                        gy = 30;

      tft.canvas(0, 4, TFT_WIDTH, 24);
      tft.set_background(COLOR_BACKGROUND);
      tft_string.set(GET_TEXT_F(MSG_MESH_VIEWER));
      tft_string.trim();
      tft.add_text(tft_string.center(TFT_WIDTH), 4, COLOR_MENU_TEXT, tft_string);

      if (!bedlevel.mesh_is_valid()) {
        tft.canvas(0, gy + 70, TFT_WIDTH, 24);
        tft.set_background(COLOR_BACKGROUND);
        tft_string.set(GET_TEXT_F(MSG_NO_VALID_MESH));
        tft_string.trim();
        tft.add_text(tft_string.center(TFT_WIDTH), 4, COLOR_YELLOW, tft_string);
      }
      else {
        float zspan = 0.1f;
        for (uint8_t x = 0; x < GRID_MAX_POINTS_X; ++x)
          for (uint8_t y = 0; y < GRID_MAX_POINTS_Y; ++y) {
            const float z = bedlevel.z_values[x][y];
            if (!isnan(z)) {
              const float az = fabs(z);
              if (az > zspan) zspan = az;
            }
          }

        tft.canvas(gx, gy, (GRID_MAX_POINTS_X) * cell, (GRID_MAX_POINTS_Y) * cell);
        tft.set_background(COLOR_BACKGROUND);
        for (uint8_t x = 0; x < GRID_MAX_POINTS_X; ++x)
          for (uint8_t y = 0; y < GRID_MAX_POINTS_Y; ++y) {
            const float z = bedlevel.z_values[x][y];
            const uint16_t cx = x * cell, cy = ((GRID_MAX_POINTS_Y) - 1 - y) * cell;
            uint16_t color = COLOR_GREY;
            if (!isnan(z)) {
              const float t = z / zspan;
              color = t <= -0.5f ? COLOR_BLUE : t <= -0.15f ? COLOR_CYAN : t < 0.15f ? COLOR_LIME : t < 0.5f ? COLOR_YELLOW : COLOR_RED;

              char sbuf[2] = { z < 0 ? '-' : '+', 0 }, vbuf[4];
              const float az = _MIN(fabs(z), 99.0f);
              if (az < 1.0f) {
                const int f = int(az * 100.0f + 0.5f);
                if (f >= 100) { vbuf[0] = '1'; vbuf[1] = '.'; vbuf[2] = '0'; vbuf[3] = 0; }
                else { vbuf[0] = '.'; vbuf[1] = char('0' + f / 10); vbuf[2] = char('0' + f % 10); vbuf[3] = 0; }
              }
              else {
                const int t10 = _MIN(99, int(az * 10.0f + 0.5f));
                vbuf[0] = char('0' + t10 / 10); vbuf[1] = '.'; vbuf[2] = char('0' + t10 % 10); vbuf[3] = 0;
              }
              const uint16_t ty = cy >= 2 ? cy - 2 : 0;
              tft_string.set(sbuf);
              tft.add_text(cx + tft_string.center(cell), ty, COLOR_MENU_TEXT, tft_string);
              tft_string.set(vbuf);
              tft.add_text(cx + tft_string.center(cell), ty + 12, COLOR_MENU_TEXT, tft_string);
            }
            tft.add_rectangle(cx, cy, cell - 1, cell - 1, color);
          }
      }
    }
    if (ui.use_click()) {
      ui.goto_previous_screen();
      ui.previous_callbackFunc();
    }
  }

#endif

#endif // HAS_MARLINUI_MENU
