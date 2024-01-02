BUILD_DIR := build

default: nat

# Remove all build outputs and intermediate files.
clean:
	@ rm -rf $(BUILD_DIR)/release
	@ rm $(BUILD_DIR)/nat
	@ rm nat

# Compile the interpreter.
nat:
	@ $(MAKE) -f util/c.make NAME=nat MODE=release SOURCE_DIR=src
	@ cp $(BUILD_DIR)/nat nat # For convenience, copy the interpreter to the top level.