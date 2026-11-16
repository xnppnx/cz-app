/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "apple_screen.h"

#include "asset_manager.h"
#include "bindings.h"

namespace screen {
namespace {

void status_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* label = lv_observer_get_target_obj(observer);
    if (!label) {
        return;
    }
    const char* text = lv_subject_get_string(subject);
    lv_label_set_text(label, text ? text : "");
}

} // namespace

AppleScreen::AppleScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void AppleScreen::build_content(lv_obj_t* content) {
    auto* column = lv_obj_create(content);
    lv_obj_remove_style_all(column);
    lv_obj_set_size(column, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(column, 6, 0);
    lv_obj_set_style_pad_row(column, 4, 0);
    lv_obj_clear_flag(column, LV_OBJ_FLAG_SCROLLABLE);

    auto* font = assets().load_standard_font(13, app::StandardFontWeight::Regular);
    auto* small_font = assets().load_standard_font(11, app::StandardFontWeight::Regular);

    // Notes list.
    auto* list = lv_label_create(column);
    lv_obj_set_width(list, LV_PCT(100));
    lv_label_set_long_mode(list, LV_LABEL_LONG_WRAP);
    lv_label_bind_text(list, view_model().notes_list_subject(), nullptr);
    lv_obj_set_style_text_font(list, font ? font : &lv_font_montserrat_14, 0);
    reactive::bind_theme(list, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    // Help / status line.
    auto* status = lv_label_create(column);
    lv_obj_set_width(status, LV_PCT(100));
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(status, small_font ? small_font : &lv_font_montserrat_12, 0);
    lv_subject_add_observer_obj(view_model().status_subject(), status_observer, status, nullptr);
    reactive::bind_theme(status, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* help = lv_label_create(column);
    lv_obj_set_width(help, LV_PCT(100));
    lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);
    lv_label_set_text(help, "N new  S open  D delete\nUp/Dn select  Hold ESC quit");
    lv_obj_set_style_text_font(help, small_font ? small_font : &lv_font_montserrat_12, 0);
    reactive::bind_theme(help, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
}

} // namespace screen
