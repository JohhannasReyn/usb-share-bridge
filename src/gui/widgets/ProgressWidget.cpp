#include "gui/Widgets.hpp"

ProgressWidget::ProgressWidget(lv_obj_t* parent)
{
    // Create container
    m_container = lv_obj_create(parent);
    lv_obj_set_size(m_container, 300, 80);
    lv_obj_set_style_bg_color(m_container, lv_color_white(), 0);
    lv_obj_set_style_border_width(m_container, 2, 0);
    lv_obj_set_style_border_color(m_container, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_radius(m_container, 8, 0);
    lv_obj_set_style_shadow_width(m_container, 10, 0);
    lv_obj_set_style_shadow_spread(m_container, 2, 0);
    lv_obj_set_style_shadow_color(m_container, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(m_container, 30, 0);
    lv_obj_clear_flag(m_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(m_container);
    
    // Initially hidden
    lv_obj_add_flag(m_container, LV_OBJ_FLAG_HIDDEN);
    
    // Progress bar
    m_bar = lv_bar_create(m_container);
    lv_obj_set_size(m_bar, 260, 20);
    lv_obj_set_pos(m_bar, 20, 40);
    lv_obj_set_style_bg_color(m_bar, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_bg_color(m_bar, lv_color_hex(0x2196F3), LV_PART_INDICATOR);
    lv_obj_set_style_radius(m_bar, 10, 0);
    lv_bar_set_range(m_bar, 0, 100);
    lv_bar_set_value(m_bar, 0, LV_ANIM_OFF);
    
    // Text label
    m_label = lv_label_create(m_container);
    lv_label_set_text(m_label, "Operation in progress...");
    lv_obj_set_style_text_align(m_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(m_label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(m_label, 20, 10);
    lv_obj_set_size(m_label, 260, 25);
}

ProgressWidget::~ProgressWidget() {
    if (m_container) {
        lv_obj_del(m_container);
    }
}

void ProgressWidget::show(const std::string& operation) {
    if (!m_container || !m_label) {
        return;
    }
    
    setText(operation);
    setProgress(0);
    lv_obj_clear_flag(m_container, LV_OBJ_FLAG_HIDDEN);
    
    // Bring to front
    lv_obj_move_foreground(m_container);
}

void ProgressWidget::hide() {
    if (!m_container) {
        return;
    }
    
    lv_obj_add_flag(m_container, LV_OBJ_FLAG_HIDDEN);
}

void ProgressWidget::setProgress(int percentage) {
    if (!m_bar) {
        return;
    }
    
    if (percentage < 0) percentage = 0;
    if (percentage > 100) percentage = 100;
    
    lv_bar_set_value(m_bar, percentage, LV_ANIM_ON);
}

void ProgressWidget::setText(const std::string& text) {
    if (!m_label) {
        return;
    }
    
    lv_label_set_text(m_label, text.c_str());
}
