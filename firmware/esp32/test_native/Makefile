# The core is plain C++17 with no dependencies, so the renderer and its state
# machines can be built and asserted anywhere - no toolchain download, no
# package registry, no board attached.
CORE := ../lib/tmbox_core
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -I$(CORE) -I.

test: run-renderer run-navigation check-golden

run-renderer: build/test_renderer
	./build/test_renderer

build/test_renderer: test_renderer.cpp $(CORE)/renderer.cpp $(CORE)/text.cpp $(CORE)/model.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@

run-navigation: build/test_navigation
	./build/test_navigation

build/test_navigation: test_navigation.cpp $(CORE)/navigation.cpp $(CORE)/renderer.cpp $(CORE)/text.cpp $(CORE)/model.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@

golden: build/dump_golden
	./build/dump_golden > golden_frames.txt
	@echo "golden_frames.txt uppdaterad"

# CI kontrollerar att den incheckade filen fortfarande ar vad renderaren ger.
check-golden: build/dump_golden
	./build/dump_golden | diff -u golden_frames.txt - && echo "gyllene rutor oforandrade"

build/dump_golden: dump_golden.cpp $(CORE)/renderer.cpp $(CORE)/text.cpp $(CORE)/model.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -rf build

.PHONY: test run-renderer run-navigation golden check-golden clean
