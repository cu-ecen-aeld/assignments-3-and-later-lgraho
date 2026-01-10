CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall -Werror
TARGET ?= aesdsocket
SOURCE ?= aesdsocket.c
LDFLAGS ?= -lpthread -lrt

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $^ -o $@ $(INCLUDES) $(LDFLAGS)

clean:
	rm -f $(TARGET) *.o