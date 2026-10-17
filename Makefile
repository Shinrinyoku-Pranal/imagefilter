CXX ?= c++
CPPFLAGS += -Iinclude -isystem third_party
CXXFLAGS ?= -O3 -DNDEBUG
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -pthread
LDFLAGS += -pthread
BUILD ?= build
LIB_SOURCES := src/image.cpp src/filters.cpp src/thread_pool.cpp src/codecs.cpp
LIB_OBJECTS := $(LIB_SOURCES:src/%.cpp=$(BUILD)/%.o)
DEPS := $(LIB_OBJECTS:.o=.d) $(BUILD)/main.d $(BUILD)/tests.d

.PHONY: all test sanitize benchmark clean
all: $(BUILD)/imagefilter

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/tests.o: tests/tests.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/imagefilter: $(LIB_OBJECTS) $(BUILD)/main.o
	$(CXX) $^ $(LDFLAGS) -o $@

$(BUILD)/imagefilter_tests: $(LIB_OBJECTS) $(BUILD)/tests.o
	$(CXX) $^ $(LDFLAGS) -o $@

test: $(BUILD)/imagefilter $(BUILD)/imagefilter_tests
	$(BUILD)/imagefilter_tests
	python3 tests/cli_test.py $(BUILD)/imagefilter
	python3 tests/scripts_test.py $(BUILD)/imagefilter

benchmark: $(BUILD)/imagefilter
	python3 scripts/benchmark.py --binary $(BUILD)/imagefilter

sanitize:
	$(MAKE) BUILD=build-sanitize CXXFLAGS='-std=c++17 -O1 -g -Wall -Wextra -Wpedantic -pthread -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-pthread -fsanitize=address,undefined' test

clean:
	rm -rf build build-sanitize

-include $(DEPS)
