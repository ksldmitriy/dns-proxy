SRC_DIR := src
BUILD_DIR := build
BIN_DIR := bin
TOML_DIR := tomlc99

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

CC := gcc
CFLAGS := -Wextra -I./$(TOML_DIR)
LDFLAGS := -L./$(TOML_DIR) -ltoml

TARGET := $(BIN_DIR)/dns-proxy-server

all: $(TOML_DIR)/libtoml.a $(TARGET)

$(TOML_DIR)/libtoml.a:
	$(MAKE) -C $(TOML_DIR)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR) $(BIN_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	$(MAKE) -C $(TOML_DIR) clean

.PHONY: all clean

