BUILD_DIR := build
CURRENT_DIR := $(shell pwd)
BIN := $(HOME)/bin

ifneq ($(origin NAT_BASE_DIR), environment)
	export NAT_BASE_DIR = $(HOME)/natlang/natc/
endif

default: clean dev

# Remove all build outputs and intermediate files.
clean:
	@ rm -rf $(BUILD_DIR)/release
	@ rm -rf $(BUILD_DIR)/debug
	@ rm -f $(BUILD_DIR)/nat $(BIN)/nat

configure:
	@ python $(BUILD_DIR)/configure.py

nat:
	@ $(MAKE) configure
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=release SOURCE_DIR=src

# Compile and symlink to local bin.
dev:
	@ $(MAKE) configure
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=release SOURCE_DIR=src
	@ ln -s $(BUILD_DIR)/nat $(BIN)/nat

# Compile the interpreter in debug mode.
debug:
	@ $(MAKE) configure
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=debug SOURCE_DIR=src

# Compile the interpreter with instruction and stack tracing enabled.
debug-trace:
	@ $(MAKE) configure
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=debug-trace SOURCE_DIR=src

debug-print:
	@ $(MAKE) configure
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=debug-print SOURCE_DIR=src

# Compile the interpreter with a manic garbage collector.
debug-stress-gc:
	@ $(MAKE) configure
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=debug-stress-gc SOURCE_DIR=src

# Compile the interpreter with a verbose garbage collector.
debug-log-gc:
	@ $(MAKE) configure
	@ $(MAKE) -f $(BUILD_DIR)/c.make NAME=nat MODE=debug-log-gc SOURCE_DIR=src

integration:
	@ $(BUILD_DIR)/nat test/integration/__index__

regression:
	@ $(BUILD_DIR)/nat test/regression/__index__

trip:
	@ $(BUILD_DIR)/nat test/trip/__index__

tests:
	@ $(MAKE) integration
	@ $(MAKE) regression
	@ $(MAKE) trip

# Compile with debugging enabled, sign the binary, and create a symbol map
# before running leaks against the integration tests.
test-leaks:
	@ $(MAKE) debug
	@ codesign -s - --entitlements $(BUILD_DIR)/nat.entitlements -f build/nat
	@ dsymutil build/nat
	@ MallocStackLogging=1 leaks --atExit -- $(BUILD_DIR)/nat test/integration/__index__

# Run valgrind against the integration tests.
test-valgrind:
	@ $(MAKE) clean
	@ $(MAKE) debug
	@ valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 -s build/nat test/integration/__index__

# Run valgrind against the integration tests in a container.
valgrind:
	@ docker build -t "linux" -f build/Dockerfile.linux .
	@ docker run -v $(CURRENT_DIR):/tmp -w /tmp linux sh -c "NAT_BASE_DIR=/tmp/ make test-valgrind"

# Drop us into a linux container.
linux:
	@ docker build -t "linux" -f build/Dockerfile.linux .
	@ docker run -v $(CURRENT_DIR):/tmp -w /tmp -it linux sh
