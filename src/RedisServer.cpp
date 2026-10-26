#include "../include/RedisServer.h"
#include "../include/RedisCommandHandler.h"
#include "../include/RedisDatabase.h"
#include "../include/Buffer.h"
#include "../include/RespParser.h"

#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <vector>
#include <thread>
#include <cstring>
#include <signal.h>

// Global pointer for signal handling
static RedisServer *globalServer = nullptr;

void signalHandler(int signum)
{
    if (globalServer)
    {
        std::cout << "\nCaught signal " << signum << ", shutting down...\n";
        globalServer->shutdown();
    }
    exit(signum);
}

void RedisServer::setupSignalHandler()
{
    signal(SIGINT, signalHandler);
}

RedisServer::RedisServer(int port) : port(port), server_socket(-1), running(true)
{
    globalServer = this;
    setupSignalHandler();
}

void RedisServer::shutdown()
{
    running = false;
    if (server_socket != -1)
    {
        // Before shutdown, persist the database
        if (RedisDatabase::getInstance().dump("dump.my_rdb"))
            std::cout << "Database Dumped to dump.my_rdb\n";
        else
            std::cerr << "Error dumping database\n";
        close(server_socket);
    }
    std::cout << "Server Shutdown Complete!\n";
}

static bool sendAll(int fd, const std::string &data)
{
    size_t sent = 0;
    while (sent < data.size())
    {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0)
            return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}
void RedisServer::run()
{
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        std::cerr << "Error Creating Server Socket\n";
        return;
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        std::cerr << "Error Binding Server Socket\n";
        return;
    }

    if (listen(server_socket, 10) < 0)
    {
        std::cerr << "Error Listening On Server Socket\n";
        return;
    }

    std::cout << "Redis Server Listening On Port " << port << "\n";

    std::vector<std::thread> threads;
    RedisCommandHandler cmdHandler;

    while (running)
    {
        int client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket < 0)
        {
            if (running)
                std::cerr << "Error Accepting Client Connection\n";
            break;
        }

        threads.emplace_back([client_socket, &cmdHandler]()
                             { 
    Buffer readBuf;
    char tmp[16384];
    constexpr size_t kMaxReadBuffer = 64 * 1024 * 1024; // 单连接缓冲上限 64MB

    while (true)
    {
        ssize_t n = recv(client_socket, tmp, sizeof(tmp), 0);
        if (n <= 0)
            break;

        readBuf.Append(tmp, static_cast<size_t>(n));

        std::string out;
        bool fatal = false;

        // 一次 recv 的数据里可能有多条命令，循环解析
        while (true)
        {
            ParseResult r = RespParser::parseNext(readBuf);

            if (r.status == ParseStatus::NeedMore)
            {
                if (readBuf.readAble() > kMaxReadBuffer)
                {
                    out += "-ERR Protocol error: too big\r\n";
                    fatal = true;
                }
                break;  // 保留剩余数据，等下一次 recv
            }

            if (r.status == ParseStatus::Error)
            {
                out += "-ERR Protocol error\r\n";
                fatal = true;
                break;
            }

            // 解析成功：处理命令、累积响应、消费字节
            out += cmdHandler.processCommand(r.args);
            readBuf.Consume(r.consumed);
        }

        if (!out.empty() && !sendAll(client_socket, out))
            break;

        if (fatal)
            break;
    }

    close(client_socket); });
    }

    for (auto &t : threads)
    {
        if (t.joinable())
            t.join();
    }

    // Before shutdown, persist the database
    if (RedisDatabase::getInstance().dump("dump.my_rdb"))
        std::cout << "Database Dumped to dump.my_rdb\n";
    else
        std::cerr << "Error dumping database\n";
}