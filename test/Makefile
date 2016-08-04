SRCS = $(wildcard *.c)
PROGS = $(patsubst %.c,%,$(SRCS))

all: $(PROGS)

%: %.c
	cc -o $@ $<

.PHONY: clean

clean:
	rm $(PROGS)

