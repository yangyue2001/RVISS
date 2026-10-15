CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror
CPPFLAGS += -Iinclude

PROGRAM := board_model
SRCS := src/main.c src/address_space.c src/board_config.c src/board_runtime.c
OBJS := $(SRCS:.c=.o)

.PHONY: all test clean

all: $(PROGRAM)

$(PROGRAM): $(OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(OBJS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -MP -c -o $@ $<

-include $(OBJS:.o=.d)

test: $(PROGRAM)
	./$(PROGRAM) configs/virt.board

clean:
	rm -f $(PROGRAM) $(OBJS) $(OBJS:.o=.d)
