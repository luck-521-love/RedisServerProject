#include "../include/RespParser.h"
#include "../include/Buffer.h"

#include <cassert>
#include <iostream>
#include <string>

// 把一段字节放进 Buffer，然后解析一次
static ParseResult parse(const std::string &bytes)
{
    Buffer b;
    b.Append(bytes.data(), bytes.size());
    return RespParser::parseNext(b);
}

int main()
{
    // 1. 空 buffer：数据不够
    assert(parse("").status == ParseStatus::NeedMore);

    // 2. 完整命令：*1\r\n$4\r\nPING\r\n
    {
        auto r = parse("*1\r\n$4\r\nPING\r\n");
        assert(r.status == ParseStatus::OK);
        assert(r.args.size() == 1);
        assert(r.args[0] == "PING");
        assert(r.consumed == 14);
    }

    // 3. 半包：只有前半条，应该等
    assert(parse("*1\r\n$4\r\nPI").status == ParseStatus::NeedMore);
    assert(parse("*1\r\n$4").status == ParseStatus::NeedMore);

    // 4. 畸形输入：已经确定非法，报 Error
    assert(parse("*abc\r\n").status == ParseStatus::Error);
    assert(parse("*1\r\nPING\r\n").status == ParseStatus::Error);

    // 5. 两个元素：*2\r\n$3\r\nGET\r\n$1\r\nk\r\n
    {
        auto r = parse("*2\r\n$3\r\nGET\r\n$1\r\nk\r\n");
        assert(r.status == ParseStatus::OK);
        assert(r.args.size() == 2);
        assert(r.args[0] == "GET");
        assert(r.args[1] == "k");
        assert(r.consumed == 20);
    }

    // 6. 二进制安全：value 里有空格，必须原样保留
    {
        auto r = parse("*2\r\n$3\r\nSET\r\n$11\r\nhello world\r\n");
        assert(r.status == ParseStatus::OK);
        assert(r.args.size() == 2);
        assert(r.args[1] == "hello world");
    }

    // 7. 流水线：同一 Buffer 里两条命令，消费第一条后再解析第二条
    {
        Buffer b;
        std::string two = "*1\r\n$4\r\nPING\r\n*1\r\n$4\r\nPING\r\n";
        b.Append(two.data(), two.size());

        auto r1 = RespParser::parseNext(b);
        assert(r1.status == ParseStatus::OK);
        assert(r1.consumed == 14);
        b.Consume(r1.consumed);

        auto r2 = RespParser::parseNext(b);
        assert(r2.status == ParseStatus::OK);
        assert(r2.consumed == 14);
        b.Consume(r2.consumed);

        assert(b.readAble() == 0);
    }

    std::cout << "all parser tests passed" << std::endl;
    return 0;
}