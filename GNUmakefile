# Default build uses cosmocc for cross-platform APE binaries.
# Delegates to Makefile.cosmo for configure + build.
#
# To use the upstream Makefile directly:
#   ./configure && make -f Makefile

.DEFAULT_GOAL := all

# test uses the system compiler via upstream Makefile
test:
	@$(MAKE) -f Makefile test

%:
	@$(MAKE) -f Makefile.cosmo $@
