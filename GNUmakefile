# Default build uses cosmocc for cross-platform APE binaries.
# Delegates to Makefile.cosmo for configure + build.
#
# To use the upstream Makefile directly:
#   ./configure && make -f Makefile

.DEFAULT_GOAL := all

%:
	@$(MAKE) -f Makefile.cosmo $@
