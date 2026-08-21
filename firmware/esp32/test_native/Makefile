# The core is plain C++ with no dependencies, so the renderer and its state
# machines can be built and asserted anywhere - no toolchain download, no
# package registry, no board attached.
CORE := ../lib/tmbox_core
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -I$(CORE) -I.
CORE_SRC := $(CORE)/navigation.cpp $(CORE)/renderer.cpp $(CORE)/text.cpp $(CORE)/model.cpp

test: run-renderer run-navigation run-firmware-calls check-golden

run-renderer: build/test_renderer
	./build/test_renderer

run-navigation: build/test_navigation
	./build/test_navigation

run-firmware-calls: build/test_firmware_calls
	./build/test_firmware_calls

# Rewrites the files every other implementation of these screens is held to.
golden: build/dump_golden build/dump_traces
	./build/dump_golden > golden_frames.txt
	./build/dump_traces > golden_traces.txt
	@echo "golden_frames.txt och golden_traces.txt uppdaterade"

# Fails when the committed files stop being what the core produces.
check-golden: build/dump_golden build/dump_traces
	./build/dump_golden | diff -u golden_frames.txt -
	./build/dump_traces | diff -u golden_traces.txt -
	@echo "gyllene rutor och spar oforandrade"

# One pattern rule rather than four near-identical ones: a duplicate target is
# silent, and a silent duplicate is how check-golden stopped running once.
build/%: %.cpp $(CORE_SRC)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -rf build

.PHONY: test run-renderer run-navigation run-firmware-calls golden check-golden clean
