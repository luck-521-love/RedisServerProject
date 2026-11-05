#include "../include/RedisServer.h"
#include "../include/RedisDatabase.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <exception>
#include <string>

int main(int argc, char *argv[])
{
    int port = 6379;

    if (argc >= 2)
    {
        const char *arg = argv[1];

        try
        {
            size_t pos = 0;
            long value = std::stol(arg, &pos);

            // 必须是完整数字，且在合法端口范围
            if (pos != std::strlen(arg) || value <= 0 || value > 65535)
                throw std::out_of_range("port");

            port = static_cast<int>(value);
        }
        catch (const std::exception &)
        {
            std::cerr << "Usage: " << argv[0] << " [port 1-65535]\n";
            return 1;
        }
    }

    if (RedisDatabase::getInstance().load("dump.my_rdb"))
        std::cout << "Database loaded from dump.my_rdb\n";
    else
        std::cout << "No dump found or load failed; starting with an empty database.\n";

    RedisServer server(port);

    std::thread persistanceThread([]()
                                  {
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::seconds(300));
            if (!RedisDatabase::getInstance().dump("dump.my_rdb"))
                std::cerr << "Error Dumping Database\n";
            else
                std::cout << "Database Dumped to dump.my_rdb\n";
        } });

    persistanceThread.detach();

    server.run();
    return 0;
}