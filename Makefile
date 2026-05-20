# swr_calculator build
#
# Targets:
#   make / make release_debug  - default; optimized with debug info (-O2 -g -march=native)
#   make release               - fully optimized (-O3 -g -DNDEBUG -fomit-frame-pointer -march=native)
#   make debug                 - debug build (-g, no optimization)
#   make all                   - all three
#   make test                  - build and run the unit test binary
#   make clean                 - remove build directories

CXX      := g++-15
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
TEST_BIN := release_debug/test_dynamic

.PHONY: default release release_debug debug all clean test
default: release_debug

release:       release/swr_calculator
release_debug: release_debug/swr_calculator
debug:         debug/swr_calculator
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

# Linking
release/swr_calculator: $(OBJ_RELEASE)
	@mkdir -p release
	$(CXX) $(CXXFLAGS) $(RELEASE_FLAGS) -o $@ $^

release_debug/swr_calculator: $(OBJ_RELEASE_DEBUG)
	@mkdir -p release_debug
	$(CXX) $(CXXFLAGS) $(RELEASE_DEBUG_FLAGS) -o $@ $^

debug/swr_calculator: $(OBJ_DEBUG)
	@mkdir -p debug
	$(CXX) $(CXXFLAGS) $(DEBUG_FLAGS) -o $@ $^

# Test target: compile the test main + production sources (no main.cpp)
$(TEST_BIN): $(TEST_SRC) $(wildcard include/*.hpp)
	@mkdir -p release_debug
	$(CXX) $(CXXFLAGS) $(RELEASE_DEBUG_FLAGS) -o $@ $(TEST_SRC)

test: $(TEST_BIN)
	$(TEST_BIN)

clean:
	rm -rf release release_debug debug

# Include generated dependency files
-include $(DEP_RELEASE) $(DEP_RELEASE_DEBUG) $(DEP_DEBUG)
