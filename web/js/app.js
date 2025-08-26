class USBBridgeWebUI {
    constructor() {
        this.currentPath = '/';
        this.refreshInterval = null;
        this.init();
    }

    init() {
        this.setupEventListeners();
        this.refreshStatus();
        this.refreshFiles();
        this.refreshActivity();
        
        // Auto-refresh every 30 seconds
        this.refreshInterval = setInterval(() => {
            this.refreshStatus();
            this.refreshActivity();
        }, 30000);
    }

    setupEventListeners() {
        // Handle modal close on background click
        document.addEventListener('click', (e) => {
            if (e.target.classList.contains('modal')) {
                this.closeModal(e.target.id);
            }
        });

        // Handle keyboard shortcuts
        document.addEventListener('keydown', (e) => {
            if (e.key === 'Escape') {
                const modals = document.querySelectorAll('.modal.show');
                modals.forEach(modal => this.closeModal(modal.id));
            }
        });
    }

    async apiCall(endpoint, options = {}) {
        try {
            const response = await fetch(`/api/${endpoint}`, {
                headers: {
                    'Content-Type': 'application/json',
                    ...options.headers
                },
                ...options
            });

            if (!response.ok) {
                throw new Error(`HTTP error! status: ${response.status}`);
            }

            return await response.json();
        } catch (error) {
            console.error(`API call failed for ${endpoint}:`, error);
            throw error;
        }
    }

    async refreshStatus() {
        try {
            const status = await this.apiCall('status');
            this.updateStatusDisplay(status);
        } catch (error) {
            console.error('Failed to refresh status:', error);
            this.showError('Failed to connect to USB Bridge system');
        }
    }

    updateStatusDisplay(status) {
        // Update status indicators in header
        const usbStatus = document.getElementById('usbStatus');
        const networkStatus = document.getElementById('networkStatus');
        const storageStatus = document.getElementById('storageStatus');

        const usbDot = usbStatus.querySelector('.status-dot');
        const networkDot = networkStatus.querySelector('.status-dot');
        const storageDot = storageStatus.querySelector('.status-dot');

        // USB status
        usbDot.className = `status-dot ${status.usb?.connected ? 'online' : 'offline'}`;
        
        // Network status
        networkDot.className = `status-dot ${status.network?.connected ? 'online' : 'offline'}`;
        
        // Storage status
        storageDot.className = `status-dot ${status.storage?.mounted ? 'online' : 'offline'}`;

        // Update dashboard cards
        document.getElementById('usbHostCount').textContent = status.usb?.hostCount || 0;
        document.getElementById('networkStatusText').textContent = 
            status.network?.connected ? 'Connected' : 'Offline';
        document.getElementById('storageCapacity').textContent = 
            this.formatBytes(status.storage?.freeSpace || 0);
        document.getElementById('systemUptime').textContent = 
            this.formatUptime(status.system?.uptime || 0);
    }

    async refreshFiles() {
        try {
            const response = await this.apiCall('files', {
                headers: { 'X-Path': this.currentPath }
            });
            
            this.updateFileDisplay(response.files || []);
        } catch (error) {
            console.error('Failed to refresh files:', error);
            this.updateFileDisplay([]);
        }
    }

    updateFileDisplay(files) {
        const fileList = document.getElementById('fileList');
        const currentPathEl = document.getElementById('currentPath');
        const backBtn = document.getElementById('backBtn');

        currentPathEl.textContent = this.currentPath || '/';
        backBtn.disabled = this.currentPath === '/' || this.currentPath === '';

        if (files.length === 0) {
            fileList.innerHTML = '<div class="loading">No files found</div>';
            return;
        }

        // Sort files: directories first, then by name
        files.sort((a, b) => {
            if (a.is_directory !== b.is_directory) {
                return a.is_directory ? -1 : 1;
            }
            return a.name.localeCompare(b.name);
        });

        const fileItems = files.map(file => {
            const icon = file.is_directory ? 
                `<svg class="file-icon" viewBox="0 0 24 24" fill="currentColor">
                    <path d="M10 4H4c-1.11 0-2 .89-2 2v12c0 1.11.89 2 2 2h16c1.11 0 2-.89 2-2V8c0-1.11-.89-2-2-2h-8l-2-2z"/>
                </svg>` :
                `<svg class="file-icon" viewBox="0 0 24 24" fill="currentColor">
                    <path d="M6,2C4.89,2 4,2.89 4,4V20A2,2 0 0,0 6,22H18A2,2 0 0,0 20,20V8L14,2H6Z"/>
                </svg>`;

            const details = file.is_directory ? 
                'Directory' : 
                `${this.formatBytes(file.size)} • ${this.formatDate(file.modified)}`;

            return `
                <div class="file-item" onclick="window.usbBridgeUI.handleFileClick('${file.name}', ${file.is_directory})">
                    ${icon}
                    <div class="file-info">
                        <div class="file-name">${this.escapeHtml(file.name)}</div>
                        <div class="file-details">${details}</div>
                    </div>
                </div>
            `;
        }).join('');

        fileList.innerHTML = fileItems;
    }

    handleFileClick(filename, isDirectory) {
        if (isDirectory) {
            this.navigateToPath(this.joinPath(this.currentPath, filename));
        } else {
            this.downloadFile(this.joinPath(this.currentPath, filename));
        }
    }

    navigateToPath(path) {
        this.currentPath = path;
        this.refreshFiles();
    }

    navigateUp() {
        if (this.currentPath === '/' || this.currentPath === '') {
            return;
        }

        const pathParts = this.currentPath.split('/').filter(p => p);
        pathParts.pop();
        this.currentPath = pathParts.length ? '/' + pathParts.join('/') : '/';
        this.refreshFiles();
    }

    downloadFile(filePath) {
        window.open(`/files${filePath}`, '_blank');
    }

    async refreshActivity() {
        try {
            const response = await this.apiCall('activity');
            this.updateActivityDisplay(response.events || []);
        } catch (error) {
            console.error('Failed to refresh activity:', error);
            this.updateActivityDisplay([]);
        }
    }

    updateActivityDisplay(events) {
        const activityList = document.getElementById('activityList');

        if (events.length === 0) {
            activityList.innerHTML = '<div class="loading">No recent activity</div>';
            return;
        }

        const activityItems = events.slice(0, 10).map(event => {
            const icon = this.getActivityIcon(event.type);
            const action = this.getActivityAction(event.type);
            const time = this.formatTime(event.timestamp);

            return `
                <div class="activity-item">
                    ${icon}
                    <div class="activity-content">
                        <div class="activity-action">${action}</div>
                        <div class="activity-file">${this.escapeHtml(event.path)}</div>
                    </div>
                    <div class="activity-time">${time}</div>
                </div>
            `;
        }).join('');

        activityList.innerHTML = activityItems;
    }

    getActivityIcon(type) {
        const icons = {
            'created': `<svg class="activity-icon" viewBox="0 0 24 24" fill="currentColor">
                <path d="M19,13H13V19H11V13H5V11H11V5H13V11H19V13Z"/>
            </svg>`,
            'modified': `<svg class="activity-icon" viewBox="0 0 24 24" fill="currentColor">
                <path d="M20.71,7.04C21.1,6.65 21.1,6 20.71,5.63L18.37,3.29C18,2.9 17.35,2.9 16.96,3.29L15.12,5.12L18.87,8.87M3,17.25V21H6.75L17.81,9.93L14.06,6.18L3,17.25Z"/>
            </svg>`,
            'deleted': `<svg class="activity-icon" viewBox="0 0 24 24" fill="currentColor">
                <path d="M19,4H15.5L14.5,3H9.5L8.5,4H5V6H19M6,19A2,2 0 0,0 8,21H16A2,2 0 0,0 18,19V7H6V19Z"/>
            </svg>`,
            'moved': `<svg class="activity-icon" viewBox="0 0 24 24" fill="currentColor">
                <path d="M13,9V15L16,12M8,12L11,9V15M2,12A10,10 0 0,1 12,2A10,10 0 0,1 22,12A10,10 0 0,1 12,22A10,10 0 0,1 2,12Z"/>
            </svg>`
        };
        return icons[type] || icons['modified'];
    }

    getActivityAction(type) {
        const actions = {
            'created': 'Created',
            'modified': 'Modified',
            'deleted': 'Deleted',
            'moved': 'Moved'
        };
        return actions[type] || 'Modified';
    }

    filterActivity() {
        const filter = document.getElementById('activityFilter').value;
        // Implementation would filter the activity list
        // For now, just refresh to keep it simple
        this.refreshActivity();
    }

    // Settings functionality
    showSettings() {
        this.loadSettings();
        this.showModal('settingsModal');
    }

    async loadSettings() {
        try {
            const settings = await this.apiCall('settings');
            
            document.getElementById('smbEnabled').checked = settings.network?.smb?.enabled || false;
            document.getElementById('httpEnabled').checked = settings.network?.http?.enabled || false;
            document.getElementById('usbHost1Enabled').checked = settings.usb?.host1?.enabled || false;
            document.getElementById('usbHost2Enabled').checked = settings.usb?.host2?.enabled || false;
        } catch (error) {
            console.error('Failed to load settings:', error);
        }
    }

    async saveSettings() {
        try {
            const settings = {
                network: {
                    smb: { enabled: document.getElementById('smbEnabled').checked },
                    http: { enabled: document.getElementById('httpEnabled').checked }
                },
                usb: {
                    host1: { enabled: document.getElementById('usbHost1Enabled').checked },
                    host2: { enabled: document.getElementById('usbHost2Enabled').checked }
                }
            };

            await this.apiCall('settings', {
                method: 'POST',
                body: JSON.stringify(settings)
            });

            this.showSuccess('Settings saved successfully');
            this.closeModal('settingsModal');
            this.refreshStatus();
        } catch (error) {
            console.error('Failed to save settings:', error);
            this.showError('Failed to save settings');
        }
    }

    // Modal functionality
    showModal(modalId) {
        const modal = document.getElementById(modalId);
        if (modal) {
            modal.classList.add('show');
        }
    }

    closeModal(modalId) {
        const modal = document.getElementById(modalId);
        if (modal) {
            modal.classList.remove('show');
        }
    }

    // Additional modal functions
    showLogs() {
        // Implementation for showing system logs
        alert('Logs viewer would be implemented here');
    }

    showAbout() {
        // Implementation for showing about dialog
        alert('USB Bridge System v1.0.0\nBuilt for BigTechTree Pi V1.2.1');
    }

    // Utility functions
    formatBytes(bytes) {
        if (bytes === 0) return '0 B';
        const k = 1024;
        const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
        const i = Math.floor(Math.log(bytes) / Math.log(k));
        return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
    }

    formatUptime(seconds) {
        const hours = Math.floor(seconds / 3600);
        const minutes = Math.floor((seconds % 3600) / 60);
        const secs = seconds % 60;
        return `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
    }

    formatDate(timestamp) {
        return new Date(timestamp * 1000).toLocaleDateString();
    }

    formatTime(timestamp) {
        return new Date(timestamp).toLocaleTimeString();
    }

    joinPath(path1, path2) {
        const p1 = path1.endsWith('/') ? path1.slice(0, -1) : path1;
        const p2 = path2.startsWith('/') ? path2.slice(1) : path2;
        return p1 === '' ? '/' + p2 : p1 + '/' + p2;
    }

    escapeHtml(text) {
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    }

    showSuccess(message) {
        this.showNotification(message, 'success');
    }

    showError(message) {
        this.showNotification(message, 'error');
    }

    showNotification(message, type = 'info') {
        // Simple notification system
        const notification = document.createElement('div');
        notification.className = `notification ${type}`;
        notification.textContent = message;
        notification.style.cssText = `
            position: fixed;
            top: 20px;
            right: 20px;
            padding: 1rem 1.5rem;
            border-radius: 6px;
            color: white;
            font-weight: 500;
            z-index: 1100;
            max-width: 400px;
            box-shadow: 0 4px 12px rgba(0,0,0,0.15);
            animation: slideIn 0.3s ease;
        `;

        if (type === 'success') {
            notification.style.background = '#4CAF50';
        } else if (type === 'error') {
            notification.style.background = '#F44336';
        } else {
            notification.style.background = '#2196F3';
        }

        document.body.appendChild(notification);

        setTimeout(() => {
            notification.style.animation = 'slideOut 0.3s ease';
            setTimeout(() => {
                document.body.removeChild(notification);
            }, 300);
        }, 3000);
    }
}

// Global functions for onclick handlers
window.refreshStatus = () => window.usbBridgeUI.refreshStatus();
window.refreshFiles = () => window.usbBridgeUI.refreshFiles();
window.navigateUp = () => window.usbBridgeUI.navigateUp();
window.filterActivity = () => window.usbBridgeUI.filterActivity();
window.showSettings = () => window.usbBridgeUI.showSettings();
window.showLogs = () => window.usbBridgeUI.showLogs();
window.showAbout = () => window.usbBridgeUI.showAbout();
window.saveSettings = () => window.usbBridgeUI.saveSettings();
window.closeModal = (modalId) => window.usbBridgeUI.closeModal(modalId);

// CSS animations
const style = document.createElement('style');
style.textContent = `
    @keyframes slideIn {
        from {
            transform: translateX(100%);
            opacity: 0;
        }
        to {
            transform: translateX(0);
            opacity: 1;
        }
    }

    @keyframes slideOut {
        from {
            transform: translateX(0);
            opacity: 1;
        }
        to {
            transform: translateX(100%);
            opacity: 0;
        }
    }
`;
document.head.appendChild(style);

// Initialize the application when DOM is loaded
document.addEventListener('DOMContentLoaded', () => {
    window.usbBridgeUI = new USBBridgeWebUI();
});