#!/bin/sh
# 龙顺 AI 销售与客户运营助手 - Linux/macOS 构建脚本
# 用法: ./build.sh [clean|all]
set -e

cd "$(dirname "$0")"

CC="${CC:-cc}"
TARGET=longshun
OBJDIR=build

case "${1:-all}" in
    clean)
        rm -rf "$OBJDIR" "$TARGET"
        echo "[build] 已清理"
        exit 0
        ;;
    all|*)
        ;;
esac

mkdir -p "$OBJDIR"

echo "[build] 编译 (CC=$CC) ..."
SRCS=$(ls src/*.c)
OBJS=""
for s in $SRCS; do
    o="$OBJDIR/$(basename "${s%.c}").o"
    "$CC" -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -O2 -Iinclude -c "$s" -o "$o"
    OBJS="$OBJS $o"
done

echo "[build] 链接 $TARGET ..."
"$CC" -o "$TARGET" $OBJS -lpthread

echo "[build] 完成: ./$TARGET"
echo "  单机模式:  ./$TARGET rooms | venues | quote ..."
echo "  服务端模式: ./$TARGET server [port]"
echo "  客户端模式: ./$TARGET client [-h host] [-p port] <cmd> [args...]"
