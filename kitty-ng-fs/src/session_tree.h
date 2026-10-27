#ifndef KG_SESSION_TREE_H
#define KG_SESSION_TREE_H
#include <stddef.h>
#include <wchar.h>
#include "profiles.h"

#define KG_TREE_NONE ((size_t)-1)
typedef enum KgTreeKind {
    KG_TREE_GROUP = 1,
    KG_TREE_SESSION = 2
} KgTreeKind;

typedef struct KgTreeNode {
    KgTreeKind kind;
    size_t parent;
    size_t profile_index; /* KG_TREE_NONE for groups. */
    wchar_t *name;        /* One safe visible component; root has an empty name. */
    wchar_t *path;        /* Full path, including leading backslash. */
    size_t *children;     /* Groups first, then sessions, ordinal name order. */
    size_t child_count;
} KgTreeNode;

typedef struct KgTree {
    KgTreeNode *nodes;    /* Node zero is the virtual root group. */
    size_t count;
    size_t *profile_nodes;
    size_t profile_count;
} KgTree;

typedef struct KgTreeMatch {
    KgTreeKind kind;
    size_t node_index;
    size_t profile_index;
    const wchar_t *relative; /* Points inside the supplied path; never allocated. */
    int legacy_alias;
} KgTreeMatch;

/* Output must be zero-initialized/freed. No profiles are copied or modified;
 * indexes remain valid while the original KgProfiles array is unchanged.
 * On failure the output is empty and a bounded diagnostic is returned. */
int kg_tree_build(const KgProfiles *profiles, KgTree *out,
                  wchar_t *error, size_t error_cap);
void kg_tree_free(KgTree *tree);

/* Group matches have an empty relative remainder. Once a session is reached,
 * the unmodified remaining path belongs to the remote filesystem. Old flat
 * labels are accepted only if the first component is absent from the new tree;
 * the new hierarchy always takes precedence. Lookup is ordinal/case-sensitive
 * to avoid changing a server identity because of case folding. */
int kg_tree_resolve(const KgTree *tree, const KgProfiles *profiles,
                    const wchar_t *path, int allow_legacy, KgTreeMatch *match);

#endif
