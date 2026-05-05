#
# Makefile — Build configuration for the AI Agent
#
# Compiles the agent, LLM client, tools, UI framework, and dependencies.
# Supports both standard and AddressSanitizer builds.

CC      ?= cc
CFLAGS  := -D_XOPEN_SOURCE=700 -std=c11 -Wall -Wextra -pedantic -g -MMD -MP -I. -Ilibs
LDLIBS  := -lpthread -lssl -lcrypto

ASAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

# Source files discovery
AGENT_SRCS := $(sort $(wildcard agent/*.c))
TOOL_SRCS  := $(sort $(wildcard tools/*.c))
UI_SRCS    := $(sort $(wildcard ui/*.c))

SRCS := main.c config.c message.c util.c http.c https.c\
        $(AGENT_SRCS) $(TOOL_SRCS) $(UI_SRCS)

BUILD     := build
OBJS      := $(SRCS:%.c=$(BUILD)/%.o) $(BUILD)/cJSON.o
DEPS      := $(SRCS:%.c=$(BUILD)/%.d) $(BUILD)/cJSON.d
TARGET    := $(BUILD)/c-agent
ASAN_TGT  := $(BUILD)/c-agent-asan

.PHONY: all clean clean-objs asan

# Default target
all: $(TARGET)

# Standard build
$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDLIBS)

# Compile regular source files
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Compile cJSON (third-party library)
$(BUILD)/cJSON.o: libs/cJSON.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

# AddressSanitizer build (detects memory leaks, buffer overflows, etc.)
asan: CFLAGS += $(ASAN_FLAGS)
asan: clean-objs $(ASAN_TGT)

$(ASAN_TGT): $(OBJS)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -o $@ $^ $(LDLIBS)

# Clean object files only (preserves dependency cache)
clean-objs:
	rm -rf $(BUILD)

# Full clean
clean:
	rm -rf $(BUILD)

# Include auto-generated dependency files
-include $(DEPS)