CXX ?= c++
CPPFLAGS := -Isrc
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror
BUILD := build
CORE_SOURCES := src/application.cpp src/navigation.cpp
CORE_OBJECTS := $(CORE_SOURCES:src/%.cpp=$(BUILD)/%.o)

.PHONY: all test test-unit test-sanitize smoke evidence clean
all: $(BUILD)/vant

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD)/vant: src/main.cpp $(CORE_OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD)/test_application: tests/application.cpp $(CORE_OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD)/test_navigation: tests/navigation.cpp $(CORE_OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

test-unit: $(BUILD)/test_application $(BUILD)/test_navigation
	./$(BUILD)/test_application
	./$(BUILD)/test_navigation

smoke: $(BUILD)/vant
	./$(BUILD)/vant --headless-smoke

test: test-unit smoke

test-sanitize:
	mkdir -p $(BUILD)/san
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror tests/application.cpp $(CORE_SOURCES) -o $(BUILD)/san/test_application
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror tests/navigation.cpp $(CORE_SOURCES) -o $(BUILD)/san/test_navigation
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/san/test_application
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/san/test_navigation

evidence: all
	python3 tools/record_environment.py --output $(BUILD)/environment.json

clean:
	rm -rf $(BUILD)
