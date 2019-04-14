# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../..

# Must follow the format in the Naming section of
# https://vcvrack.com/manual/PluginDevelopmentTutorial.html
SLUG = Template

# Must follow the format in the Versioning section of
# https://vcvrack.com/manual/PluginDevelopmentTutorial.html
VERSION = 0.6.0

# FLAGS will be passed to both the C and C++ compiler
FLAGS +=
CFLAGS +=
CXXFLAGS += -Isrc/deps/libbinio/src

# Careful about linking to shared libraries, since you can't assume much about the user's environment and library search path.
# Static libraries are fine.
LDFLAGS +=

# Add .cpp and .c files to the build
SOURCES += $(wildcard src/*.cpp) src/deps/adlmidi/src/dbopl.cpp

DEPS_LIBS = src/deps/libadplug.a src/deps/libbinio.a
OBJECTS += $(DEPS_LIBS)

# Add files to the ZIP package when running `make dist`
# The compiled plugin is automatically added.
DISTRIBUTABLES += $(wildcard LICENSE*) res

deps: $(DEPS_LIBS)

depsclean:
	rm -f $(DEPS_LIBS)
	(cd src/deps/libbinio && make clean)
	(cd src/deps/adplug && make clean)

src/deps/libbinio.a: $(wildcard src/deps/libbinio/**)
	(cd src/deps/libbinio/ && ./autogen.sh && ./configure --with-pic --enable-static && touch ./doc/version.texi && touch ./doc/version-remake.texi && make -j4 || true)
	cp src/deps/libbinio/src/.libs/libbinio.a src/deps/libbinio.a

src/deps/libadplug.a: $(wildcard src/deps/adplug/**)
	(cd src/deps/adplug/ && ./autogen.sh && ./configure --with-pic --enable-static && make -j4)
	cp src/deps/adplug/src/.libs/libadplug.a src/deps/libadplug.a

# Include the VCV Rack plugin Makefile framework
include $(RACK_DIR)/plugin.mk
