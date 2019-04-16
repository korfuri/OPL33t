# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../..

# Must follow the format in the Naming section of
# https://vcvrack.com/manual/PluginDevelopmentTutorial.html
SLUG = OPL33t

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

LIB_EXTENSION ?= a
DEPS_LIBS = src/deps/libadplug.$(LIB_EXTENSION) src/deps/libbinio.$(LIB_EXTENSION)
OBJECTS += $(DEPS_LIBS)

# Add files to the ZIP package when running `make dist`
# The compiled plugin is automatically added.
DISTRIBUTABLES += $(wildcard LICENSE*) res

deps: $(DEPS_LIBS)

depsclean:
	rm -f $(DEPS_LIBS)
	(cd src/deps/libbinio && make clean)
	(cd src/deps/adplug && make clean)

src/deps/libbinio.$(LIB_EXTENSION): $(wildcard src/deps/libbinio/**)
	echo "=== Building libbinio ==="
	(cd src/deps/libbinio/ && autoreconf --install && ./configure --with-pic --enable-static && touch ./doc/version.texi && touch ./doc/version-remake.texi && make -j4 -k || true)
	cp src/deps/libbinio/src/.libs/libbinio.$(LIB_EXTENSION) src/deps/libbinio.$(LIB_EXTENSION)

src/deps/libadplug.$(LIB_EXTENSION): $(wildcard src/deps/adplug/**) src/deps/libbinio.$(LIB_EXTENSION)
	echo "=== Building libadplug ==="
	(cd src/deps/adplug/ && autoreconf --install && PKG_CONFIG_PATH="../libbinio" libbinio_CFLAGS="-I../libbinio/src" libbinio_LIBS="-L../libbinio/src/.libs -lbinio" ./configure --with-pic --enable-static && make -j4 -k || true)
	cp src/deps/adplug/src/.libs/libadplug.$(LIB_EXTENSION) src/deps/libadplug.$(LIB_EXTENSION)

# Include the VCV Rack plugin Makefile framework
include $(RACK_DIR)/plugin.mk
