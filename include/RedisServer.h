#ifndef REDIS_SERVER_H
#define REDIS_SERVER_H

#include <atomic>
#include <string>
#include <unordered_map>

#include "Buffer.h"
#include "RedisCommandHandler.h"
#include "ServerStats.h"

class RedisServer
{
public:
    RedisServer(int port);
    void run();
    void shutdown();

private:
    struct Connection
    {
        int fd = -1;
        Buffer in;
        std::string out;
        bool want_close = false;
    };

    int port;
    int server_socket;
    int epoll_fd;
    std::atomic<bool> running;
    std::unordered_map<int, Connection> connections;

    // 注意声明顺序：stats/config 必须在 cmdHandler 之前
    ServerStats stats;
    ServerConfig config;
    RedisCommandHandler cmdHandler;

    void setupSignalHandler();
    void acceptLoop();
    void handleRead(Connection &conn);
    void handleWrite(Connection &conn);
    void flushConnection(Connection &conn);
    void updateEvents(Connection &conn);
    void closeConnection(int fd);
};

#endif