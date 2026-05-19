CC = gcc
CFLAGS = -Wall -Wextra -O3 -std=c11 -fPIC -Iinclude
LDFLAGS = -shared -pthread

# Targets
TARGET_SO = libcortlet-upgradesched.so
TARGET_A  = libcortlet-upgradesched.a

# Directories
BUILD_DIR = build
TEST_DIR  = test
DEPS_DIR  = $(TEST_DIR)/deps
TEST_BIN  = $(TEST_DIR)/test_runner

# Source & Object mapping
SRC = src/upgradesched.c
OBJ = $(BUILD_DIR)/upgradesched.o

PREFIX ?= /usr/local
LIBDIR = $(PREFIX)/lib
INCLUDEDIR = $(PREFIX)/include

# Default target
all: $(TARGET_SO) $(TARGET_A)

# Compile shared library
$(TARGET_SO): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

# Compile static library
$(TARGET_A): $(OBJ)
	ar rcs $@ $^

# Rule for building object files into the build/ directory
$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Test target: Copies the .so to test/deps/ and compiles the test runner inside test/
test: all
	@mkdir -p $(DEPS_DIR)
	cp $(TARGET_SO) $(DEPS_DIR)/
	$(CC) -Wall -Wextra -O3 -std=c11 $(TEST_DIR)/test.c -L$(DEPS_DIR) -lcortlet-upgradesched -lpthread -o $(TEST_BIN)
	@echo "--------------------------------------------------------"
	@echo " Build Success! Run tests with:"
	@echo " LD_LIBRARY_PATH=$(DEPS_DIR) ./$(TEST_BIN)"
	@echo "--------------------------------------------------------"

# Clean up all generated structures
clean:
	rm -rf $(BUILD_DIR) $(DEPS_DIR)
	rm -f *.so *.a $(TEST_BIN)

# System-wide installation
install: all
	install -d $(DESTDIR)$(LIBDIR)
	install -d $(DESTDIR)$(INCLUDEDIR)
	install -m 755 $(TARGET_SO) $(DESTDIR)$(LIBDIR)/
	install -m 644 $(TARGET_A) $(DESTDIR)$(LIBDIR)/
	install -m 644 include/cortlet-upgradesched.h $(DESTDIR)$(INCLUDEDIR)/
	ldconfig

# System-wide removal
uninstall:
	rm -f $(DESTDIR)$(LIBDIR)/$(TARGET_SO)
	rm -f $(DESTDIR)$(LIBDIR)/$(TARGET_A)
	rm -f $(DESTDIR)$(INCLUDEDIR)/cortlet-upgradesched.h
	ldconfig

.PHONY: all clean test install uninstall
