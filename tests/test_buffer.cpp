#include "../include/Buffer.h"

#include <cassert>
#include <iostream>
#include <string>

int main()
{
    Buffer b;

    // 1. 第一次只收到半条
    b.Append("PIN", 3);
    assert(b.readAble() == 3);
    assert(std::string(b.Peek(), b.readAble()) == "PIN");

    // 2. 剩下半条到了
    b.Append("G\r\n", 3);
    assert(b.readAble() == 6);
    assert(std::string(b.Peek(), b.readAble()) == "PING\r\n");

    // 3. 消费整条
    b.Consume(6);
    assert(b.readAble() == 0);

    // 4. 验证 compact：消费掉前缀后，新数据仍然能按顺序读出来
    b.Append("abcdef", 6);
    b.Consume(4);
    b.Append("gh", 2);
    assert(b.readAble() == 4);
    assert(std::string(b.Peek(), b.readAble()) == "efgh");

    std::cout << "all buffer tests passed" << std::endl;
    return 0;
    return 0;
}