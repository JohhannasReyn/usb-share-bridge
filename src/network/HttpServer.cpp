#include "network/HttpServer.hpp"
#include "utils/Logger.hpp"
#include "utils/FileUtils.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>
#include <fstream>
#include <filesystem>

HttpServer::HttpServer()
    : m_running(false)
    , m_port(8080)
    , m_directoryListing(true)
    , m_fileDownload(true)
{
}

HttpServer::~HttpServer() {
    stop();
}

bool HttpServer::initialize(int port) {
    m_port = port;
    
    // Add default API endpoints
    addApiEndpoint("/status", [this](const std::string& data) {
        return generateApiResponse("/status", R"({"status": "online", "server": "USB Bridge HTTP"})", 500);
    });
    
    addApiEndpoint("/files", [this](const std::string& data) {
        // Extract path from headers (simplified)
        std::string path = "/";
        return listDirectory(m_documentRoot + path);
    });
    
    LOG_INFO("HTTP server initialized on port " + std::to_string(m_port), "HTTP");
    return true;
}

bool HttpServer::start() {
    if (m_running) {
        return true;
    }
    
    LOG_INFO("Starting HTTP server", "HTTP");
    
    m_running = true;
    m_serverThread = std::thread(&HttpServer::serverLoop, this);
    
    LOG_INFO("HTTP server started on port " + std::to_string(m_port), "HTTP");
    return true;
}

bool HttpServer::stop() {
    if (!m_running) {
        return true;
    }
    
    LOG_INFO("Stopping HTTP server", "HTTP");
    
    m_running = false;
    
    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }
    
    LOG_INFO("HTTP server stopped", "HTTP");
    return true;
}

void HttpServer::addApiEndpoint(const std::string& path, std::function<std::string(const std::string&)> handler) {
    m_apiHandlers[path] = handler;
}

int HttpServer::getActiveConnections() const {
    // For simplicity, return 0. In a real implementation, this would track active connections
    return 0;
}

uint64_t HttpServer::getRequestCount() const {
    // For simplicity, return 0. In a real implementation, this would track requests
    return 0;
}

void HttpServer::serverLoop() {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        LOG_ERROR("Failed to create server socket", "HTTP");
        return;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(m_port);
    
    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        LOG_ERROR("Failed to bind server socket", "HTTP");
        close(serverSocket);
        return;
    }
    
    if (listen(serverSocket, 10) < 0) {
        LOG_ERROR("Failed to listen on server socket", "HTTP");
        close(serverSocket);
        return;
    }
    
    // Set non-blocking
    fcntl(serverSocket, F_SETFL, O_NONBLOCK);
    
    while (m_running) {
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        
        int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientLen);
        if (clientSocket < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        // Handle request in a separate thread for simplicity
        std::thread([this, clientSocket]() {
            char buffer[4096];
            int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
            
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                std::string request(buffer);
                std::string response = handleRequest(request);
                
                send(clientSocket, response.c_str(), response.length(), 0);
            }
            
            close(clientSocket);
        }).detach();
    }
    
    close(serverSocket);
}

std::string HttpServer::handleRequest(const std::string& request) {
    std::istringstream iss(request);
    std::string method, path, protocol;
    iss >> method >> path >> protocol;
    
    LOG_DEBUG("HTTP request: " + method + " " + path, "HTTP");
    
    // Handle API requests
    if (path.find("/api/") == 0) {
        auto it = m_apiHandlers.find(path);
        if (it != m_apiHandlers.end()) {
            return it->second("");
        } else {
            return generateApiResponse("", R"({"error": "API endpoint not found"})", 404);
        }
    }
    
    // Handle file requests
    if (method == "GET") {
        if (path == "/") {
            path = "/index.html";
        }
        
        return serveFile(path);
    }
    
    // Method not allowed
    std::string response = "HTTP/1.1 405 Method Not Allowed\r\n";
    response += "Content-Length: 0\r\n";
    response += "Connection: close\r\n\r\n";
    
    return response;
}

std::string HttpServer::serveFile(const std::string& path) {
    std::string fullPath;
    
    // Serve web interface files from /web directory
    if (path.find("/css/") == 0 || path.find("/js/") == 0 || path == "/index.html") {
        fullPath = "/web" + path;
    } else {
        // Serve files from document root (USB storage)
        fullPath = m_documentRoot + path;
    }
    
    // Security check - prevent directory traversal
    std::string normalizedPath = std::filesystem::weakly_canonical(fullPath).string();
    if (normalizedPath.find(m_documentRoot) != 0 && normalizedPath.find("/web") != 0) {
        std::string response = "HTTP/1.1 403 Forbidden\r\n";
        response += "Content-Length: 0\r\n";
        response += "Connection: close\r\n\r\n";
        return response;
    }
    
    if (!std::filesystem::exists(fullPath)) {
        std::string response = "HTTP/1.1 404 Not Found\r\n";
        response += "Content-Type: text/html\r\n";
        response += "Connection: close\r\n\r\n";
        response += "<html><body><h1>404 Not Found</h1></body></html>";
        return response;
    }
    
    if (std::filesystem::is_directory(fullPath)) {
        if (m_directoryListing) {
            return listDirectory(fullPath);
        } else {
            std::string response = "HTTP/1.1 403 Forbidden\r\n";
            response += "Content-Length: 0\r\n";
            response += "Connection: close\r\n\r\n";
            return response;
        }
    }
    
    // Serve file
    std::ifstream file(fullPath, std::ios::binary);
    if (!file.is_open()) {
        std::string response = "HTTP/1.1 500 Internal Server Error\r\n";
        response += "Content-Length: 0\r\n";
        response += "Connection: close\r\n\r\n";
        return response;
    }
    
    std::ostringstream content;
    content << file.rdbuf();
    std::string fileContent = content.str();
    
    std::string mimeType = FileUtils::getMimeType(fullPath);
    
    std::string response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: " + mimeType + "\r\n";
    response += "Content-Length: " + std::to_string(fileContent.length()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += fileContent;
    
    return response;
}

std::string HttpServer::listDirectory(const std::string& path) {
    std::ostringstream html;
    
    html << "HTTP/1.1 200 OK\r\n";
    html << "Content-Type: text/html\r\n";
    html << "Connection: close\r\n\r\n";
    
    html << "<!DOCTYPE html>\n";
    html << "<html><head><title>Directory Listing</title>";
    html << "<style>body{font-family:Arial,sans-serif;margin:20px;} ";
    html << "table{border-collapse:collapse;width:100%;} ";
    html << "th,td{border:1px solid #ddd;padding:8px;text-align:left;} ";
    html << "th{background-color:#f2f2f2;}</style></head>\n";
    html << "<body><h1>Directory Listing</h1>\n";
    html << "<table><tr><th>Name</th><th>Size</th><th>Modified</th></tr>\n";
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            std::string name = entry.path().filename().string();
            std::string size = entry.is_directory() ? "-" : FileUtils::formatFileSize(entry.file_size());
            std::string modified = FileUtils::formatTime(FileUtils::getLastModifiedTime(entry.path().string()));
            
            html << "<tr>";
            html << "<td>";
            if (entry.is_directory()) {
                html << "📁 ";
            } else {
                html << "📄 ";
            }
            html << "<a href=\"" << name;
            if (entry.is_directory()) html << "/";
            html << "\">" << name << "</a></td>";
            html << "<td>" << size << "</td>";
            html << "<td>" << modified << "</td>";
            html << "</tr>\n";
        }
    } catch (const std::exception& e) {
        html << "<tr><td colspan=\"3\">Error reading directory: " << e.what() << "</td></tr>\n";
    }
    
    html << "</table></body></html>";
    
    return html.str();
}

std::string HttpServer::generateApiResponse(const std::string& endpoint, const std::string& data, int statusCode) {
    std::string statusText = "OK";
    if (statusCode == 404) statusText = "Not Found";
    else if (statusCode == 500) statusText = "Internal Server Error";
    
    std::string response = "HTTP/1.1 " + std::to_string(statusCode) + " " + statusText + "\r\n";
    response += "Content-Type: application/json\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "Content-Length: " + std::to_string(data.length()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += data;
    
    return response;
}