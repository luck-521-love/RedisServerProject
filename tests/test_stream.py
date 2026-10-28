#!/usr/bin/env python3
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

PORT = 6378
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "my_redis_server")

PASS = 0
FAIL = 0


def check(name, expected, actual):
    global PASS, FAIL
    if expected == actual:
        print(f"PASS: {name}")
        PASS += 1
    else:
        print(f"FAIL: {name}")
        print(f"  expected: {expected!r}")
        print(f"  actual:   {actual!r}")
        FAIL += 1


def connect():
    return socket.create_connection(("127.0.0.1", PORT), timeout=2)


def recv_n(s, n, timeout=2.0):
    s.settimeout(timeout)
    buf = b""
    while len(buf) < n:
        try:
            chunk = s.recv(n - len(buf))
        except socket.timeout:
            break
        if not chunk:
            break
        buf += chunk
    return buf


def wait_ready():
    for _ in range(50):
        try:
            s = connect()
            s.sendall(b"PING\r\n")
            if recv_n(s, 7) == b"+PONG\r\n":
                s.close()
                return True
            s.close()
        except OSError:
            pass
        time.sleep(0.1)
    return False


def main():
    if not os.path.exists(BIN):
        print(f"server binary not found: {BIN}")
        return 1

    subprocess.run(["pkill", "-f", f"my_redis_server {PORT}"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.3)

    work = tempfile.mkdtemp(prefix="redis-stream-")
    logfile = open(os.path.join(work, "server.log"), "wb")
    proc = subprocess.Popen([BIN, str(PORT)], cwd=work,
                            stdout=logfile, stderr=subprocess.STDOUT)
    try:
        if not wait_ready():
            print("server did not start")
            return 1

        # 1. 流水线：3 条命令一个包
        s = connect()
        s.sendall(b"PING\r\nPING\r\nPING\r\n")
        check("pipeline 3 PING", b"+PONG\r\n" * 3, recv_n(s, 21))
        s.close()

        # 2. 半包：SET 拆两次发送
        s = connect()
        s.sendall(b"SET frag ")
        time.sleep(0.1)
        s.sendall(b"v\r\n")
        check("fragmented SET", b"+OK\r\n", recv_n(s, 5))
        s.sendall(b"GET frag\r\n")
        check("fragmented GET", b"$1\r\nv\r\n", recv_n(s, 7))
        s.close()

        # 3. 2KB value
        value = b"x" * 2000
        s = connect()
        s.sendall(b"SET big " + value + b"\r\n")
        check("big value SET", b"+OK\r\n", recv_n(s, 5))
        s.sendall(b"GET big\r\n")
        expected = b"$2000\r\n" + value + b"\r\n"
        check("big value GET", expected, recv_n(s, len(expected)))
        s.close()

        # 4. 流水线混合命令
        s = connect()
        s.sendall(b"SET mix 1\r\nGET mix\r\n")
        check("pipeline SET+GET", b"+OK\r\n$1\r\n1\r\n", recv_n(s, 12))
        s.close()

        # 5. 畸形 RESP：返回错误并关闭连接
        s = connect()
        s.sendall(b"*abc\r\n")
        check("malformed RESP", b"-ERR Protocol error\r\n", recv_n(s, 21))
        s.close()

        # 6. 服务器仍然存活
        s = connect()
        s.sendall(b"PING\r\n")
        check("server alive after malformed", b"+PONG\r\n", recv_n(s, 7))
        s.close()
         # 7. GET 读错误类型 → WRONGTYPE
        s = connect()
        s.sendall(b"LPUSH wt:list a\r\n")
        check("prepare list for WRONGTYPE", b":1\r\n", recv_n(s, 4))
        expected = b"-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"
        s.sendall(b"GET wt:list\r\n")
        check("GET on list returns WRONGTYPE", expected, recv_n(s, len(expected)))
        s.close()

        # 8. GET 不存在的 key 仍然返回 nil
        s = connect()
        s.sendall(b"GET no-such-wt\r\n")
        check("GET missing returns nil", b"$-1\r\n", recv_n(s, 5))
        s.close()
    finally:
        proc.kill()
        proc.wait()
        logfile.close()
        shutil.rmtree(work, ignore_errors=True)

    print(f"\n== result: PASS={PASS} FAIL={FAIL} ==")
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())