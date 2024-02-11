BUILD_DIR := build
CURRENT_DIR := $(shell pwd)

default: clean nat

# Remove all build outputs and intermediate files.
clean:
	@ rm -rf $(BUILD_DIR)/release
	@ rm -rf $(BUILD_DIR)/debug
	@ rm $(BUILD_DIR)/nat /usr/local/bin/nat

# Compile the interpreter.
nat:
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=release SOURCE_DIR=src
	@ ln -s $(CURRENT_DIR)/$(BUILD_DIR)/nat /usr/local/bin/nat

debug:
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=debug SOURCE_DIR=src
	@ ln -s $(CURRENT_DIR)/$(BUILD_DIR)/nat /usr/local/bin/nat

debug-gc:
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=debug-gc SOURCE_DIR=src
	@ ln -s $(CURRENT_DIR)/$(BUILD_DIR)/nat /usr/local/bin/nat

tests:
	@ $(CURRENT_DIR)/$(BUILD_DIR)/nat test/integration/__index__ && echo "ok"
