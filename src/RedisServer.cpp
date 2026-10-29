#include "../include/RedisServer.h"
#include "../include/RedisDatabase.h"
#include "../include/RespParser.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

static RedisServer *globalServer = nullptr;

static constexpr int kMaxEvents = 64;
static constexpr size_t kMaxReadBuffer = 64 * 1024 * 1024;

void signalHandler(int signum)
{
    if (globalServer)
    {
        std::cout << "\nCaught signal " << signum << ", shutting down...\n";
        globalServer->shutdown();
    }
    exit(signum);
}

static void setNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return;
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void RedisServer::setupSignalHandler()
{
    signal(SIGINT, signalHandler);
}

RedisServer::RedisServer(int port)
    : port(port), server_socket(-1), epoll_fd(-1), running(true)
{
    globalServer = this;
    setupSignalHandler();
}

void RedisServer::shutdown()
{
    running = false;
    if (server_socket != -1)
    {
        if (RedisDatabase::getInstance().dump("dump.my_rdb"))
            std::cout << "Database Dumped to dump.my_rdb\n";
        else
            std::cerr << "Error dumping database\n";

        close(server_socket);
        server_socket = -1;
    }
    std::cout << "Server Shutdown Complete!\n";
}

void RedisServer::updateEvents(Connection &conn)
{
    epoll_event ev{};
    ev.events = EPOLLIN;
    if (!conn.out.empty())
        ev.events |= EPOLLOUT;
    ev.data.fd = conn.fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn.fd, &ev);
}

void RedisServer::closeConnection(int fd)
{
    auto it = connections.find(fd);
    if (it == connections.end())
        return;

    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    connections.erase(it);
}

void RedisServer::acceptLoop()
{
    while (true)
    {
        int client = accept(server_socket, nullptr, nullptr);
        if (client < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            if (errno == EINTR)
                continue;
            break;
        }

        setNonBlocking(client);

        Connection conn;
        conn.fd = client;
        auto result = connections.emplace(client, std::move(conn));
        if (!result.second)
        {
            close(client);
            continue;
        }

        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = client;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client, &ev) < 0)
        {
            closeConnection(client);
        }
    }
}

void RedisServer::flushConnection(Connection &conn)
{
    size_t sent = 0;

    while (sent < conn.out.size())
    {
        ssize_t n = send(conn.fd, conn.out.data() + sent,
                         conn.out.size() - sent, MSG_NOSIGNAL);
        if (n > 0)
        {
            sent += static_cast<size_t>(n);
        }
        else if (n < 0 && errno == EINTR)
        {
            continue;
        }
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            break;
        }
        else
        {
            conn.want_close = true;
            return;
        }
    }

    if (sent > 0)
        conn.out.erase(0, sent);

    updateEvents(conn);
}

void RedisServer::handleRead(Connection &conn)
{
    char tmp[16384];

    // 1. 读到 EAGAIN 为止
    while (true)
    {
        ssize_t n = recv(conn.fd, tmp, sizeof(tmp), 0);
        if (n > 0)
        {
            conn.in.Append(tmp, static_cast<size_t>(n));
        }
        else if (n == 0)
        {
            closeConnection(conn.fd);
            return;
        }
        else
        {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            closeConnection(conn.fd);
            return;
        }
    }

    // 2. 循环解析、处理、累积响应
    std::string responses;
    bool fatal = false;

    while (true)
    {
        ParseResult r = RespParser::parseNext(conn.in);

        if (r.status == ParseStatus::NeedMore)
        {
            if (conn.in.readAble() > kMaxReadBuffer)
            {
                responses += "-ERR Protocol error: too big\r\n";
                fatal = true;
            }
            break;
        }

        if (r.status == ParseStatus::Error)
        {
            responses += "-ERR Protocol error\r\n";
            fatal = true;
            break;
        }

        responses += cmdHandler.processCommand(r.args);
        conn.in.Consume(r.consumed);
    }

    if (!responses.empty())
        conn.out += responses;

    if (fatal)
        conn.want_close = true;

    // 3. 发送响应
    flushConnection(conn);

    if (conn.want_close)
        closeConnection(conn.fd);
}

void RedisServer::handleWrite(Connection &conn)
{
    flushConnection(conn);
    if (conn.want_close)
        closeConnection(conn.fd);
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

    setNonBlocking(server_socket);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        std::cerr << "Error Binding Server Socket\n";
        return;
    }

    if (listen(server_socket, 128) < 0)
    {
        std::cerr << "Error Listening On Server Socket\n";
        return;
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0)
    {
        std::cerr << "Error Creating epoll\n";
        return;
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = server_socket;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket, &ev) < 0)
    {
        std::cerr << "Error Adding Listen Socket To epoll\n";
        return;
    }

    std::cout << "Redis Server Listening On Port " << port << "\n";

    epoll_event events[kMaxEvents];

    while (running)
    {
        int n = epoll_wait(epoll_fd, events, kMaxEvents, -1);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            std::cerr << "epoll_wait failed\n";
            break;
        }

        for (int i = 0; i < n; ++i)
        {
            int fd = events[i].data.fd;

            if (fd == server_socket)
            {
                acceptLoop();
                continue;
            }

            auto it = connections.find(fd);
            if (it == connections.end())
                continue;

            if (events[i].events & (EPOLLERR | EPOLLHUP))
            {
                closeConnection(fd);
                continue;
            }

            if (events[i].events & EPOLLIN)
            {
                handleRead(it->second);

                // handleRead 可能已经关闭连接，重新查找
                it = connections.find(fd);
                if (it == connections.end())
                    continue;
            }

            if (events[i].events & EPOLLOUT)
            {
                handleWrite(it->second);
            }
        }
    }

    for (auto &pair : connections)
        close(pair.first);
    connections.clear();

    if (epoll_fd != -1)
        close(epoll_fd);

    if (server_socket != -1)
    {
        if (RedisDatabase::getInstance().dump("dump.my_rdb"))
            std::cout << "Database Dumped to dump.my_rdb\n";
        else
            std::cerr << "Error dumping database\n";
        close(server_socket);
    }
}