include ../config.mak

TEST_CFLAGS = -g -O2 -Wall -I.. -DHAVE_QE_CONFIG_H

TESTS = test_cutils

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

test_session: test_session.c ../session.c ../session.h testlib.h ../config.h
	$(CC) $(TEST_CFLAGS) -o $@ test_session.c $(SESSION_DETACH_LIBS)

# Terminal integration tests (requires qe binary with test_display driver)
test_terminal: test_terminal.c testlib.h
	$(CC) $(TEST_CFLAGS) -o $@ test_terminal.c

# Run terminal tests separately (they need qe built first)
test-terminal: test_terminal
	@echo "--- Running test_terminal ---"
	./test_terminal

clean:
	rm -f $(TESTS) test_terminal

.PHONY: all test clean
