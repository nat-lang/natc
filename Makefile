BUILD_DIR := build
CURRENT_DIR := $(shell pwd)

default: nat

# Remove all build outputs and intermediate files.
clean:
	@ rm -rf $(BUILD_DIR)/release
	@ rm $(BUILD_DIR)/nat /usr/local/bin/nat nat

# Compile the interpreter.
nat:
	@ $(MAKE) -f util/c.make NAME=nat MODE=release SOURCE_DIR=src
	@ ln -s $(CURRENT_DIR)/$(BUILD_DIR)/nat /usr/local/bin/nat

debug:
	@ $(MAKE) -f util/c.make NAME=nat MODE=debug SOURCE_DIR=src
