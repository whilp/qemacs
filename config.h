/*
 * QEmacs build configuration for cosmocc
 *
 * QE_VERSION is defined via -D flag in the Makefile from the VERSION file.
 * All other options are always enabled for the cosmocc build.
 */

#define CONFIG_QE_PREFIX "/usr/local"
#define CONFIG_QE_DATADIR "/usr/local/share"
#define CONFIG_QE_MANDIR "/usr/local/man"

#define CONFIG_HAS_TYPEOF 1
#define CONFIG_PTSNAME 1
#define CONFIG_NETWORK 1
#define CONFIG_SESSION_DETACH 1
#define CONFIG_ALL_KMAPS 1
#define CONFIG_MMAP 1
#define CONFIG_ALL_MODES 1
#define CONFIG_UNICODE_JOIN 1
#define CONFIG_HTML 1
