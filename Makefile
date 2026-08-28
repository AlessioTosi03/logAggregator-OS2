CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99 -pthread -Iinclude -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -g
LDFLAGS = -pthread

SRC_DIR = src
INC_DIR = include
TEST_DIR = tests
BIN_DIR = bin

TARGETS = $(BIN_DIR)/coordinator $(BIN_DIR)/producer

all: $(BIN_DIR) $(TARGETS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/common.o: $(SRC_DIR)/common.c $(INC_DIR)/common.h
	$(CC) $(CFLAGS) -c $(SRC_DIR)/common.c -o $(BIN_DIR)/common.o

$(BIN_DIR)/coordinator: $(SRC_DIR)/coordinator.c $(BIN_DIR)/common.o
	$(CC) $(CFLAGS) $(SRC_DIR)/coordinator.c $(BIN_DIR)/common.o -o $(BIN_DIR)/coordinator $(LDFLAGS)

$(BIN_DIR)/producer: $(SRC_DIR)/producer.c $(BIN_DIR)/common.o
	$(CC) $(CFLAGS) $(SRC_DIR)/producer.c $(BIN_DIR)/common.o -o $(BIN_DIR)/producer $(LDFLAGS)

clean:
	rm -rf $(BIN_DIR) *.log *.bak

.PHONY: all clean
