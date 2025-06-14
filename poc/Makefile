CC ?= gcc
CFLAGS ?= -Wall

TARGET = prog-v1 prog-v2
SRCS   = foo-v1.c foo-v2.c bar-v1.c bar-v2.c
OBJS   = $(SRCS:.c=.o)

.PHONY: run

run: clean prog-v1 prog-v2


prog-v1:
	@echo
	@echo
	@echo "### Compiling version 1 ###"
	@echo "Place G_GNUC_PRINTF in 'foo.c' only"
	@echo "No warning will appear, sliently cause security flaw"
	@echo
	$(CC) $(CFLAGS) -c foo-v1.c -o foo-v1.o
	$(CC) $(CFLAGS) -c bar-v1.c -o bar-v1.o
	@echo
	$(CC) foo-v1.o bar-v1.o -o $@

prog-v2:
	@echo
	@echo
	@echo "### Compiling version 2###"
	@echo "Place G_GNUC_PRINTF in 'foo.h' instead"
	@echo "Show warning for missing arguments"
	@echo
	$(CC) $(CFLAGS) -c foo-v2.c -o foo-v2.o
	$(CC) $(CFLAGS) -c bar-v2.c -o bar-v2.o
	@echo
	$(CC) foo-v2.o bar-v2.o -o $@

clean:
	@echo "### Clean all artifacts ###"
	rm -f $(TARGET) $(OBJS)
