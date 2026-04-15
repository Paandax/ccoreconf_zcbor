# Makefile for ccoreconf with zcbor

# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -pedantic -std=c11 -Werror -Werror=strict-prototypes -Werror=format -Werror=old-style-definition -Werror=cast-align

# Library name
LIB_NAME = ccoreconf

# Source directory
SRC_DIR = src
HEADER_DIR = include
ZCBOR_GEN_DIR = coreconf_zcbor_generated

# Object directory
OBJ_DIR = obj

# Include directories (include zcbor generated headers)
INCLUDE_DIRS = -I$(HEADER_DIR) -I$(ZCBOR_GEN_DIR)

# Source files (including zcbor generated files)
SRC_FILES = $(wildcard $(SRC_DIR)/*.c)
ZCBOR_SRC_FILES = $(ZCBOR_GEN_DIR)/zcbor_common.c \
                  $(ZCBOR_GEN_DIR)/zcbor_encode.c \
                  $(ZCBOR_GEN_DIR)/zcbor_decode.c

# Object files
OBJ_FILES = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRC_FILES))
ZCBOR_OBJ_FILES = $(patsubst $(ZCBOR_GEN_DIR)/%.c,$(OBJ_DIR)/%.o,$(ZCBOR_SRC_FILES))

# Header files
HEADER_FILES = $(wildcard $(HEADER_DIR)/*.h) $(wildcard $(ZCBOR_GEN_DIR)/*.h)

# Build rule for the library
$(LIB_NAME).a: $(OBJ_FILES) $(ZCBOR_OBJ_FILES)
	ar rcs $@ $^

# Examples directory
EXAMPLES_DIR = examples

# Build rule for examples
examples: $(LIB_NAME).a
	@echo "Building examples..."
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) $(EXAMPLES_DIR)/test_zcbor_migration.c $(LIB_NAME).a -o $(EXAMPLES_DIR)/test_migration
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) $(EXAMPLES_DIR)/test_fetch_simple.c $(LIB_NAME).a -o $(EXAMPLES_DIR)/test_fetch_simple
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) $(EXAMPLES_DIR)/test_exhaustive.c $(LIB_NAME).a -o $(EXAMPLES_DIR)/test_exhaustive
	@echo "✓ Examples built in $(EXAMPLES_DIR)/"

# Legacy example target (kept for compatibility)
EXEC_NAME = example
EXEC_OUTPUT = examples
# Build rule for exec
$(EXEC_NAME): $(OBJ_FILES) $(ZCBOR_OBJ_FILES) examples/demo_functionalities_coreconf.c
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) -o $@ $^

# Build rule for object files from src/
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(HEADER_FILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) -c $< -o $@

# Build rule for object files from zcbor generated/
$(OBJ_DIR)/%.o: $(ZCBOR_GEN_DIR)/%.c $(HEADER_FILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) -c $< -o $@


# Clean rule
clean:
	rm -rf $(LIB_NAME).a $(OBJ_DIR) $(EXEC_NAME)
	rm -f $(EXAMPLES_DIR)/test_migration $(EXAMPLES_DIR)/test_fetch_simple $(EXAMPLES_DIR)/test_exhaustive

# Regenerate include/sids.h from sid/*.sid files
sids:
	python3 tools/generate_sids_header.py

# Phony target to prevent conflicts with files of the same name
.PHONY: clean examples sids

