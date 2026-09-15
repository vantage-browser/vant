CXX ?= c++
PKG_CONFIG ?= pkg-config
CPPFLAGS := -Isrc
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror
REQUIRED_PACKAGES := gtk4 webkitgtk-6.0 sqlite3
NATIVE_CFLAGS = $(shell $(PKG_CONFIG) --cflags gtk4 webkitgtk-6.0)
NATIVE_LIBS = $(shell $(PKG_CONFIG) --libs gtk4 webkitgtk-6.0)
SQLITE_CFLAGS = $(shell $(PKG_CONFIG) --cflags sqlite3)
SQLITE_LIBS = $(shell $(PKG_CONFIG) --libs sqlite3)
BUILD := build
AGENT_SOURCES := src/agent_rpc.cpp
AGENT_OBJECTS := $(AGENT_SOURCES:src/%.cpp=$(BUILD)/%.o)
CORE_SOURCES := src/application.cpp src/navigation.cpp src/browser_model.cpp src/session_store.cpp src/user_data.cpp src/preferences.cpp src/launch_options.cpp
CORE_OBJECTS := $(CORE_SOURCES:src/%.cpp=$(BUILD)/%.o)
JSPP_DIR := third_party/jspp
JSPP_SOURCES := $(filter-out $(JSPP_DIR)/src/main.cpp,$(wildcard $(JSPP_DIR)/src/*.cpp))
JSPP_OBJECTS := $(JSPP_SOURCES:$(JSPP_DIR)/src/%.cpp=$(BUILD)/jspp/%.o)

.PHONY: all deps test test-unit test-sanitize smoke smoke-native benchmark evidence vendor-check vendor-integrity clean
all: deps $(BUILD)/vant

deps:
	@if ! command -v "$(PKG_CONFIG)" >/dev/null 2>&1; then \
		echo "error: pkg-config is required." >&2; \
		echo "Ubuntu: sudo apt install build-essential pkg-config python3 libgtk-4-dev libwebkitgtk-6.0-dev libsqlite3-dev" >&2; \
		echo "Omarchy/Arch: sudo pacman -S --needed base-devel pkgconf python gtk4 webkitgtk-6.0 sqlite" >&2; \
		exit 1; \
	fi
	@if ! $(PKG_CONFIG) --exists $(REQUIRED_PACKAGES); then \
		echo "error: Vantage development packages are missing." >&2; \
		echo "Ubuntu: sudo apt install build-essential pkg-config python3 libgtk-4-dev libwebkitgtk-6.0-dev libsqlite3-dev" >&2; \
		echo "Omarchy/Arch: sudo pacman -S --needed base-devel pkgconf python gtk4 webkitgtk-6.0 sqlite" >&2; \
		echo "Then run: pkg-config --modversion $(REQUIRED_PACKAGES)" >&2; \
		exit 1; \
	fi

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.cpp | deps $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD)/native_app.o: src/native_app.cpp src/native_app.h | deps $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(NATIVE_CFLAGS) -c $< -o $@

$(BUILD)/jspp:
	mkdir -p $(BUILD)/jspp

$(BUILD)/jspp/%.o: $(JSPP_DIR)/src/%.cpp | $(BUILD)/jspp
	$(CXX) -I$(JSPP_DIR)/src -I$(JSPP_DIR)/include $(CXXFLAGS) -fvisibility=hidden -c $< -o $@

$(BUILD)/jspp_adapter.o: src/jspp_adapter.cpp src/jspp_adapter.h | $(BUILD)
	$(CXX) $(CPPFLAGS) -I$(JSPP_DIR)/include $(CXXFLAGS) -c $< -o $@

$(BUILD)/vant: src/main.cpp $(CORE_OBJECTS) $(AGENT_OBJECTS) $(BUILD)/native_app.o $(BUILD)/jspp_adapter.o $(JSPP_OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(NATIVE_CFLAGS) $(SQLITE_CFLAGS) $^ $(NATIVE_LIBS) $(SQLITE_LIBS) -o $@


$(BUILD)/test_agent_rpc: tests/agent_rpc.cpp $(BUILD)/agent_rpc.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -pthread -o $@

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

$(BUILD)/test_preferences: tests/preferences.cpp $(BUILD)/preferences.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD)/test_launch_options: tests/launch_options.cpp $(BUILD)/launch_options.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD)/test_version: tests/version.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(BUILD)/test_jspp_adapter: tests/jspp_adapter.cpp $(BUILD)/jspp_adapter.o $(JSPP_OBJECTS)
	$(CXX) $(CPPFLAGS) -I$(JSPP_DIR)/include $(CXXFLAGS) $^ -pthread -o $@

$(BUILD)/automation.o: src/automation.cpp src/automation.h | $(BUILD)
	$(CXX) $(CPPFLAGS) -I$(JSPP_DIR)/include $(CXXFLAGS) -c $< -o $@

$(BUILD)/test_automation: tests/automation.cpp $(BUILD)/automation.o $(CORE_OBJECTS) $(JSPP_OBJECTS)
	$(CXX) $(CPPFLAGS) -I$(JSPP_DIR)/include $(CXXFLAGS) $^ $(SQLITE_LIBS) -pthread -o $@

test-unit: $(BUILD)/test_agent_rpc $(BUILD)/test_application $(BUILD)/test_navigation $(BUILD)/test_browser_model $(BUILD)/test_session_store $(BUILD)/test_user_data $(BUILD)/test_preferences $(BUILD)/test_launch_options $(BUILD)/test_version $(BUILD)/test_jspp_adapter $(BUILD)/test_automation
	./$(BUILD)/test_agent_rpc
	./$(BUILD)/test_application
	./$(BUILD)/test_navigation
	./$(BUILD)/test_browser_model
	./$(BUILD)/test_session_store
	./$(BUILD)/test_user_data
	./$(BUILD)/test_preferences
	./$(BUILD)/test_launch_options
	./$(BUILD)/test_version
	python3 tests/desktop_entry.py
	python3 tests/agent_security.py
	./$(BUILD)/test_jspp_adapter
	./$(BUILD)/test_automation

smoke: $(BUILD)/vant
	./$(BUILD)/vant --headless-smoke
	./$(BUILD)/vant --native-probe

smoke-native: $(BUILD)/vant
	env WEBKIT_DISABLE_COMPOSITING_MODE=1 ./$(BUILD)/vant --native-smoke

test: test-unit smoke

test-sanitize: deps
	mkdir -p $(BUILD)/san
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror tests/application.cpp $(CORE_SOURCES) $(SQLITE_LIBS) -o $(BUILD)/san/test_application
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror tests/navigation.cpp $(CORE_SOURCES) $(SQLITE_LIBS) -o $(BUILD)/san/test_navigation
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror tests/browser_model.cpp $(CORE_SOURCES) $(SQLITE_LIBS) -o $(BUILD)/san/test_browser_model
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror tests/session_store.cpp $(CORE_SOURCES) $(SQLITE_LIBS) -o $(BUILD)/san/test_session_store
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror tests/user_data.cpp $(CORE_SOURCES) $(SQLITE_LIBS) -o $(BUILD)/san/test_user_data
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror tests/preferences.cpp src/preferences.cpp -o $(BUILD)/san/test_preferences
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror tests/launch_options.cpp src/launch_options.cpp -o $(BUILD)/san/test_launch_options
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/san/test_application
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/san/test_navigation
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/san/test_browser_model
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/san/test_session_store
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/san/test_user_data
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/san/test_preferences
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/san/test_launch_options
	$(CXX) $(CPPFLAGS) -I$(JSPP_DIR)/include -I$(JSPP_DIR)/src -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror tests/jspp_adapter.cpp src/jspp_adapter.cpp $(JSPP_SOURCES) -pthread -o $(BUILD)/san/test_jspp_adapter
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/san/test_jspp_adapter
	$(CXX) $(CPPFLAGS) -I$(JSPP_DIR)/include -I$(JSPP_DIR)/src -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror tests/automation.cpp src/automation.cpp $(CORE_SOURCES) $(JSPP_SOURCES) $(SQLITE_LIBS) -pthread -o $(BUILD)/san/test_automation
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/san/test_automation

vendor-check:
	python3 tools/vendor_jspp.py --check

vendor-integrity:
	python3 tools/check_vendor_integrity.py

evidence: deps all
	python3 tools/record_environment.py --output $(BUILD)/environment.json

$(BUILD)/benchmark_model: benchmarks/model.cpp $(CORE_OBJECTS) | deps
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SQLITE_CFLAGS) $^ $(SQLITE_LIBS) -o $@

benchmark: $(BUILD)/benchmark_model
	./$(BUILD)/benchmark_model

clean:
	rm -rf $(BUILD)
