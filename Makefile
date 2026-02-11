
TARGET = procx

SOURCE = procx.c

CC = gcc

CFLAGS = -Wall -std=c99

LDFLAGS = -pthread -lrt

all: $(TARGET)

$(TARGET): $(SOURCE)
	@echo "Derleniyor... $(SOURCE)"
	$(CC) $(SOURCE) $(CFLAGS) $(LDFLAGS) -o $@

clean:
	@echo "Temizlik yapiliyor..."
	rm -f $(TARGET)
	
	rm -f shmfile

.PHONY: all clean