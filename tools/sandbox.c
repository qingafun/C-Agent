/*
 * This module implements the "Sandboxed Filesystem" layer, ensuring that the 
 * LLM-driven agent cannot access, modify, or delete files outside the 
 * designated workspace directory (g_config.workdir).
 */
#include "tools/sandbox.h"

#include "compat.h"
#include "config.h"
#include "util.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool is_path_sep(char c) {
    return c == '/' || c == '\\';
}

static bool within_workspace(const char *resolved) {
    const char *workdir = g_config.workdir;
    size_t n = strlen(workdir);
    return strncmp(resolved, workdir, n) == 0 &&
           (is_path_sep(resolved[n]) || resolved[n] == '\0');
}

char *resolve_workspace_path(const char *rel_path) {
    if (!rel_path || !rel_path[0])
        return NULL;
    /* Reject absolute paths (Unix: /, Windows: \ or X:) */
    if (rel_path[0] == '/' || rel_path[0] == '\\')
        return NULL;
    if (rel_path[0] && rel_path[1] == ':')
        return NULL;

    char full[PATH_MAX];
    snprintf(full, sizeof(full), "%s/%s", g_config.workdir, rel_path);

    char resolved[PATH_MAX];
    if (fs_realpath(full, resolved))
        return within_workspace(resolved) ? xstrdup(resolved) : NULL;

    /* Path does not exist yet — resolve the parent and graft the leaf back on. */
    char parent[PATH_MAX];
    snprintf(parent, sizeof(parent), "%s", full);
    char *slash = strrchr(parent, '/');
    if (!slash || slash == parent)
        return NULL;
    *slash = '\0';
    const char *leaf = slash + 1;

    char resolved_parent[PATH_MAX];
    if (!fs_realpath(parent, resolved_parent))
        return NULL;
    if (!within_workspace(resolved_parent))
        return NULL;

    return xasprintf("%s/%s", resolved_parent, leaf);
}
