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

# Compile and symlink to local bin.
dev:
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=release SOURCE_DIR=src
	@ ln -s $(CURRENT_DIR)/$(BUILD_DIR)/nat $(BIN)/nat

# Compile the interpreter with verbosity=debug.
debug:
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=debug SOURCE_DIR=src
	@ ln -s $(CURRENT_DIR)/$(BUILD_DIR)/nat $(BIN)/nat

# Compile the interpreter with a manic garbage collector.
debug-gc:
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=debug-gc SOURCE_DIR=src
	@ ln -s $(CURRENT_DIR)/$(BUILD_DIR)/nat $(BIN)/nat

# Compile the interpreter and run the tests.
ci:
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=release SOURCE_DIR=src
	@ $(CURRENT_DIR)/$(BUILD_DIR)/nat test/integration/__index__ && echo "ok"

# Run all the tests.
tests:
	@ $(CURRENT_DIR)/$(BUILD_DIR)/nat test/integration/__index__ && echo "ok"

test-leaks:
	@ leaks --atExit -- $(CURRENT_DIR)/$(BUILD_DIR)/nat test/integration/__index__ && echo "ok"
