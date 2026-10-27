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
assert_eq "EXPIRE t 1" "1" "$(cli EXPIRE t 1)"
assert_eq "FLUSHALL" "OK" "$(cli FLUSHALL)"
assert_eq "SET t new" "OK" "$(cli SET t new)"
sleep 2
assert_eq "FLUSHALL 清除旧 TTL 后新 key 存活" "new" "$(cli GET t)"

# 过期 hash / list 的读取必须视为不存在
assert_eq "准备过期 hash" "1" "$(cli HSET h3 f v)"
assert_eq "准备过期 list" "3" "$(cli RPUSH l3 a b c)"
assert_eq "EXPIRE h3" "1" "$(cli EXPIRE h3 1)"
assert_eq "EXPIRE l3" "1" "$(cli EXPIRE l3 1)"
sleep 2
assert_eq "过期 HGET 返回空" "" "$(cli HGET h3 f)"
assert_eq "过期 HLEN 返回 0" "0" "$(cli HLEN h3)"
assert_eq "过期 LGET 返回空" "" "$(cli LGET l3)"
assert_eq "过期 LLEN 返回 0" "0" "$(cli LLEN l3)"
assert_eq "过期 TYPE 返回 none" "none" "$(cli TYPE h3)"

# 没设 TTL 的数据必须正常
assert_eq "无 TTL HSET" "1" "$(cli HSET h4 f v)"
assert_eq "无 TTL HGET" "v" "$(cli HGET h4 f)"

# TTL / PTTL / PERSIST 语义
assert_eq "TTL 不存在的 key" "-2" "$(cli TTL no-such-key)"
assert_eq "SET ttl:one v" "OK" "$(cli SET ttl:one v)"
assert_eq "无 TTL 时 TTL" "-1" "$(cli TTL ttl:one)"
assert_eq "EXPIRE ttl:one 100" "1" "$(cli EXPIRE ttl:one 100)"
assert_eq "TTL 约 100 秒" "100" "$(cli TTL ttl:one)"
assert_eq "PERSIST 移除 TTL" "1" "$(cli PERSIST ttl:one)"
assert_eq "PERSIST 后 TTL" "-1" "$(cli TTL ttl:one)"
assert_eq "PERSIST 后 PTTL" "-1" "$(cli PTTL ttl:one)"
assert_eq "再次 PERSIST" "0" "$(cli PERSIST ttl:one)"
assert_eq "PTTL 不存在的 key" "-2" "$(cli PTTL no-such-key)"
assert_eq "EXPIRE 不存在的 key" "0" "$(cli EXPIRE no-such-key 10)"
assert_eq "SET ttl:two v" "OK" "$(cli SET ttl:two v)"
assert_eq "EXPIRE ttl:two 0" "1" "$(cli EXPIRE ttl:two 0)"
assert_eq "EXPIRE 0 后 key 删除" "" "$(cli GET ttl:two)"
assert_eq "SET ttl:three v" "OK" "$(cli SET ttl:three v)"
assert_eq "EXPIRE ttl:three 1" "1" "$(cli EXPIRE ttl:three 1)"
assert_eq "SET ttl:three new" "OK" "$(cli SET ttl:three new)"
sleep 2
assert_eq "旧 TTL 不影响 SET 后的新值" "new" "$(cli GET ttl:three)"

# ---- Phase A: 数据正确性回归（先红后绿） ----

# ① RENAME 同名：不能把 key 删掉
assert_eq "SET rename:self v" "OK" "$(cli SET rename:self v)"
assert_eq "RENAME 同名返回 OK" "OK" "$(cli RENAME rename:self rename:self)"
assert_eq "RENAME 同名后 key 仍在" "v" "$(cli GET rename:self)"

# ① RENAME 到已有其它类型：目标旧类型必须被替换
assert_eq "SET rename:dest old" "OK" "$(cli SET rename:dest old)"
assert_eq "RPUSH rename:src a" "1" "$(cli RPUSH rename:src a)"
assert_eq "RENAME 跨类型" "OK" "$(cli RENAME rename:src rename:dest)"
assert_eq "RENAME 后目标类型为 list" "list" "$(cli TYPE rename:dest)"
assert_eq "RENAME 后目标 list 内容" "a" "$(cli LGET rename:dest)"

# ① 源无 TTL、目标有 TTL：旧 TTL 必须清除
assert_eq "SET rename:d v" "OK" "$(cli SET rename:d v)"
assert_eq "EXPIRE rename:d 100" "1" "$(cli EXPIRE rename:d 100)"
assert_eq "SET rename:s v" "OK" "$(cli SET rename:s v)"
assert_eq "RENAME 到有 TTL 的目标" "OK" "$(cli RENAME rename:s rename:d)"
assert_eq "源无 TTL 时目标 TTL 必须为 -1" "-1" "$(cli TTL rename:d)"

# ② 过期后的写操作不能被旧 TTL 误删
assert_eq "HSET expired:h f v" "1" "$(cli HSET expired:h f v)"
assert_eq "EXPIRE expired:h 1" "1" "$(cli EXPIRE expired:h 1)"
sleep 2
assert_eq "过期后 HSET 新字段" "1" "$(cli HSET expired:h g w)"
assert_eq "过期后 HGET 新字段" "w" "$(cli HGET expired:h g)"
assert_eq "过期后旧字段应消失" "" "$(cli HGET expired:h f)"

# ③ 列表最后一个元素被弹出后，key 应删除
assert_eq "RPUSH empty:list a" "1" "$(cli RPUSH empty:list a)"
assert_eq "LPOP 最后一个元素" "a" "$(cli LPOP empty:list)"
assert_eq "LPOP 后 TYPE 应为 none" "none" "$(cli TYPE empty:list)"
assert_eq "LPOP 后 KEYS 不含该 key" "0" "$(cli KEYS '*' | grep -c '^empty:list$')"
# RPOP 最后一个元素后 key 也应删除
assert_eq "RPUSH empty:list2 a" "1" "$(cli RPUSH empty:list2 a)"
assert_eq "RPOP 最后一个元素" "a" "$(cli RPOP empty:list2)"
assert_eq "RPOP 后 TYPE 应为 none" "none" "$(cli TYPE empty:list2)"

# LREM 删除全部元素后 key 也应删除
assert_eq "RPUSH empty:list3 a" "1" "$(cli RPUSH empty:list3 a)"
assert_eq "LREM 删除全部" "1" "$(cli LREM empty:list3 0 a)"
assert_eq "LREM 后 TYPE 应为 none" "none" "$(cli TYPE empty:list3)"
# 过期 list 上继续 RPUSH：旧数据不能复活
assert_eq "RPUSH expired:list a" "1" "$(cli RPUSH expired:list a)"
assert_eq "EXPIRE expired:list 1" "1" "$(cli EXPIRE expired:list 1)"
sleep 2
assert_eq "过期后 RPUSH 新元素" "1" "$(cli RPUSH expired:list b)"
assert_eq "过期后 LLEN" "1" "$(cli LLEN expired:list)"
assert_eq "过期后 LGET" "b" "$(cli LGET expired:list)"

echo
echo "== 结果：PASS=$PASS FAIL=$FAIL =="
[ "$FAIL" -eq 0 ] || exit 1 
