# Makefile for Dezipper
# A tool to extract ZIP files and free disk space

CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lz

# Windows-specific
ifeq ($(OS),Windows_NT)
    LDFLAGS += -lkernel32
endif

TARGET = dezipper
SRC = dezipper.c

.PHONY: all clean test help

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET) *.exe *.o

test: $(TARGET)
	@echo "Running tests..."
	@echo "Test 1: Help option"
	./$(TARGET) --help > /dev/null && echo "  PASS: Help" || echo "  FAIL: Help"
	@echo "Test 2: List option"
	./$(TARGET) -l test_clean.zip > /dev/null 2>&1 && echo "  PASS: List" || echo "  FAIL: List"
	@echo "Test 3: Extract with preserve timestamp"
	./$(TARGET) -p -f time_test.zip > /dev/null 2>&1 && echo "  PASS: Extract" || echo "  FAIL: Extract"

help:
	@echo "Dezipper Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all     - Build dezipper (default)"
	@echo "  clean   - Remove built files"
	@echo "  test    - Run basic tests"
	@echo "  help    - Show this help message"
	@echo ""
	@echo "Usage:"
	@echo "  make              - Build dezipper"
	@echo "  make clean        - Clean build artifacts"
	@echo "  make test         - Run tests"
	@echo "  ./dezipper -h     - Show help"