# If RACK_DIR is not defined when calling the Makefile, default to two
# directories above this plugin (standard VCV Rack plugin layout:
# Rack-SDK/plugins/BmboBTFQuantizer).
RACK_DIR ?= ../..

# FLAGS will be passed to both the C and C++ compiler
FLAGS +=
CFLAGS +=
CXXFLAGS += -std=c++17

# Careful about linking to shared libraries, since you can't assume much
# about the user's environment and library search paths.
# Static libraries are fine.
LDFLAGS +=

# Add .cpp files to the build
SOURCES += plugin.cpp
SOURCES += $(wildcard src/*.cpp)

# Add files to the ZIP package when running `make dist`
DISTRIBUTABLES += res
DISTRIBUTABLES += presets
DISTRIBUTABLES += LICENSE*

# Include the VCV Rack plugin Makefile framework
include $(RACK_DIR)/plugin.mk

# Force C++17: the SDK's own plugin.mk appends its own -std= flag after
# whatever we set above, and the LAST -std= flag on the command line wins.
# Re-asserting it here (after the include) guarantees C++17 is actually used.
CXXFLAGS += -std=c++17

