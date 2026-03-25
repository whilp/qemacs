CC ?= cc
CFLAGS ?= -g -O2 -Wall
CFLAGS += -I..

TESTS = test_cutils

all: test

test: $(TESTS)
	@for t in $(TESTS); do \
		echo "--- Running $$t ---"; \
		./$$t || exit 1; \
	done
	@echo ""
	@echo "All test suites passed."

test_cutils: test_cutils.c ../cutils.c testlib.h ../config.h ../cutils.h
	$(CC) $(CFLAGS) -o $@ test_cutils.c ../cutils.c

clean:
	rm -f $(TESTS)

.PHONY: all test clean
