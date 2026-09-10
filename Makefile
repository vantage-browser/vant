CXX ?= c++
CPPFLAGS := -Isrc
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror
NATIVE_CFLAGS := $(shell pkg-config --cflags gtk4 webkitgtk-6.0)
NATIVE_LIBS := $(shell pkg-config --libs gtk4 webkitgtk-6.0)
SQLITE_CFLAGS := $(shell pkg-config --cflags sqlite3)
SQLITE_LIBS := $(shell pkg-config --libs sqlite3)
BUILD := build
CORE_SOURCES := src/application.cpp src/navigation.cpp src/browser_model.cpp src/session_store.cpp src/user_data.cpp
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
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(NATIVE_CFLAGS) $(SQLITE_CFLAGS) $^ $(NATIVE_LIBS) $(SQLITE_LIBS) -o $@

$(BUILD)/test_application: tests/application.cpp $(CORE_OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SQLITE_CFLAGS) $^ $(SQLITE_LIBS) -o $@

$(BUILD)/test_navigation: tests/navigation.cpp $(CORE_OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SQLITE_CFLAGS) $^ $(SQLITE_LIBS) -o $@

$(BUILD)/test_browser_model: tests/browser_model.cpp $(CORE_OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SQLITE_CFLAGS) $^ $(SQLITE_LIBS) -o $@

$(BUILD)/test_session_store: tests/session_store.cpp $(CORE_OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SQLITE_CFLAGS) $^ $(SQLITE_LIBS) -o $@

$(BUILD)/test_user_data: tests/user_data.cpp $(CORE_OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SQLITE_CFLAGS) $^ $(SQLITE_LIBS) -o $@

test-unit: $(BUILD)/test_application $(BUILD)/test_navigation $(BUILD)/test_browser_model $(BUILD)/test_session_store $(BUILD)/test_user_data
	./$(BUILD)/test_application
	./$(BUILD)/test_navigation
	./$(BUILD)/test_browser_model
	./$(BUILD)/test_session_store
	./$(BUILD)/test_user_data

smoke: $(BUILD)/vant
	./$(BUILD)/vant --headless-smoke
	./$(BUILD)/vant --native-probe

smoke-native: $(BUILD)/vant
	env WEBKIT_DISABLE_COMPOSITING_MODE=1 ./$(BUILD)/vant --native-smoke

test: test-unit smoke

test-sanitize:
	mkdir -p $(BUILD)/san
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror tests/application.cpp $(CORE_SOURCES) $(SQLITE_LIBS) -o $(BUILD)/san/test_application
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror tests/navigation.cpp $(CORE_SOURCES) $(SQLITE_LIBS) -o $(BUILD)/san/test_navigation
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror tests/browser_model.cpp $(CORE_SOURCES) $(SQLITE_LIBS) -o $(BUILD)/san/test_browser_model
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror tests/session_store.cpp $(CORE_SOURCES) $(SQLITE_LIBS) -o $(BUILD)/san/test_session_store
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror tests/user_data.cpp $(CORE_SOURCES) $(SQLITE_LIBS) -o $(BUILD)/san/test_user_data
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/san/test_application
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/san/test_navigation
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/san/test_browser_model
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/san/test_session_store
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/san/test_user_data

evidence: all
	python3 tools/record_environment.py --output $(BUILD)/environment.json

clean:
	rm -rf $(BUILD)
