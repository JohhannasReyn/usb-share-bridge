#pragma once

#include "../Screen.hpp"
#include "../Widgets.hpp"
#include "../../core/StorageManager.hpp"
#include <memory>

class ScreenFileExplorer : public Screen {
public:
    ScreenFileExplorer(const std::string& name, UsbBridge* bridge);
    
    bool create() override;
    void show() override;

private:
    void createNavigationBar();
    void createControlButtons();
    void refreshFileList();
    void onFileSelected(const FileInfo& file);
    void showFileDetails(const FileInfo& file);
    void navigateUp();
    
    std::string m_currentPath;
    std::unique_ptr<FileListWidget> m_fileListWidget;
    lv_obj_t* m_backButton;
    lv_obj_t* m_pathLabel;
    lv_obj_t* m_infoLabel;
};
