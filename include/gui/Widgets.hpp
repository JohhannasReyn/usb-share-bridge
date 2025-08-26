#pragma once

#include "lvgl.h"
#include <string>
#include <vector>
#include <functional>
#include "../core/StorageManager.hpp"

// File list widget for file explorer
class FileListWidget {
public:
    FileListWidget(lv_obj_t* parent);
    ~FileListWidget();
    
    void setFiles(const std::vector<FileInfo>& files);
    void setSelectionCallback(std::function<void(const FileInfo&)> callback);
    void refresh();
    void clearSelection();
    
    lv_obj_t* getWidget() const { return m_list; }

private:
    static void onItemClick(lv_event_t* e);
    void updateItem(lv_obj_t* item, const FileInfo& fileInfo);
    
    lv_obj_t* m_list;
    std::vector<FileInfo> m_files;
    std::function<void(const FileInfo&)> m_selectionCallback;
};

// Status indicator widget
class StatusWidget {
public:
    StatusWidget(lv_obj_t* parent);
    ~StatusWidget();
    
    void setUsbStatus(bool connected, int hostCount = 0);
    void setNetworkStatus(bool connected, const std::string& ssid = "");
    void setStorageStatus(bool mounted, uint64_t freeSpace = 0, uint64_t totalSpace = 0);
    
    lv_obj_t* getWidget() const { return m_container; }

private:
    lv_obj_t* m_container;
    lv_obj_t* m_usbIcon;
    lv_obj_t* m_usbLabel;
    lv_obj_t* m_networkIcon;
    lv_obj_t* m_networkLabel;
    lv_obj_t* m_storageIcon;
    lv_obj_t* m_storageLabel;
};

// Progress bar widget for operations
class ProgressWidget {
public:
    ProgressWidget(lv_obj_t* parent);
    ~ProgressWidget();
    
    void show(const std::string& operation);
    void hide();
    void setProgress(int percentage);
    void setText(const std::string& text);
    
    lv_obj_t* getWidget() const { return m_container; }

private:
    lv_obj_t* m_container;
    lv_obj_t* m_bar;
    lv_obj_t* m_label;
};
