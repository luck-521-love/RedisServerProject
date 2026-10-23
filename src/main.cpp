#include "../include/RedisServer.h"
#include "../include/RedisDatabase.h"
#include <iostream>
#include <thread>
#include <chrono>

int main(int argc, char *argv[])
{
    int port = 6379;
    int port1 = 0;
    if (argc >= 2)
    {
        port1 = std::stoi(argv[1]);
        if (port1 > 6379)
            std::cerr << "port mistake\n";
        else
            port = port1;
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