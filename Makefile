default: release_debug

.PHONY: default release debug all clean test

include make-utils/flags.mk
include make-utils/cpp-utils.mk

# Use C++26
$(eval $(call use_cpp26))

CXX_FLAGS += -pthread

$(eval $(call auto_folder_compile,src))
$(eval $(call auto_add_executable,swr_calculator))

release_debug: release_debug_swr_calculator
release: release_swr_calculator
debug: debug_swr_calculator

all: release release_debug debug

clean:
	rm -rf release/
	rm -rf release_debug/
	rm -rf debug/

# Home-grown test executable. Test driver lives in tests/ so it's not
# picked up by the src/ glob; production .cpp files are linked in explicitly.
TEST_SRC := tests/test_dynamic.cpp src/dynamic.cpp src/data.cpp \
            src/portfolio.cpp src/simulation.cpp src/cli.cpp \
            src/output_formatter.cpp
TEST_BIN := release_debug/test_dynamic

$(TEST_BIN): $(TEST_SRC) include/test_assert.hpp include/dynamic.hpp \
             include/data.hpp include/portfolio.hpp include/simulation.hpp \
             include/cli.hpp include/output_formatter.hpp
	@mkdir -p release_debug
	$(cxx) $(CXX_FLAGS) -Iinclude -o $@ $(TEST_SRC)

.PHONY: test
test: $(TEST_BIN)
	$(TEST_BIN)

include make-utils/cpp-utils-finalize.mk
