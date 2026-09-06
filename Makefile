# 龙顺 AI 销售与客户运营助手 - POSIX 构建
CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -Iinclude
LDFLAGS ?= -lpthread

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:.c=.o)

TARGET := longshun

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
