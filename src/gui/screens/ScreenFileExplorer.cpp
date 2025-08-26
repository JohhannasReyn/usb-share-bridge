#include "gui/screens/ScreenFileExplorer.hpp"
#include "core/UsbBridge.hpp"
#include "utils/Logger.hpp"
#include "utils/FileUtils.hpp"

ScreenFileExplorer::ScreenFileExplorer(const std::string& name, UsbBridge* bridge)
    : Screen(name, bridge)
    , m_currentPath("")
{
}

bool ScreenFileExplorer::create() {
    m_container = lv_obj_create(nullptr);
    lv_obj_set_size(m_container, 480, 290);
    lv_obj_set_pos(m_container, 0, 30);
    lv_obj_set_style_bg_color(m_container, lv_color_hex(0xFFFFFF), 0);
    
    // Navigation bar
    createNavigationBar();
    
    // File list
    m_fileListWidget = std::make_unique<FileListWidget>(m_container);
    lv_obj_set_pos(m_fileListWidget->getWidget(), 10, 50);
    lv_obj_set_size(m_fileListWidget->getWidget(), 460, 190);
    
    m_fileListWidget->setSelectionCallback([this](const FileInfo& file) {
        onFileSelected(file);
    });
    
    // Control buttons
    createControlButtons();
    
    return true;
}

void ScreenFileExplorer::createNavigationBar() {
    // Back button
    m_backButton = lv_btn_create(m_container);
    lv_obj_set_size(m_backButton, 50, 30);
    lv_obj_set_pos(m_backButton, 10, 10);
    lv_obj_add_event_cb(m_backButton, [](lv_event_t* e) {
        ScreenFileExplorer* screen = static_cast<ScreenFileExplorer*>(lv_event_get_user_data(e));
        screen->navigateUp();
    }, LV_EVENT_CLICKED, this);
    
    lv_obj_t* backLabel = lv_label_create(m_backButton);
    lv_label_set_text(backLabel, LV_SYMBOL_LEFT);
    lv_obj_center(backLabel);
    
    // Path label
    m_pathLabel = lv_label_create(m_container);
    lv_obj_set_pos(m_pathLabel, 70, 15);
    lv_obj_set_size(m_pathLabel, 350, 20);
    lv_label_set_text(m_pathLabel, "/");
    
    // Home button
    lv_obj_t* homeButton = lv_btn_create(m_container);
    lv_obj_set_size(homeButton, 50, 30);
    lv_obj_set_pos(homeButton, 420, 10);
    lv_obj_add_event_cb(homeButton, [](lv_event_t* e) {
        ScreenFileExplorer* screen = static_cast<ScreenFileExplorer*>(lv_event_get_user_data(e));
        screen->navigateToScreen("home");
    }, LV_EVENT_CLICKED, this);
    
    lv_obj_t* homeLabel = lv_label_create(homeButton);
    lv_label_set_text(homeLabel, LV_SYMBOL_HOME);
    lv_obj_center(homeLabel);
}

void ScreenFileExplorer::createControlButtons() {
    // Refresh button
    lv_obj_t* refreshButton = lv_btn_create(m_container);
    lv_obj_set_size(refreshButton, 80, 30);
    lv_obj_set_pos(refreshButton, 10, 250);
    lv_obj_add_event_cb(refreshButton, [](lv_event_t* e) {
        ScreenFileExplorer* screen = static_cast<ScreenFileExplorer*>(lv_event_get_user_data(e));
        screen->refreshFileList();
    }, LV_EVENT_CLICKED, this);
    
    lv_obj_t* refreshLabel = lv_label_create(refreshButton);
    lv_label_set_text(refreshLabel, "Refresh");
    lv_obj_center(refreshLabel);
    
    // Info label for selected file
    m_infoLabel = lv_label_create(m_container);
    lv_obj_set_pos(m_infoLabel, 100, 250);
    lv_obj_set_size(m_infoLabel, 370, 30);
    lv_label_set_text(m_infoLabel, "Select a file to view details");
    lv_obj_set_style_text_color(m_infoLabel, lv_color_hex(0x666666), 0);
}

void ScreenFileExplorer::show() {
    Screen::show();
    refreshFileList();
}

void ScreenFileExplorer::refreshFileList() {
    if (!m_bridge || !m_bridge->getStorageManager()->isDriveConnected()) {
        showError("No storage device connected");
        return;
    }
    
    auto files = m_bridge->getStorageManager()->listDirectory(m_currentPath);
    m_fileListWidget->setFiles(files);
    
    // Update path display
    std::string displayPath = m_currentPath.empty() ? "/" : "/" + m_currentPath;
    lv_label_set_text(m_pathLabel, displayPath.c_str());
    
    // Enable/disable back button
    lv_obj_set_state(m_backButton, m_currentPath.empty() ? LV_STATE_DISABLED : LV_STATE_DEFAULT);
    
    LOG_INFO("File list refreshed for path: " + displayPath, "GUI");
}

void ScreenFileExplorer::onFileSelected(const FileInfo& file) {
    if (file.isDirectory) {
        // Navigate into directory
        if (m_currentPath.empty()) {
            m_currentPath = file.name;
        } else {
            m_currentPath = FileUtils::joinPath(m_currentPath, file.name);
        }
        refreshFileList();
    } else {
        // Show file details
        showFileDetails(file);
    }
}

void ScreenFileExplorer::showFileDetails(const FileInfo& file) {
    std::string details = "File: " + file.name + "\n";
    details += "Size: " + FileUtils::formatFileSize(file.size) + "\n";
    details += "Type: " + file.mimeType + "\n";
    details += "Modified: " + FileUtils::formatTime(file.lastModified);
    
    lv_label_set_text(m_infoLabel, details.c_str());
}

void ScreenFileExplorer::navigateUp() {
    if (m_currentPath.empty()) {
        return;
    }
    
    size_t lastSlash = m_currentPath.find_last_of('/');
    if (lastSlash == std::string::npos) {
        m_currentPath.clear();
    } else {
        m_currentPath = m_currentPath.substr(0, lastSlash);
    }
    
    refreshFileList();
}