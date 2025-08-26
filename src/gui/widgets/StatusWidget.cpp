#include "gui/Widgets.hpp"
#include "utils/FileUtils.hpp"

StatusWidget::StatusWidget(lv_obj_t* parent)
{
    // Create main container
    m_container = lv_obj_create(parent);
    lv_obj_set_size(m_container, 440, 80);
    lv_obj_set_style_bg_color(m_container, lv_color_hex(0xF8F8F8), 0);
    lv_obj_set_style_border_width(m_container, 1, 0);
    lv_obj_set_style_border_color(m_container, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_radius(m_container, 6, 0);
    lv_obj_set_style_pad_all(m_container, 10, 0);
    lv_obj_clear_flag(m_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // USB Status
    m_usbIcon = lv_label_create(m_container);
    lv_label_set_text(m_usbIcon, LV_SYMBOL_USB);
    lv_obj_set_pos(m_usbIcon, 10, 10);
    lv_obj_set_style_text_color(m_usbIcon, lv_color_hex(0x757575), 0);
    
    m_usbLabel = lv_label_create(m_container);
    lv_label_set_text(m_usbLabel, "USB: Disconnected");
    lv_obj_set_pos(m_usbLabel, 35, 10);
    lv_obj_set_style_text_font(m_usbLabel, &lv_font_montserrat_12, 0);
    
    // Network Status
    m_networkIcon = lv_label_create(m_container);
    lv_label_set_text(m_networkIcon, LV_SYMBOL_WIFI);
    lv_obj_set_pos(m_networkIcon, 200, 10);
    lv_obj_set_style_text_color(m_networkIcon, lv_color_hex(0x757575), 0);
    
    m_networkLabel = lv_label_create(m_container);
    lv_label_set_text(m_networkLabel, "Network: Offline");
    lv_obj_set_pos(m_networkLabel, 225, 10);
    lv_obj_set_style_text_font(m_networkLabel, &lv_font_montserrat_12, 0);
    
    // Storage Status
    m_storageIcon = lv_label_create(m_container);
    lv_label_set_text(m_storageIcon, LV_SYMBOL_SD_CARD);
    lv_obj_set_pos(m_storageIcon, 10, 40);
    lv_obj_set_style_text_color(m_storageIcon, lv_color_hex(0x757575), 0);
    
    m_storageLabel = lv_label_create(m_container);
    lv_label_set_text(m_storageLabel, "Storage: No device");
    lv_obj_set_pos(m_storageLabel, 35, 40);
    lv_obj_set_style_text_font(m_storageLabel, &lv_font_montserrat_12, 0);
}

StatusWidget::~StatusWidget() {
    if (m_container) {
        lv_obj_del(m_container);
    }
}

void StatusWidget::setUsbStatus(bool connected, int hostCount) {
    if (!m_usbIcon || !m_usbLabel) {
        return;
    }
    
    if (connected) {
        lv_obj_set_style_text_color(m_usbIcon, lv_color_hex(0x4CAF50), 0); // Green
        std::string text = "USB: " + std::to_string(hostCount) + " host(s) connected";
        lv_label_set_text(m_usbLabel, text.c_str());
        lv_obj_set_style_text_color(m_usbLabel, lv_color_hex(0x4CAF50), 0);
    } else {
        lv_obj_set_style_text_color(m_usbIcon, lv_color_hex(0x757575), 0); // Gray
        lv_label_set_text(m_usbLabel, "USB: Disconnected");
        lv_obj_set_style_text_color(m_usbLabel, lv_color_hex(0x757575), 0);
    }
}

void StatusWidget::setNetworkStatus(bool connected, const std::string& ssid) {
    if (!m_networkIcon || !m_networkLabel) {
        return;
    }
    
    if (connected) {
        lv_obj_set_style_text_color(m_networkIcon, lv_color_hex(0x2196F3), 0); // Blue
        std::string text = "Network: Connected";
        if (!ssid.empty()) {
            text += " (" + ssid + ")";
        }
        lv_label_set_text(m_networkLabel, text.c_str());
        lv_obj_set_style_text_color(m_networkLabel, lv_color_hex(0x2196F3), 0);
    } else {
        lv_obj_set_style_text_color(m_networkIcon, lv_color_hex(0x757575), 0); // Gray
        lv_label_set_text(m_networkLabel, "Network: Offline");
        lv_obj_set_style_text_color(m_networkLabel, lv_color_hex(0x757575), 0);
    }
}

void StatusWidget::setStorageStatus(bool mounted, uint64_t freeSpace, uint64_t totalSpace) {
    if (!m_storageIcon || !m_storageLabel) {
        return;
    }
    
    if (mounted) {
        lv_obj_set_style_text_color(m_storageIcon, lv_color_hex(0xFF9800), 0); // Orange
        std::string text = "Storage: " + FileUtils::formatFileSize(freeSpace) + " free";
        if (totalSpace > 0) {
            text += " / " + FileUtils::formatFileSize(totalSpace);
        }
        lv_label_set_text(m_storageLabel, text.c_str());
        lv_obj_set_style_text_color(m_storageLabel, lv_color_hex(0xFF9800), 0);
    } else {
        lv_obj_set_style_text_color(m_storageIcon, lv_color_hex(0x757575), 0); // Gray
        lv_label_set_text(m_storageLabel, "Storage: No device");
        lv_obj_set_style_text_color(m_storageLabel, lv_color_hex(0x757575), 0);
    }
}
