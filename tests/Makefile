CC = cosmocc

-include ../config.mak

TEST_CFLAGS = -g -O2 -mcosmo -Wall -I.. \
              -DCONFIG_SESSION_DETACH -DCONFIG_HTML

# Output directory for test binaries (inside o/, already gitignored)
OUTDIR := ../o/tests

# Auto-discover all test_*.c files and derive binary names
TEST_SRCS := $(wildcard test_*.c)
TESTS := $(addprefix $(OUTDIR)/, $(TEST_SRCS:.c=))

all: test

QE_BINARY ?= ../o/qe

# Build all test binaries (parallelizable with make -j)
build: $(TESTS)

# Run tests sequentially (must be serial for correct output)
test: build
	@for t in $(TESTS); do \
		echo "--- Running $$(basename $$t) ---"; \
		QE_BINARY=$(QE_BINARY) ./$$t || exit 1; \
	done
	@echo ""
	@echo "All test suites passed."

# Ensure output directory exists
$(OUTDIR):
	mkdir -p $(OUTDIR)

#
# Default rule: most tests are single-file, standalone
#
$(OUTDIR)/test_%: test_%.c testlib.h | $(OUTDIR)
	$(CC) $(TEST_CFLAGS) -o $@ $<

#
# Tests with extra source dependencies override the default rule
#
$(OUTDIR)/test_cutils: test_cutils.c ../cutils.c testlib.h ../cutils.h | $(OUTDIR)
	$(CC) $(TEST_CFLAGS) -o $@ test_cutils.c ../cutils.c

BUFFER_SRCS = ../buffer.c ../cutils.c ../charset.c ../libunicode.c ../util.c \
              ../unix.c ../charsetmore.c ../unicode_join.c ../indic.c ../arabic.c

$(OUTDIR)/test_buffer: test_buffer.c $(BUFFER_SRCS) testlib.h ../qe.h ../cutils.h | $(OUTDIR)
	$(CC) $(TEST_CFLAGS) -Wno-format-truncation -o $@ test_buffer.c $(BUFFER_SRCS)

$(OUTDIR)/test_session: test_session.c ../session.c ../session.h testlib.h | $(OUTDIR)
	$(CC) $(TEST_CFLAGS) -o $@ test_session.c

$(OUTDIR)/test_mkd_render: test_mkd_render.c ../modes/mkd_render_parse.c ../modes/mkd_render.h testlib.h | $(OUTDIR)
	$(CC) $(TEST_CFLAGS) -o $@ test_mkd_render.c ../modes/mkd_render_parse.c

# Embed test: validates resource files are accessible via /zip/ paths
EMBED_FILES = ../o/kmaps ../o/ligatures ../config.eg ../o/qe-manual.md ../qe.1

$(OUTDIR)/test_embed: test_embed.c testlib.h $(EMBED_FILES) | $(OUTDIR)
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

$(OUTDIR)/bench_buffer: bench_buffer.c $(BENCH_SRCS) ../qe.h ../cutils.h | $(OUTDIR)
	$(CC) $(TEST_CFLAGS) -Wno-format-truncation -o $@ bench_buffer.c $(BENCH_SRCS)

bench: $(OUTDIR)/bench_buffer
	@echo "--- Running bench_buffer ---"
	./$(OUTDIR)/bench_buffer

clean:
	rm -rf $(OUTDIR) .embed

.PHONY: all build test bench clean
