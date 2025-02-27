# Compiler
CC = mpicc

# Flags
CFLAGS = -Wall -pthread

# Output binary
TARGET = tema2

# Source files
SRC = tema2.c

# Header files
HEADERS = tema2.h

# Build rule
build: $(TARGET)

$(TARGET): $(SRC) $(HEADERS)
	$(CC) -o $(TARGET) $(SRC) $(CFLAGS)

# Clean rule
clean:
	rm -rf $(TARGET)
