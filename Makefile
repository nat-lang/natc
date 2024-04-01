BUILD_DIR := build
CURRENT_DIR := $(shell pwd)
BIN := $(HOME)/bin
HAS_LEAKS := false

default: clean dev

# Remove all build outputs and intermediate files.
clean:
	@ rm -rf $(BUILD_DIR)/release
	@ rm -rf $(BUILD_DIR)/debug
	@ rm -f $(BUILD_DIR)/nat $(BIN)/nat

# Compile the interpreter.
nat:
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=release SOURCE_DIR=src

# Compile and symlink to local bin.
dev:
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=release SOURCE_DIR=src
	@ ln -s $(BUILD_DIR)/nat $(BIN)/nat

# Compile the interpreter in debug mode.
debug:
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=debug SOURCE_DIR=src

# Compile the interpreter with instruction and stack tracing enabled.
debug-trace:
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=debug-trace SOURCE_DIR=src

# Compile the interpreter with a manic garbage collector.
debug-stress-gc:
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=debug-stress-gc SOURCE_DIR=src

# Compile the interpreter
debug-log-gc:
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=debug-log-gc SOURCE_DIR=src

integration:
	@ $(BUILD_DIR)/nat test/integration/__index__ && echo "ok"

trip:
	@ $(BUILD_DIR)/nat test/trip/__index__ && echo "ok"

tests:
	@ $(BUILD_DIR)/nat test/integration/__index__
	@ $(BUILD_DIR)/nat test/trip/__index__

# Compile with debugging enabled, sign the binary, and create a symbol map
# before running leaks against the integration tests.
test-leaks:
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=debug SOURCE_DIR=src
	@ codesign -s - --entitlements $(BUILD_DIR)/nat.entitlements -f build/nat
	@ dsymutil build/nat
	@ MallocStackLogging=1 leaks --atExit -- $(BUILD_DIR)/nat test/integration/__index__

# Run valgrind against the integration tests.
test-valgrind:
	@ $(MAKE) debug
	@ valgrind --leak-check=full --track-origins=yes --error-exitcode=1 -s build/nat test/integration/__index__

# Run valgrind against the integration tests in a container.
valgrind:
	@ docker build -t "valgrind" -f build/Dockerfile.valgrind .
	@ docker run -v $(CURRENT_DIR):/tmp -w /tmp valgrind sh -c "make test-valgrind"