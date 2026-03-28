CC = gcc
CFLAGS = -Wall -Wextra -O3 -fPIC -Iinclude
LDFLAGS = -shared -lz -lpthread

SRC = src/kv_store.c
OBJ = $(SRC:.c=.o)
LIB = libkvdb.so

all: $(LIB)

$(LIB): $(OBJ)
	$(CC) -shared -o $@ $^ -lz -lpthread

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(LIB)

test: all
	$(CC) $(CFLAGS) tests/test_kv.c -o tests/test_kv -L. -lkvdb -Wl,-rpath,.
	./tests/test_kv

.PHONY: all clean test
