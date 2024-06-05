
CC ?= gcc

SOURCES = wadremip.c

override CFLAGS += -pedantic -Wall -Wextra -Werror -g3

EXEC ?= wadremip

all: clean $(EXEC)

clean:
	$(RM) $(EXEC)

$(EXEC): $(SOURCES)
	$(CC) -o $(EXEC) $(SOURCES) $(CFLAGS) $(LDFLAGS)
