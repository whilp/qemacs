#!/bin/sh
# Generate qe_modules.c from source files
# Usage: gen-modules.sh output.c src1.c src2.c ...
set -e

out="$1"; shift

cat > "$out" <<'EOF'
/* This file was generated automatically */
#include "qe.h"
#undef qe_module_init
#undef qe_module_init_mode
#define qe_module_init(fn)  extern int qe_module_##fn(QEmacsState *qs)
#define qe_module_init_mode(mode, flags)  extern int qe_module_##mode##__init(QEmacsState *qs)
EOF
grep -h ^qe_module_init "$@" >> "$out" || true
cat >> "$out" <<'EOF'
#undef qe_module_init
#undef qe_module_init_mode
void qe_init_all_modules(QEmacsState *qs) {
#define qe_module_init(fn)  qe_module_##fn(qs)
#define qe_module_init_mode(mode, flags)  qe_module_##mode##__init(qs)
EOF
grep -h ^qe_module_init "$@" >> "$out" || true
cat >> "$out" <<'EOF'
#undef qe_module_init
#undef qe_module_init_mode
}
#undef qe_module_exit
#define qe_module_exit(fn)  extern void qe_module_##fn(QEmacsState *qs)
EOF
grep -h ^qe_module_exit "$@" >> "$out" || true
cat >> "$out" <<'EOF'
#undef qe_module_exit
void qe_exit_all_modules(QEmacsState *qs) {
#define qe_module_exit(fn)  qe_module_##fn(qs)
EOF
grep -h ^qe_module_exit "$@" >> "$out" || true
cat >> "$out" <<'EOF'
#undef qe_module_exit
}
EOF
