BUILD_DIR := build
CURRENT_DIR := $(shell pwd)
BIN := $(HOME)/bin

default: clean dev

# Remove all build outputs and intermediate files.
clean:
	@ rm -rf $(BUILD_DIR)/release
	@ rm -rf $(BUILD_DIR)/debug
	@ rm $(BUILD_DIR)/nat $(BIN)/nat

# Compile the interpreter.
nat:
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=release SOURCE_DIR=src

# Compile the interpreter in debug mode.
nat-debug:
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=debug SOURCE_DIR=src

# Compile and symlink to local bin.
dev:
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=release SOURCE_DIR=src
	@ ln -s $(CURRENT_DIR)/$(BUILD_DIR)/nat $(BIN)/nat

# Compile the interpreter in debug mode and symlink to local bin.
debug:
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=debug SOURCE_DIR=src
	@ ln -s $(CURRENT_DIR)/$(BUILD_DIR)/nat $(BIN)/nat

# Compile the interpreter with a manic garbage collector.
debug-gc:
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=debug-gc SOURCE_DIR=src
	@ ln -s $(CURRENT_DIR)/$(BUILD_DIR)/nat $(BIN)/nat

# Run all the tests.
integration:
	@ $(CURRENT_DIR)/$(BUILD_DIR)/nat test/integration/__index__ && echo "ok"

# Run all the tests.
trip:
	@ $(CURRENT_DIR)/$(BUILD_DIR)/nat test/trip/__index__ && echo "ok"

# Run all the tests.
tests:
	@ $(CURRENT_DIR)/$(BUILD_DIR)/nat test/integration/__index__ && echo "integration ok"
	@ $(CURRENT_DIR)/$(BUILD_DIR)/nat test/trip/__index__ && echo "trip        ok"

test-leaks:
	@ leaks --atExit -- $(CURRENT_DIR)/$(BUILD_DIR)/nat test/integration/__index__ && echo "ok"
