#!/usr/bin/env bash
# tests/smoke.sh —— 带断言的集成测试
set -u

PORT=6378
SERVER="$HOME/RedisServer/my_redis_server"
WORK=/tmp/redis-test-run

PASS=0
FAIL=0

pass() { echo "PASS: $1"; PASS=$((PASS + 1)); }

fail() {
  echo "FAIL: $1"
  echo "      期望: [$2]"
  echo "      实际: [$3]"
  FAIL=$((FAIL + 1))
}

assert_eq() {
  local name="$1"
  local expected="$2"
  local actual="$3"
  if [ "$expected" = "$actual" ]; then
    pass "$name"
  else
    fail "$name" "$expected" "$actual"
  fi
}

echo "== 1. 清理旧进程 =="
pkill -f "my_redis_server $PORT" 2>/dev/null
sleep 0.3
rm -rf "$WORK"
mkdir -p "$WORK"

echo "== 2. 启动服务器 =="
cd "$WORK"
"$SERVER" "$PORT" > server.log 2>&1 &
SRV=$!
trap 'kill "$SRV" 2>/dev/null; wait "$SRV" 2>/dev/null' EXIT

for i in $(seq 1 20); do
  redis-cli -p "$PORT" PING >/dev/null 2>&1 && break
  sleep 0.1
done

cli() { redis-cli -p "$PORT" --raw "$@"; }

echo "== 3. 测试 =="
assert_eq "PING" "PONG" "$(cli PING)"

assert_eq "HSET 新字段返回 1" "1" "$(cli HSET test:1 name Alice)"
assert_eq "HSET 覆盖返回 0" "0" "$(cli HSET test:1 name Bob)"
assert_eq "HGET 返回最新值" "Bob" "$(cli HGET test:1 name)"

assert_eq "HMSET 两个 pair" "OK" "$(cli HMSET hm a 1 b 2)"
assert_eq "HLEN 为 2" "2" "$(cli HLEN hm)"
assert_eq "HGET hm b" "2" "$(cli HGET hm b)"

assert_eq "HGETALL 不存在的 key 为空" "" "$(cli HGETALL no-such-key)"

RAW=$(python3 - "$PORT" <<'PY'
import socket, sys

port = int(sys.argv[1])
s = socket.create_connection(("127.0.0.1", port), timeout=2)
s.sendall(b"*2\r\n$7\r\nHGETALL\r\n$6\r\ntest:1\r\n")
data = s.recv(4096)
s.close()
print(data.hex())
PY
)
assert_eq "HGETALL 原始字节" "2a320d0a24340d0a6e616d650d0a24330d0a426f620d0a" "$RAW"
# FLUSHALL 必须清掉旧 TTL，不能误删新 key
assert_eq "SET t v" "OK" "$(cli SET t v)"
assert_eq "EXPIRE t 1" "OK" "$(cli EXPIRE t 1)"
assert_eq "FLUSHALL" "OK" "$(cli FLUSHALL)"
assert_eq "SET t new" "OK" "$(cli SET t new)"
sleep 2
assert_eq "FLUSHALL 清除旧 TTL 后新 key 存活" "new" "$(cli GET t)"

# 过期 hash / list 的读取必须视为不存在
assert_eq "准备过期 hash" "1" "$(cli HSET h3 f v)"
assert_eq "准备过期 list" "3" "$(cli RPUSH l3 a b c)"
assert_eq "EXPIRE h3" "OK" "$(cli EXPIRE h3 1)"
assert_eq "EXPIRE l3" "OK" "$(cli EXPIRE l3 1)"
sleep 2
assert_eq "过期 HGET 返回空" "" "$(cli HGET h3 f)"
assert_eq "过期 HLEN 返回 0" "0" "$(cli HLEN h3)"
assert_eq "过期 LGET 返回空" "" "$(cli LGET l3)"
assert_eq "过期 LLEN 返回 0" "0" "$(cli LLEN l3)"
assert_eq "过期 TYPE 返回 none" "none" "$(cli TYPE h3)"

# 没设 TTL 的数据必须正常
assert_eq "无 TTL HSET" "1" "$(cli HSET h4 f v)"
assert_eq "无 TTL HGET" "v" "$(cli HGET h4 f)"

echo
echo "== 结果：PASS=$PASS FAIL=$FAIL =="
[ "$FAIL" -eq 0 ] || exit 1
