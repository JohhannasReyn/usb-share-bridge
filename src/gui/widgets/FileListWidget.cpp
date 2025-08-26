#include "gui/Widgets.hpp"
#include "utils/FileUtils.hpp"
#include "utils/Logger.hpp"

FileListWidget::FileListWidget(lv_obj_t* parent)
    : m_list(nullptr)
{
    // Create list container
    m_list = lv_list_create(parent);
    lv_obj_set_size(m_list, 440, 180);
    lv_obj_set_style_bg_color(m_list, lv_color_white(), 0);
    lv_obj_set_style_border_width(m_list, 1, 0);
    lv_obj_set_style_border_color(m_list, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_radius(m_list, 4, 0);
}

FileListWidget::~FileListWidget() {
    if (m_list) {
        lv_obj_del(m_list);
    }
}

void FileListWidget::setFiles(const std::vector<FileInfo>& files) {
    m_files = files;
    refresh();
}

void FileListWidget::setSelectionCallback(std::function<void(const FileInfo&)> callback) {
    m_selectionCallback = callback;
}

void FileListWidget::refresh() {
    if (!m_list) {
        return;
    }
    
    // Clear existing items
    lv_obj_clean(m_list);
    
    if (m_files.empty()) {
        lv_obj_t* item = lv_list_add_text(m_list, "No files found");
        lv_obj_set_style_text_color(item, lv_color_hex(0x757575), 0);
        return;
    }
    
    // Sort files: directories first, then alphabetically
    std::vector<FileInfo> sortedFiles = m_files;
    std::sort(sortedFiles.begin(), sortedFiles.end(), [](const FileInfo& a, const FileInfo& b) {
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory; // Directories first
        }
        return a.name < b.name; // Alphabetical
    });
    
    // Add file items
    for (size_t i = 0; i < sortedFiles.size(); i++) {
        const FileInfo& file = sortedFiles[i];
        
        // Create list item
        lv_obj_t* item;
        if (file.isDirectory) {
            item = lv_list_add_btn(m_list, LV_SYMBOL_DIRECTORY, file.name.c_str());
        } else {
            const char* icon = LV_SYMBOL_FILE;
            if (FileUtils::isImageFile(file.path)) {
                icon = LV_SYMBOL_IMAGE;
            } else if (FileUtils::isVideoFile(file.path)) {
                icon = LV_SYMBOL_VIDEO;
            } else if (FileUtils::isAudioFile(file.path)) {
                icon = LV_SYMBOL_AUDIO;
            }
            item = lv_list_add_btn(m_list, icon, file.name.c_str());
        }
        
        // Set item styling
        lv_obj_set_style_text_color(item, lv_color_hex(0x212121), 0);
        lv_obj_set_style_bg_color(item, lv_color_white(), 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xF5F5F5), LV_STATE_PRESSED);
        
        // Store file index in user data
        lv_obj_set_user_data(item, (void*)i);
        
        // Add click event
        lv_obj_add_event_cb(item, onItemClick, LV_EVENT_CLICKED, this);
        
        // Add file details as a secondary label
        if (!file.isDirectory) {
            lv_obj_t* detailsLabel = lv_label_create(item);
            std::string details = FileUtils::formatFileSize(file.size) + " • " + 
                                 FileUtils::formatTime(file.lastModified);
            lv_label_set_text(detailsLabel, details.c_str());
            lv_obj_set_style_text_color(detailsLabel, lv_color_hex(0x757575), 0);
            lv_obj_set_style_text_font(detailsLabel, &lv_font_montserrat_10, 0);
            lv_obj_align(detailsLabel, LV_ALIGN_BOTTOM_LEFT, 35, -5);
        }
    }
}

void FileListWidget::clearSelection() {
    // Clear any visual selection state
    if (m_list) {
        lv_obj_clear_state(m_list, LV_STATE_FOCUSED);
    }
}

void FileListWidget::onItemClick(lv_event_t* e) {
    lv_obj_t* item = lv_event_get_target(e);
    FileListWidget* widget = static_cast<FileListWidget*>(lv_event_get_user_data(e));
    
    if (!widget || !item) {
        return;
    }
    
    // Get file index from user data
    size_t fileIndex = (size_t)lv_obj_get_user_data(item);
    
    if (fileIndex < widget->m_files.size() && widget->m_selectionCallback) {
        widget->m_selectionCallback(widget->m_files[fileIndex]);
    }
}

void FileListWidget::updateItem(lv_obj_t* item, const FileInfo& fileInfo) {
    if (!item) {
        return;
    }
    
    // Update icon and text based on file type
    const char* icon = fileInfo.isDirectory ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE;
    if (!fileInfo.isDirectory) {
        if (FileUtils::isImageFile(fileInfo.path)) {
            icon = LV_SYMBOL_IMAGE;
        } else if (FileUtils::isVideoFile(fileInfo.path)) {
            icon = LV_SYMBOL_VIDEO;
        } else if (FileUtils::isAudioFile(fileInfo.path)) {
            icon = LV_SYMBOL_AUDIO;
        }
    }
    
    // Update the button text (this is simplified - real implementation would need more work)
    lv_obj_t* label = lv_obj_get_child(item, 0);
    if (label) {
        std::string text = std::string(icon) + " " + fileInfo.name;
        lv_label_set_text(label, text.c_str());
    }
}
