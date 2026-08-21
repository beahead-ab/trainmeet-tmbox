# The core is plain C++17 with no dependencies, so the renderer and its state
# machines can be built and asserted anywhere - no toolchain download, no
# package registry, no board attached.
CORE := ../lib/tmbox_core
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -I$(CORE) -I.

test: run-renderer

run-renderer: build/test_renderer
	./build/test_renderer

build/test_renderer: test_renderer.cpp $(CORE)/renderer.cpp $(CORE)/text.cpp $(CORE)/model.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -rf build

.PHONY: test run-renderer clean
