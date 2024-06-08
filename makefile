
CC ?= gcc

SOURCES = qremip.c

override CFLAGS += -pedantic -Wall -Wextra -Werror -g3

EXEC ?= qremip

all: clean $(EXEC)

clean:
	$(RM) $(EXEC)

$(EXEC): $(SOURCES)
	$(CC) -o $(EXEC) $(SOURCES) $(CFLAGS) $(LDFLAGS)
