CXX ?= c++
CPPFLAGS := -Isrc
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror
NATIVE_CFLAGS := $(shell pkg-config --cflags gtk4 webkitgtk-6.0)
NATIVE_LIBS := $(shell pkg-config --libs gtk4 webkitgtk-6.0)
BUILD := build
CORE_SOURCES := src/application.cpp src/navigation.cpp src/browser_model.cpp
CORE_OBJECTS := $(CORE_SOURCES:src/%.cpp=$(BUILD)/%.o)

.PHONY: all test test-unit test-sanitize smoke smoke-native evidence clean
all: $(BUILD)/vant

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD)/native_app.o: src/native_app.cpp src/native_app.h | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(NATIVE_CFLAGS) -c $< -o $@

$(BUILD)/vant: src/main.cpp $(CORE_OBJECTS) $(BUILD)/native_app.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(NATIVE_CFLAGS) $^ $(NATIVE_LIBS) -o $@

$(BUILD)/test_application: tests/application.cpp $(CORE_OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD)/test_navigation: tests/navigation.cpp $(CORE_OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD)/test_browser_model: tests/browser_model.cpp $(CORE_OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

test-unit: $(BUILD)/test_application $(BUILD)/test_navigation $(BUILD)/test_browser_model
	./$(BUILD)/test_application
	./$(BUILD)/test_navigation
	./$(BUILD)/test_browser_model

smoke: $(BUILD)/vant
	./$(BUILD)/vant --headless-smoke
	./$(BUILD)/vant --native-probe

smoke-native: $(BUILD)/vant
	env WEBKIT_DISABLE_COMPOSITING_MODE=1 ./$(BUILD)/vant --native-smoke

test: test-unit smoke

test-sanitize:
	mkdir -p $(BUILD)/san
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror tests/application.cpp $(CORE_SOURCES) -o $(BUILD)/san/test_application
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror tests/navigation.cpp $(CORE_SOURCES) -o $(BUILD)/san/test_navigation
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror tests/browser_model.cpp $(CORE_SOURCES) -o $(BUILD)/san/test_browser_model
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/san/test_application
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/san/test_navigation
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/san/test_browser_model

evidence: all
	python3 tools/record_environment.py --output $(BUILD)/environment.json

clean:
	rm -rf $(BUILD)
