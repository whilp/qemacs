CC = cosmocc
CONFIG_SESSION_DETACH ?= yes

-include ../config.mak

TEST_CFLAGS = -g -O2 -mcosmo -Wall -I.. -DHAVE_QE_CONFIG_H

# Auto-discover all test_*.c files and derive binary names
TEST_SRCS := $(wildcard test_*.c)
TESTS := $(TEST_SRCS:.c=)

# Conditional tests: exclude if feature is not enabled
ifndef CONFIG_SESSION_DETACH
TESTS := $(filter-out test_session,$(TESTS))
endif
ifndef CONFIG_HTML
TESTS := $(filter-out test_html,$(TESTS))
endif

all: test

test: $(TESTS)
	@for t in $(TESTS); do \
		echo "--- Running $$t ---"; \
		./$$t || exit 1; \
	done
	@echo ""
	@echo "All test suites passed."

#
# Default rule: most tests are single-file, standalone
#
test_%: test_%.c testlib.h
	$(CC) $(TEST_CFLAGS) -o $@ $<

#
# Tests with extra source dependencies override the default rule
#
test_cutils: test_cutils.c ../cutils.c testlib.h ../config.h ../cutils.h
	$(CC) $(TEST_CFLAGS) -o $@ test_cutils.c ../cutils.c

BUFFER_SRCS = ../buffer.c ../cutils.c ../charset.c ../libunicode.c ../util.c \
              ../unix.c ../charsetmore.c ../unicode_join.c ../indic.c ../arabic.c

test_buffer: test_buffer.c $(BUFFER_SRCS) testlib.h ../qe.h ../cutils.h ../config.h
	$(CC) $(TEST_CFLAGS) -Wno-format-truncation -o $@ test_buffer.c $(BUFFER_SRCS)

test_session: test_session.c ../session.c ../session.h testlib.h ../config.h
	$(CC) $(TEST_CFLAGS) -o $@ test_session.c

# Embed test: validates resource files are accessible via /zip/ paths
EMBED_FILES = ../kmaps ../ligatures ../config.eg ../qe-manual.md ../qe.1

test_embed: test_embed.c testlib.h $(EMBED_FILES)
	$(CC) $(TEST_CFLAGS) -o $@ test_embed.c
	mv $@ $@.com
	mkdir -p .embed/share/qe
	for f in $(EMBED_FILES); do cp "$$f" .embed/share/qe/$$(basename "$$f"); done
	cd .embed && zip -qr0 ../$@.com share/qe/
	mv $@.com $@
	rm -rf .embed

#
# Benchmarks (not part of test suite)
#
BENCH_SRCS = ../buffer.c ../cutils.c ../charset.c ../libunicode.c ../util.c \
             ../unix.c ../charsetmore.c ../unicode_join.c ../indic.c ../arabic.c

bench_buffer: bench_buffer.c $(BENCH_SRCS) ../qe.h ../cutils.h ../config.h
	$(CC) $(TEST_CFLAGS) -Wno-format-truncation -o $@ bench_buffer.c $(BENCH_SRCS)

bench: bench_buffer
	@echo "--- Running bench_buffer ---"
	./bench_buffer

clean:
	rm -rf $(TESTS) bench_buffer .embed

.PHONY: all test bench clean
