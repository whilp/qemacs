CC = cosmocc
CONFIG_SESSION_DETACH ?= yes

-include ../config.mak

TEST_CFLAGS = -g -O2 -mcosmo -Wall -I.. -DHAVE_QE_CONFIG_H

TESTS = test_cutils test_buffer test_unix test_cosmo

ifdef CONFIG_SESSION_DETACH
TESTS += test_session
endif

all: test

test: $(TESTS)
	@for t in $(TESTS); do \
		echo "--- Running $$t ---"; \
		./$$t || exit 1; \
	done
	@echo ""
	@echo "All test suites passed."

test_cutils: test_cutils.c ../cutils.c testlib.h ../config.h ../cutils.h
	$(CC) $(TEST_CFLAGS) -o $@ test_cutils.c ../cutils.c

BUFFER_SRCS = ../buffer.c ../cutils.c ../charset.c ../libunicode.c ../util.c \
              ../unix.c ../charsetmore.c ../unicode_join.c ../indic.c ../arabic.c

test_buffer: test_buffer.c $(BUFFER_SRCS) testlib.h ../qe.h ../cutils.h ../config.h
	$(CC) $(TEST_CFLAGS) -Wno-format-truncation -o $@ test_buffer.c $(BUFFER_SRCS)

test_unix: test_unix.c testlib.h
	$(CC) $(TEST_CFLAGS) -o $@ test_unix.c

test_cosmo: test_cosmo.c testlib.h
	$(CC) $(TEST_CFLAGS) -o $@ test_cosmo.c

test_session: test_session.c ../session.c ../session.h testlib.h ../config.h
	$(CC) $(TEST_CFLAGS) -o $@ test_session.c

# Terminal integration tests (requires qe binary with test_display driver)
test_terminal: test_terminal.c testlib.h
	$(CC) $(TEST_CFLAGS) -o $@ test_terminal.c

# Run terminal tests separately (they need qe built first)
test-terminal: test_terminal
	@echo "--- Running test_terminal ---"
	./test_terminal

# HTML rendering tests (requires qe binary with test_display driver)
test_html: test_html.c testlib.h
	$(CC) $(TEST_CFLAGS) -o $@ test_html.c

# Run HTML rendering tests separately (they need qe built first)
test-html: test_html
	@echo "--- Running test_html ---"
	./test_html

BENCH_SRCS = ../buffer.c ../cutils.c ../charset.c ../libunicode.c ../util.c \
             ../unix.c ../charsetmore.c ../unicode_join.c ../indic.c ../arabic.c

bench_buffer: bench_buffer.c $(BENCH_SRCS) ../qe.h ../cutils.h ../config.h
	$(CC) $(TEST_CFLAGS) -Wno-format-truncation -o $@ bench_buffer.c $(BENCH_SRCS)

bench: bench_buffer
	@echo "--- Running bench_buffer ---"
	./bench_buffer

clean:
	rm -f $(TESTS) test_terminal test_html bench_buffer

.PHONY: all test bench clean
