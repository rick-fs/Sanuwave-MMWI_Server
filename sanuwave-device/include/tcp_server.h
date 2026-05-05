// include/tcp_server.h
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace sanuwave
{

struct FramePacket
{
    std::vector<uint8_t> data;
    std::string modality;
    std::string format;
    int width;
    int height;
    uint64_t timestamp_ms;
};

class TCPServer
{
  public:
    using CommandCallback = std::function<std::string(const std::string &)>;

    TCPServer(int port = 8080);
    ~TCPServer();

    TCPServer(const TCPServer &) = delete;
    TCPServer &operator=(const TCPServer &) = delete;

    bool start();
    void stop();
    bool isRunning() const
    {
        return running;
    }

    void setCommandCallback(CommandCallback callback)
    {
        commandCallback = callback;
    }

    void setDisconnectCallback(std::function<void()> callback)
    {
        disconnectCallback = callback;
    }

    bool sendImage(const std::vector<uint8_t> &imageData, const std::string &modality);

    /// Send a diagnostic frame: JSON header + raw binary payload.
    /// Synchronous — only used for diagnostic one-shot captures (not streaming).
    void sendDiagFrame(const std::string& headerJson, 
                       const uint8_t* data, size_t dataSize);

    void sendJsonNotification(const std::string& json);
    /**
     * Queue a stream frame for sending (non-blocking)
     * Frames are dropped if queue is full
     */
    void broadcastStreamFrame(const std::vector<uint8_t> &frameData, const std::string &modality,
                              const std::string &format, int width, int height,
                              uint64_t timestamp_ms);

    int getClientCount();

  private:
    void acceptLoop();
    void handleClient(int clientSocket);
    void frameSenderLoop();
    void sendFrameToClients(const FramePacket &frame);
    std::string processCommand(const std::string &jsonCommand);
    void clearFrameQueue();

    int serverSocket;
    int port;
    std::atomic<bool> running;
    std::thread acceptThread;
    std::vector<int> clientSockets;
    std::mutex clientMutex;

    CommandCallback commandCallback;
    std::function<void()> disconnectCallback;

    // Frame sender thread
    std::thread frameSenderThread;
    std::atomic<bool> frameSenderRunning{false};
    std::queue<FramePacket> frameQueue;
    std::mutex frameQueueMutex;
    std::condition_variable frameQueueCV;
    static constexpr size_t MAX_FRAME_QUEUE_SIZE = 3;
};

} // namespace sanuwave
