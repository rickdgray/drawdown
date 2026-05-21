# drawdown build
#
# Targets:
#   make / make release_debug  - default; optimized with debug info (-O2 -g -march=native)
#   make release               - fully optimized (-O3 -g -DNDEBUG -fomit-frame-pointer -march=native)
#   make debug                 - debug build (-g, no optimization)
#   make all                   - all three
#   make test                  - build and run the unit test binary
#   make compile_commands      - regenerate compile_commands.json via bear (optional, gitignored)
#   make clean                 - remove build directories

# Default to parallel builds (override with `make -j1` for single-threaded).
MAKEFLAGS += -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)

CXX      := g++-15

# On MSYS2/MinGW, g++ appends .exe to the output binary.
ifeq ($(OS),Windows_NT)
    EXE := .exe
else
    EXE :=
endif
CXXFLAGS := -std=c++26 -Iinclude -Wextra -Wall -Wuninitialized -Wno-long-long -Winit-self -pthread

RELEASE_FLAGS       := -g -DNDEBUG -O3 -fomit-frame-pointer -march=native
RELEASE_DEBUG_FLAGS := -g -O2 -march=native
DEBUG_FLAGS         := -g

# Source discovery
SRC_FILES := $(wildcard src/*.cpp)

OBJ_RELEASE       := $(SRC_FILES:src/%.cpp=release/%.o)
OBJ_RELEASE_DEBUG := $(SRC_FILES:src/%.cpp=release_debug/%.o)
OBJ_DEBUG         := $(SRC_FILES:src/%.cpp=debug/%.o)

# Dependency files for incremental builds
DEP_RELEASE       := $(OBJ_RELEASE:.o=.d)
DEP_RELEASE_DEBUG := $(OBJ_RELEASE_DEBUG:.o=.d)
DEP_DEBUG         := $(OBJ_DEBUG:.o=.d)

# Test driver: links production sources (excluding main.cpp) with the test main
TEST_SRC := tests/test_dynamic.cpp \
            $(filter-out src/main.cpp,$(SRC_FILES))
TEST_BIN := release_debug/test_dynamic$(EXE)

.PHONY: default release release_debug debug all clean test compile_commands
default: release_debug

release:       release/drawdown$(EXE)
release_debug: release_debug/drawdown$(EXE)
debug:         debug/drawdown$(EXE)
all:           release release_debug debug

# Per-mode pattern rules for compilation
release/%.o: src/%.cpp
	@mkdir -p release
	$(CXX) $(CXXFLAGS) $(RELEASE_FLAGS) -MMD -MP -c $< -o $@

release_debug/%.o: src/%.cpp
	@mkdir -p release_debug
	$(CXX) $(CXXFLAGS) $(RELEASE_DEBUG_FLAGS) -MMD -MP -c $< -o $@

debug/%.o: src/%.cpp
	@mkdir -p debug
	$(CXX) $(CXXFLAGS) $(DEBUG_FLAGS) -MMD -MP -c $< -o $@

# Linking. LDFLAGS is an extension point for CI (e.g. -static-libstdc++).
release/drawdown$(EXE): $(OBJ_RELEASE)
	@mkdir -p release
	$(CXX) $(CXXFLAGS) $(RELEASE_FLAGS) -o $@ $^ $(LDFLAGS)

release_debug/drawdown$(EXE): $(OBJ_RELEASE_DEBUG)
	@mkdir -p release_debug
	$(CXX) $(CXXFLAGS) $(RELEASE_DEBUG_FLAGS) -o $@ $^ $(LDFLAGS)

debug/drawdown$(EXE): $(OBJ_DEBUG)
	@mkdir -p debug
	$(CXX) $(CXXFLAGS) $(DEBUG_FLAGS) -o $@ $^ $(LDFLAGS)

# Test target: compile the test main + production sources (no main.cpp)
$(TEST_BIN): $(TEST_SRC) $(wildcard include/*.hpp)
	@mkdir -p release_debug
	$(CXX) $(CXXFLAGS) $(RELEASE_DEBUG_FLAGS) -o $@ $(TEST_SRC)

test: $(TEST_BIN)
	$(TEST_BIN)

clean:
	rm -rf release release_debug debug

# Regenerate compile_commands.json using `bear`. compile_commands.json is
# gitignored — this is a local convenience for IDEs (clangd, VS Code C/C++
# extension, CLion). Skips gracefully if bear is not installed.
compile_commands:
	@if command -v bear >/dev/null 2>&1; then \
	    rm -f compile_commands.json && bear -- $(MAKE) -B release_debug; \
	else \
	    echo "compile_commands: 'bear' not installed; skipping."; \
	    echo "Install via: apt install bear (or brew install bear)"; \
	fi

# Include generated dependency files
-include $(DEP_RELEASE) $(DEP_RELEASE_DEBUG) $(DEP_DEBUG)
