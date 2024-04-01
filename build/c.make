# Makefile for building a single configuration of the C interpreter. It expects
# variables to be passed in for:
#
# MODE         "debug" or "release".
# NAME         Name of the output executable (and object file directory).
# SOURCE_DIR   Directory where source files and headers are found.

ifeq ($(CPP),true)
	# Ideally, we'd add -pedantic-errors, but the use of designated initializers
	# means nat relies on some GCC/Clang extensions to compile as C++.
	CFLAGS := -std=c++11
	C_LANG := -x c++
else
	CFLAGS := -std=c99
endif

CFLAGS += -Wall -Wextra -Werror -Wno-unused-parameter

# Mode configuration.
ifeq ($(MODE),debug)
	CFLAGS += -O0 -DDEBUG -g
	BUILD_DIR := build/debug
else ifeq ($(MODE),debug-trace)
	CFLAGS += -O0 -DDEBUG -g -D DEBUG_PRINT_CODE -D DEBUG_TRACE_EXECUTION
	BUILD_DIR := build/debug
else ifeq ($(MODE),debug-log-gc)
	CFLAGS += -O0 -DDEBUG -g -D DEBUG_LOG_GC
	BUILD_DIR := build/debug
else ifeq ($(MODE),debug-stress-gc)
	CFLAGS += -O0 -DDEBUG -g -D DEBUG_STRESS_GC
	BUILD_DIR := build/debug
else
	CFLAGS += -O3 -flto
	BUILD_DIR := build/release
endif

# Files.
HEADERS := $(wildcard $(SOURCE_DIR)/*.h)
SOURCES := $(wildcard $(SOURCE_DIR)/*.c)
OBJECTS := $(addprefix $(BUILD_DIR)/$(NAME)/, $(notdir $(SOURCES:.c=.o)))

# Targets ---------------------------------------------------------------------

# Link the interpreter.
build/$(NAME): $(OBJECTS)
	@ printf "%8s %-40s %s\n" $(CC) $@ "$(CFLAGS)"
	@ mkdir -p build
	@ $(CC) $(CFLAGS) $^ -o $@ -lm

# Compile object files.
$(BUILD_DIR)/$(NAME)/%.o: $(SOURCE_DIR)/%.c $(HEADERS)
	@ printf "%8s %-40s %s\n" $(CC) $< "$(CFLAGS)"
	@ mkdir -p $(BUILD_DIR)/$(NAME)
	@ $(CC) -c $(C_LANG) $(CFLAGS) -o $@ $<

.PHONY: default
