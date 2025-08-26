#pragma once

#include <map>
#include <string>
#include <atomic>
#include <thread>
#include <functional>

class HttpServer {
public:
    HttpServer();
    ~HttpServer();

    bool initialize(int port = 8080);
    bool start();
    bool stop();
    bool isRunning() const { return m_running; }
    
    void setDocumentRoot(const std::string& path) { m_documentRoot = path; }
    void setPort(int port) { m_port = port; }
    
    // REST API endpoints
    void addApiEndpoint(const std::string& path, std::function<std::string(const std::string&)> handler);
    
    // File serving
    void enableDirectoryListing(bool enable) { m_directoryListing = enable; }
    void enableFileDownload(bool enable) { m_fileDownload = enable; }
    
    // Statistics
    int getActiveConnections() const;
    uint64_t getRequestCount() const;

private:
    void serverLoop();
    std::string handleRequest(const std::string& request);
    std::string serveFile(const std::string& path);
    std::string listDirectory(const std::string& path);
    std::string generateApiResponse(const std::string& endpoint, const std::string& data, int);
    
    std::atomic<bool> m_running;
    std::thread m_serverThread;
    int m_port;
    std::string m_documentRoot;
    std::atomic<bool> m_directoryListing;
    std::atomic<bool> m_fileDownload;
    
    std::map<std::string, std::function<std::string(const std::string&)>> m_apiHandlers;
};