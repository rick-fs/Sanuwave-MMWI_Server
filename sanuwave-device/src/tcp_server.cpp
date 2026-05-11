// src/tcp_server.cpp
#include "tcp_server.h"
#include "logger.h"
#include "stream_frame_meta.h"
#include "protocol_constants.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <sstream>

namespace sanuwave
{

TCPServer::TCPServer(int port)
    : serverSocket(-1)
    , port(port)
    , running(false)
{
}

TCPServer::~TCPServer()
{
    stop();
}

bool TCPServer::start()
{
    if (running)
    {
        LOG_WARNING << "Server already running" << std::endl;
        return true;
    }
    
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0)
    {
        LOG_ERROR << "Failed to create socket" << std::endl;
        return false;
    }
    
    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        LOG_WARNING << "Failed to set SO_REUSEADDR" << std::endl;
    }
    
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    if (bind(serverSocket, (struct sockaddr*)&address, sizeof(address)) < 0)
    {
        LOG_ERROR << "Failed to bind to port " << port << std::endl;
        close(serverSocket);
        serverSocket = -1;
        return false;
    }
    
    if (listen(serverSocket, 5) < 0)
    {
        LOG_ERROR << "Failed to listen on socket" << std::endl;
        close(serverSocket);
        serverSocket = -1;
        return false;
    }
    
    running = true;
    
    // Start frame sender thread
    frameSenderRunning = true;
    frameSenderThread = std::thread(&TCPServer::frameSenderLoop, this);
    
    // Start accept thread
    acceptThread = std::thread(&TCPServer::acceptLoop, this);
    
    LOG_INFO << "TCP Server started on port " << port << std::endl;
    
    return true;
}

void TCPServer::stop()
{
    if (!running)
    {
        return;
    }
    
    running = false;
    
    // Stop frame sender thread
    frameSenderRunning = false;
    frameQueueCV.notify_all();
    if (frameSenderThread.joinable())
        frameSenderThread.join();
    
    // Clear any remaining frames
    {
        std::lock_guard<std::mutex> lock(frameQueueMutex);
        while (!frameQueue.empty())
            frameQueue.pop();
    }
    
    // Close server socket to unblock accept()
    if (serverSocket >= 0)
    {
        shutdown(serverSocket, SHUT_RDWR);
        close(serverSocket);
        serverSocket = -1;
    }
    
    // Close all client connections
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        for (int clientSocket : clientSockets)
        {
            shutdown(clientSocket, SHUT_RDWR);
            close(clientSocket);
        }
        clientSockets.clear();
    }
    
    if (acceptThread.joinable())
        acceptThread.join();
    
    LOG_INFO << "TCP Server stopped" << std::endl;
}

void TCPServer::acceptLoop()
{
    while (running)
    {
        struct sockaddr_in clientAddress;
        socklen_t clientLen = sizeof(clientAddress);
        
        int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddress, &clientLen);
        
        if (clientSocket < 0)
        {
            if (running)
            {
                LOG_ERROR << "Accept failed" << std::endl;
            }
            continue;
        }
        
        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddress.sin_addr, clientIP, INET_ADDRSTRLEN);
        
        LOG_INFO << "Client connected: " << clientIP << std::endl;
        
        {
            std::lock_guard<std::mutex> lock(clientMutex);
            clientSockets.push_back(clientSocket);
        }
        
        std::thread clientThread(&TCPServer::handleClient, this, clientSocket);
        clientThread.detach();
    }
}

void TCPServer::clearFrameQueue()
{
    std::lock_guard<std::mutex> lock(frameQueueMutex);
    size_t count = frameQueue.size();
    while (!frameQueue.empty())
        frameQueue.pop();
    LOG_INFO << "Cleared " << count << " frames from send queue" << std::endl;
}

void TCPServer::handleClient(int clientSocket)
{
    char buffer[4096];
    std::string receiveBuffer;
    bool connectionActive = true;
    
    while (running && connectionActive)
    {
        int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytesRead <= 0)
        {
            if (bytesRead < 0)
                LOG_WARNING << "recv() error on client socket" << std::endl;
            else
                LOG_DEBUG << "Client closed connection" << std::endl;
            connectionActive = false;
            break;
        }
        
        buffer[bytesRead] = '\0';
        receiveBuffer += buffer;
        
        size_t newlinePos = receiveBuffer.find('\n');
        
        while (newlinePos != std::string::npos && connectionActive)
        {
            std::string command = receiveBuffer.substr(0, newlinePos);
            receiveBuffer.erase(0, newlinePos + 1);
            
            if (command.empty() || command == "\r")
            {
                newlinePos = receiveBuffer.find('\n');
                continue;
            }
            
            LOG_DEBUG << "Received command: " << command << std::endl;
            
            std::string response = processCommand(command);
            
            if (!response.empty() && connectionActive)
            {
                response += "\n";
                ssize_t sent = send(clientSocket, response.c_str(), response.length(), MSG_NOSIGNAL);
                
                if (sent <= 0)
                {
                    LOG_WARNING << "Failed to send response (client disconnected)" << std::endl;
                    connectionActive = false;
                    break;
                }
            }
            
            newlinePos = receiveBuffer.find('\n');
        }
    }
    
    if (disconnectCallback)
    {
        disconnectCallback();
    }
    // Remove from client list
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        clientSockets.erase(
            std::remove(clientSockets.begin(), clientSockets.end(), clientSocket),
            clientSockets.end()
        );
    }
    
    close(clientSocket);
    LOG_INFO << "Client disconnected" << std::endl;
}

void TCPServer::frameSenderLoop()
{
    LOG_INFO << "Frame sender thread started" << std::endl;
    
    while (frameSenderRunning)
    {
        FramePacket frame;
        LOG_TRACE << "Waiting for frame to send..." << std::endl;

        {
            std::unique_lock<std::mutex> lock(frameQueueMutex);
            
            frameQueueCV.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return !frameQueue.empty() || !frameSenderRunning;
            });
            
            if (!frameSenderRunning)
                break;
            
            if (frameQueue.empty())
            {
                LOG_TRACE << "Frame queue is empty, continuing to wait" << std::endl;
                continue;
            }
            
            frame = std::move(frameQueue.front());
            frameQueue.pop();
            LOG_TRACE << "Sending frame with size " << frame.data.size() << std::endl;
        }
        
        sendFrameToClients(frame);
    }
    
    LOG_INFO << "Frame sender thread stopped" << std::endl;
}

void TCPServer::sendFrameToClients(const FramePacket& frame)
{
    std::lock_guard<std::mutex> lock(clientMutex);

    if (clientSockets.empty())
        return;

    const auto& m = frame.meta;
    namespace P = sanuwave::protocol;

    std::ostringstream header;
    // Fixed precision so floats serialise without surprises (default
    // precision is only 6 significant digits; not enough for stable
    // motion display). Applies only to this stream.
    header.precision(6);
    header << std::fixed;

    header << R"({"type":")" << P::ResponseType::STREAM_FRAME << R"(",)"
           << R"("modality":")" << m.modality << R"(",)"
           << R"("format":")"   << m.format   << R"(",)"
           << R"("width":)"        << m.width        << ","
           << R"("height":)"       << m.height       << ","
           << R"("timestamp_ms":)" << m.timestamp_ms << ","
           << R"("size":)"         << frame.data.size();

    // Motion sub-object is emitted only when valid and all numeric fields
    // are finite (NaN/Inf would produce invalid JSON). Unaware clients see
    // the same wire shape as before; aware clients parse it via
    // MotionField keys.
    const bool motionEmit =
        m.motion.valid &&
        std::isfinite(m.motion.trans_px) &&
        std::isfinite(m.motion.rot_deg)  &&
        std::isfinite(m.motion.confidence);

    if (motionEmit)
    {
        header << R"(,")" << P::MotionField::OBJECT << R"(":{)"
               << R"(")" << P::MotionField::VALID      << R"(":true,)"
               << R"(")" << P::MotionField::TRANS_PX   << R"(":)" << m.motion.trans_px   << ","
               << R"(")" << P::MotionField::ROT_DEG    << R"(":)" << m.motion.rot_deg    << ","
               << R"(")" << P::MotionField::CONFIDENCE << R"(":)" << m.motion.confidence << ","
               << R"(")" << P::MotionField::REFERENCE  << R"(":")" << m.motion.reference << R"("})";
    }

    header << "}\n";
    std::string headerStr = header.str();

    auto it = clientSockets.begin();
    while (it != clientSockets.end())
    {
        int clientSocket = *it;
        bool sendSuccess = true;
        
        ssize_t headerSent = send(clientSocket, headerStr.c_str(), headerStr.length(), MSG_NOSIGNAL);
        if (headerSent != static_cast<ssize_t>(headerStr.length()))
        {
            LOG_WARNING << "Failed to send frame header to client " << clientSocket << std::endl;
            sendSuccess = false;
        }
        
        if (sendSuccess)
        {
            size_t totalSent = 0;
            while (totalSent < frame.data.size())
            {
                ssize_t sent = send(clientSocket, 
                                    frame.data.data() + totalSent, 
                                    frame.data.size() - totalSent, 
                                    MSG_NOSIGNAL);
                
                if (sent <= 0)
                {
                    LOG_WARNING << "Failed to send frame data to client " << clientSocket << std::endl;
                    sendSuccess = false;
                    break;
                }
                
                totalSent += sent;
            }
        }
        
        if (!sendSuccess)
        {
            LOG_INFO << "Removing client " << clientSocket << " due to send failure" << std::endl;
            shutdown(clientSocket, SHUT_RDWR);
            close(clientSocket);
            it = clientSockets.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void TCPServer::broadcastStreamFrame(const std::vector<uint8_t>& frameData,
                                     const sanuwave::StreamFrameMeta& meta)
{
    if (frameData.empty())
        return;

    std::lock_guard<std::mutex> lock(frameQueueMutex);

    // Drop frames if queue is backing up
    if (frameQueue.size() >= MAX_FRAME_QUEUE_SIZE)
    {
        LOG_DEBUG << "Frame queue full, dropping frame" << std::endl;
        return;
    }

    frameQueue.push(FramePacket{frameData, meta});
    frameQueueCV.notify_one();
}

std::string TCPServer::processCommand(const std::string& jsonCommand)
{
    if (commandCallback)
    {
        return commandCallback(jsonCommand);
    }
    
    // Default response if no callback set
    return R"({"type":"error","message":"No command handler registered"})";
}

void TCPServer::sendJsonNotification(const std::string& json)
{
    if (json.empty())
        return;

    std::string line = json + "\n";

    std::lock_guard<std::mutex> lock(clientMutex);

    auto it = clientSockets.begin();
    while (it != clientSockets.end())
    {
        int clientSocket = *it;
        ssize_t sent = send(clientSocket, line.c_str(), line.length(), MSG_NOSIGNAL);
        if (sent != static_cast<ssize_t>(line.length()))
        {
            LOG_WARNING << "sendJsonNotification: failed to send to client "
                        << clientSocket << std::endl;
            shutdown(clientSocket, SHUT_RDWR);
            close(clientSocket);
            it = clientSockets.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

bool TCPServer::sendImage(const std::vector<uint8_t>& imageData, const std::string& modality)
{
    if (imageData.empty())
    {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(clientMutex);
    
    if (clientSockets.empty())
    {
        LOG_WARNING << "No clients connected to send image" << std::endl;
        return false;
    }
    
    std::string header = R"({"type":"image","modality":")" + modality + 
                        R"(","size":)" + std::to_string(imageData.size()) + "}\n";
    
    bool success = false;
    
    auto it = clientSockets.begin();
    while (it != clientSockets.end())
    {
        int clientSocket = *it;
        bool sendSuccess = true;
        
        ssize_t headerSent = send(clientSocket, header.c_str(), header.length(), MSG_NOSIGNAL);
        if (headerSent != static_cast<ssize_t>(header.length()))
        {
            LOG_ERROR << "Failed to send image header to client " << clientSocket << std::endl;
            sendSuccess = false;
        }
        
        if (sendSuccess)
        {
            ssize_t dataSent = send(clientSocket, imageData.data(), imageData.size(), MSG_NOSIGNAL);
            if (dataSent != static_cast<ssize_t>(imageData.size()))
            {
                LOG_ERROR << "Failed to send image data to client " << clientSocket << std::endl;
                sendSuccess = false;
            }
        }
        
        if (sendSuccess)
        {
            LOG_DEBUG << "Sent " << modality << " image (" << imageData.size() << " bytes)" << std::endl;
            success = true;
            ++it;
        }
        else
        {
            LOG_INFO << "Removing dead client " << clientSocket << std::endl;
            shutdown(clientSocket, SHUT_RDWR);
            close(clientSocket);
            it = clientSockets.erase(it);
        }
    }
    
    return success;
}

void TCPServer::sendDiagFrame(const std::string& headerJson,const uint8_t* data, size_t dataSize)
{
    if (headerJson.empty() || !data || dataSize == 0)
        return;
    
    std::string headerLine = headerJson + "\n";
    
    std::lock_guard<std::mutex> lock(clientMutex);
    
    auto it = clientSockets.begin();
    while (it != clientSockets.end())
    {
        int clientSocket = *it;
        bool sendOk = true;
        
        // Send JSON header
        ssize_t headerSent = send(clientSocket, headerLine.c_str(), 
                                   headerLine.length(), MSG_NOSIGNAL);
        if (headerSent != static_cast<ssize_t>(headerLine.length()))
        {
            LOG_WARNING << "Failed to send diag header to client " 
                        << clientSocket << std::endl;
            sendOk = false;
        }
        
        // Send raw binary payload
        if (sendOk)
        {
            size_t totalSent = 0;
            while (totalSent < dataSize)
            {
                ssize_t sent = send(clientSocket,
                                    data + totalSent,
                                    dataSize - totalSent,
                                    MSG_NOSIGNAL);
                if (sent <= 0)
                {
                    LOG_WARNING << "Failed to send diag data to client " 
                                << clientSocket << std::endl;
                    sendOk = false;
                    break;
                }
                totalSent += sent;
            }
        }
        
        if (!sendOk)
        {
            LOG_INFO << "Removing client " << clientSocket 
                     << " due to send failure" << std::endl;
            shutdown(clientSocket, SHUT_RDWR);
            close(clientSocket);
            it = clientSockets.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

int TCPServer::getClientCount()
{
    std::lock_guard<std::mutex> lock(clientMutex);
    return clientSockets.size();
}

} // namespace sanuwave
