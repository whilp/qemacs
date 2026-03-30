/*
 * Dynamic plugin loading for QEmacs
 *
 * Loads native shared libraries (.so/.dylib/.dll) from ~/.qe/ at startup
 * and via the load-plugin command. Plugins are built with -DQE_MODULE and
 * export __qe_module_init / __qe_module_exit symbols.
 *
 * Uses cosmo_dlopen() for cross-platform support with Cosmopolitan libc.
 */

#include "qe.h"

#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>

#define MAX_PLUGINS 64

typedef int (*plugin_init_func)(QEmacsState *qs);
typedef void (*plugin_exit_func)(QEmacsState *qs);

typedef struct QEPlugin {
    void *handle;
    plugin_init_func init;
    plugin_exit_func exit;
    char path[1024];
} QEPlugin;

static QEPlugin plugins[MAX_PLUGINS];
static int nb_plugins;

static int is_shared_lib(const char *name)
{
    const char *ext;

    ext = strrchr(name, '.');
    if (!ext)
        return 0;
    return !strcmp(ext, ".so") || !strcmp(ext, ".dylib") || !strcmp(ext, ".dll");
}

static int qe_load_plugin(QEmacsState *qs, const char *path)
{
    void *handle;
    plugin_init_func init_func;
    plugin_exit_func exit_func;
    int i;

    /* check if already loaded */
    for (i = 0; i < nb_plugins; i++) {
        if (!strcmp(plugins[i].path, path))
            return -1;  /* already loaded */
    }

    if (nb_plugins >= MAX_PLUGINS)
        return -2;  /* too many plugins */

    handle = cosmo_dlopen(path, RTLD_NOW);
    if (!handle) {
        return -3;  /* dlopen failed */
    }

    init_func = (plugin_init_func)cosmo_dlsym(handle, "__qe_module_init");
    if (!init_func) {
        cosmo_dlclose(handle);
        return -4;  /* no init function */
    }

    exit_func = (plugin_exit_func)cosmo_dlsym(handle, "__qe_module_exit");

    /* call init */
    if (init_func(qs) != 0) {
        cosmo_dlclose(handle);
        return -5;  /* init failed */
    }

    /* record plugin */
    plugins[nb_plugins].handle = handle;
    plugins[nb_plugins].init = init_func;
    plugins[nb_plugins].exit = exit_func;
    pstrcpy(plugins[nb_plugins].path, sizeof(plugins[nb_plugins].path), path);
    nb_plugins++;

    return 0;
}

/* Load all plugins from a directory */
static void qe_load_plugins_from_dir(QEmacsState *qs, const char *dir)
{
    DIR *d;
    struct dirent *de;
    char path[1024];

    d = opendir(dir);
    if (!d)
        return;

    while ((de = readdir(d)) != NULL) {
        if (!is_shared_lib(de->d_name))
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        qe_load_plugin(qs, path);
    }
    closedir(d);
}

/* Load plugins from ~/.qe/ at startup */
void qe_load_all_plugins(QEmacsState *qs)
{
    const char *home;
    char dir[1024];

    home = getenv("HOME");
    if (!home)
        return;

    snprintf(dir, sizeof(dir), "%s/.qe", home);
    qe_load_plugins_from_dir(qs, dir);
}

/* Unload all plugins at exit */
void qe_exit_all_plugins(QEmacsState *qs)
{
    int i;

    for (i = nb_plugins - 1; i >= 0; i--) {
        if (plugins[i].exit)
            plugins[i].exit(qs);
        cosmo_dlclose(plugins[i].handle);
    }
    nb_plugins = 0;
}

/* Interactive command: load-plugin */
static void do_load_plugin(EditState *s, const char *filename)
{
    QEmacsState *qs = s->qs;
    int ret;

    ret = qe_load_plugin(qs, filename);
    switch (ret) {
    case 0:
        put_status(s, "Plugin loaded: %s", filename);
        break;
    case -1:
        put_status(s, "Plugin already loaded: %s", filename);
        break;
    case -3:
        put_status(s, "Cannot open plugin: %s: %s", filename,
                   cosmo_dlerror());
        break;
    case -4:
        put_status(s, "Not a valid plugin (no __qe_module_init): %s",
                   filename);
        break;
    case -5:
        put_status(s, "Plugin init failed: %s", filename);
        break;
    default:
        put_status(s, "Cannot load plugin: %s", filename);
        break;
    }
}

static const CmdDef plugin_commands[] = {
    CMD3( "load-plugin", "",
          "Load a dynamic plugin from a shared library file",
          do_load_plugin, ESs,
          "s{Plugin file: }[file]|file|", 0)
};

static int plugin_init(QEmacsState *qs)
{
    qe_register_commands(qs, NULL, plugin_commands, countof(plugin_commands));
    return 0;
}

qe_module_init(plugin_init);
