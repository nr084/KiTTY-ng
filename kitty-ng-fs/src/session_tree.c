/* Hierarchical virtual profile navigation. This module never opens SSH,
 * reads credentials or changes the logical name passed to Klink -load. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "session_tree.h"

#define KG_TREE_MAX_PROFILES 10000u
#define KG_TREE_MAX_DEPTH 16u
#define KG_TREE_MAX_NODES (KG_TREE_MAX_PROFILES * (KG_TREE_MAX_DEPTH + 1u) + 1u)
#define KG_TREE_MAX_PATH 1023u
#define KG_TREE_MAX_NAME 255u

typedef struct ProfileOrder {
    const KgProfile *profile;
    size_t index;
} ProfileOrder;

typedef struct ChildOrder {
    KgTreeNode *node;
    size_t index;
    const wchar_t *base;
    const wchar_t *load_name;
    wchar_t *replacement;
} ChildOrder;

typedef struct Builder {
    KgTree *tree;
    size_t capacity;
    size_t *child_capacity;
    wchar_t *error;
    size_t error_cap;
} Builder;

static void error_text(wchar_t *out, size_t cap, const wchar_t *text)
{
    size_t n;
    if (!out || !cap) return;
    n = wcslen(text);
    if (n >= cap) n = cap - 1;
    memcpy(out, text, n * sizeof(*out)); out[n] = 0;
}

static wchar_t *duplicate(const wchar_t *s)
{
    size_t n = wcslen(s);
    wchar_t *p = malloc((n + 1) * sizeof(*p));
    if (p) memcpy(p, s, (n + 1) * sizeof(*p));
    return p;
}

static int compare_i(const wchar_t *a, const wchar_t *b)
{
    int result = CompareStringOrdinal(a, -1, b, -1, TRUE);
    if (result == CSTR_LESS_THAN) return -1;
    if (result == CSTR_GREATER_THAN) return 1;
    return 0;
}

static int compare_names(const wchar_t *a, const wchar_t *b)
{
    int c = compare_i(a, b);
    return c ? c : wcscmp(a, b);
}

static int profile_order(const void *av, const void *bv)
{
    const ProfileOrder *a = av, *b = bv;
    return compare_names(a->profile->load_name, b->profile->load_name);
}

static int children_original(const void *av, const void *bv)
{
    const ChildOrder *a = av, *b = bv;
    int c = compare_i(a->base, b->base);
    if (c) return c;
    if (a->node->kind != b->node->kind) return a->node->kind < b->node->kind ? -1 : 1;
    c = wcscmp(a->base, b->base);
    if (c) return c;
    if (a->load_name && b->load_name) return wcscmp(a->load_name, b->load_name);
    return 0;
}

static int children_visible(const void *av, const void *bv)
{
    const ChildOrder *a = av, *b = bv;
    if (a->node->kind != b->node->kind) return a->node->kind < b->node->kind ? -1 : 1;
    return compare_names(a->node->name, b->node->name);
}

static int reserved_name(const wchar_t *s)
{
    size_t n = wcscspn(s, L".");
    if (n == 3 &&
        (CompareStringOrdinal(s, 3, L"CON", 3, TRUE) == CSTR_EQUAL ||
         CompareStringOrdinal(s, 3, L"PRN", 3, TRUE) == CSTR_EQUAL ||
         CompareStringOrdinal(s, 3, L"AUX", 3, TRUE) == CSTR_EQUAL ||
         CompareStringOrdinal(s, 3, L"NUL", 3, TRUE) == CSTR_EQUAL)) return 1;
    return n == 4 && s[3] >= L'1' && s[3] <= L'9' &&
        (CompareStringOrdinal(s, 3, L"COM", 3, TRUE) == CSTR_EQUAL ||
         CompareStringOrdinal(s, 3, L"LPT", 3, TRUE) == CSTR_EQUAL);
}

/* Per-component equivalent of the v0.1 profile label encoding. Literal '%'
 * is escaped, so no raw profile name can impersonate a different component.
 * The logical profile name itself is never decoded again during lookup. */
static wchar_t *component_label(const wchar_t *name, int session_dots)
{
    static const wchar_t hex[] = L"0123456789ABCDEF";
    wchar_t buffer[KG_TREE_MAX_NAME + 1u];
    size_t n = wcslen(name), tail = n, i, at = 0;
    int reserved = reserved_name(name) || compare_i(name, KG_SETTINGS_NAME) == 0;
    if (!n) return NULL;
    while (tail && (name[tail - 1] == L'.' || name[tail - 1] == L' ')) --tail;
    for (i = 0; i < n; ++i) {
        wchar_t c = name[i];
        int escape = (session_dots && c == 0x00b7) || c < 32 || (c >= 127 && c <= 159) ||
                     wcschr(L"%/\\:?*\"<>|", c) || i >= tail || (!i && reserved);
        if (session_dots && c == L'.') {
            if (at >= KG_TREE_MAX_NAME) return NULL;
            buffer[at++] = 0x00b7;
            continue;
        }
        if (escape) {
            unsigned char bytes[2];
            size_t count = 1, j;
            if (c < 128) bytes[0] = (unsigned char)c;
            else { bytes[0] = 0xc0u | (unsigned char)(c >> 6);
                   bytes[1] = 0x80u | (unsigned char)(c & 63); count = 2; }
            if (at + count * 3 > KG_TREE_MAX_NAME) return NULL;
            for (j = 0; j < count; ++j) {
                buffer[at++] = L'%'; buffer[at++] = hex[bytes[j] >> 4];
                buffer[at++] = hex[bytes[j] & 15];
            }
        } else {
            if (at >= KG_TREE_MAX_NAME) return NULL;
            buffer[at++] = c;
        }
    }
    buffer[at] = 0;
    return duplicate(buffer);
}

static int grow_nodes(Builder *b)
{
    size_t new_capacity = b->capacity ? b->capacity * 2 : 64;
    KgTreeNode *nodes;
    size_t *capacities;
    if (new_capacity > KG_TREE_MAX_NODES) new_capacity = KG_TREE_MAX_NODES;
    if (new_capacity <= b->capacity) return 0;
    nodes = realloc(b->tree->nodes, new_capacity * sizeof(*nodes));
    if (!nodes) return 0;
    b->tree->nodes = nodes;
    capacities = realloc(b->child_capacity, new_capacity * sizeof(*capacities));
    if (!capacities) return 0;
    b->child_capacity = capacities;
    memset(nodes + b->capacity, 0, (new_capacity - b->capacity) * sizeof(*nodes));
    memset(capacities + b->capacity, 0, (new_capacity - b->capacity) * sizeof(*capacities));
    b->capacity = new_capacity;
    return 1;
}

static size_t add_node(Builder *b, size_t parent, KgTreeKind kind,
                       wchar_t *name, size_t profile)
{
    KgTreeNode *node;
    size_t index = b->tree->count;
    if (index == b->capacity && !grow_nodes(b)) return KG_TREE_NONE;
    node = &b->tree->nodes[index];
    memset(node, 0, sizeof(*node));
    node->kind = kind; node->parent = parent; node->profile_index = profile;
    node->name = name;
    ++b->tree->count;
    if (parent != KG_TREE_NONE) {
        KgTreeNode *p = &b->tree->nodes[parent];
        if (p->child_count == b->child_capacity[parent]) {
            size_t n = b->child_capacity[parent] ? b->child_capacity[parent] * 2 : 8;
            size_t *children = realloc(p->children, n * sizeof(*children));
            if (!children) return KG_TREE_NONE; /* New node/name now owned by tree. */
            p->children = children; b->child_capacity[parent] = n;
        }
        p->children[p->child_count++] = index;
    }
    return index;
}

static int original_name_exists(const ChildOrder *order, size_t count, const wchar_t *name)
{
    size_t first = 0, last = count;
    while (first < last) {
        size_t middle = first + (last - first) / 2;
        int c = compare_i(order[middle].base, name);
        if (c < 0) first = middle + 1;
        else last = middle;
    }
    return first < count && compare_i(order[first].base, name) == 0;
}

/* Reserve all original names before adding collision suffixes. Thus a
 * naturally named "VPN [SSH]" does not get displaced by a conflicting VPN. */
static int arrange_children(Builder *b, const KgProfiles *profiles, size_t parent)
{
    KgTreeNode *p = &b->tree->nodes[parent];
    ChildOrder *order;
    size_t i, end, j;
    int ok = 0;
    if (!p->child_count) return 1;
    order = calloc(p->child_count, sizeof(*order));
    if (!order) return 0;
    for (i = 0; i < p->child_count; ++i) {
        order[i].index = p->children[i]; order[i].node = &b->tree->nodes[p->children[i]];
        order[i].base = order[i].node->name;
        if (order[i].node->kind == KG_TREE_SESSION)
            order[i].load_name = profiles->items[order[i].node->profile_index].load_name;
    }
    qsort(order, p->child_count, sizeof(*order), children_original);
    for (i = 0; i < p->child_count; i = end) {
        unsigned number = 1;
        end = i + 1;
        while (end < p->child_count && compare_i(order[i].base, order[end].base) == 0) ++end;
        if (end == i + 1) continue;
        for (j = i; j < end; ++j) {
            wchar_t candidate[KG_TREE_MAX_NAME + 1u], suffix[32];
            size_t base_len = wcslen(order[j].base), suffix_len;
            if (order[j].node->kind == KG_TREE_GROUP) {
                /* Group duplicates should have coalesced while constructing
                 * sorted logical prefixes. Refuse an ambiguous tree. */
                if (j != i) {
                    error_text(b->error, b->error_cap, L"Conflicting KiTTY-ng group names cannot be represented safely.");
                    goto done;
                }
                continue;
            }
            for (;;) {
                if (number == 1) wcscpy(suffix, L" [SSH]");
                else swprintf(suffix, 32, L" [SSH %u]", number);
                ++number;
                suffix_len = wcslen(suffix);
                if (base_len + suffix_len > KG_TREE_MAX_NAME) {
                    error_text(b->error, b->error_cap, L"A conflicting KiTTY-ng session name exceeds 255 characters after the SSH suffix. Rename that session.");
                    goto done;
                }
                memcpy(candidate, order[j].base, base_len * sizeof(*candidate));
                memcpy(candidate + base_len, suffix, (suffix_len + 1) * sizeof(*candidate));
                if (!original_name_exists(order, p->child_count, candidate)) break;
            }
            order[j].replacement = duplicate(candidate);
            if (!order[j].replacement) goto done;
        }
    }
    for (i = 0; i < p->child_count; ++i) {
        if (order[i].replacement) {
            free(order[i].node->name); order[i].node->name = order[i].replacement;
            order[i].replacement = NULL;
        }
    }
    qsort(order, p->child_count, sizeof(*order), children_visible);
    for (i = 0; i < p->child_count; ++i) p->children[i] = order[i].index;
    ok = 1;
done:
    for (i = 0; i < p->child_count; ++i) free(order[i].replacement);
    free(order); return ok;
}

static int build_paths(Builder *b)
{
    size_t i;
    b->tree->nodes[0].path = duplicate(L"\\");
    if (!b->tree->nodes[0].path) return 0;
    /* Parents are allocated before children, including renamed collisions. */
    for (i = 1; i < b->tree->count; ++i) {
        KgTreeNode *n = &b->tree->nodes[i], *p = &b->tree->nodes[n->parent];
        size_t prefix = wcslen(p->path), leaf = wcslen(n->name), separator = n->parent ? 1 : 0;
        if (prefix + separator + leaf > KG_TREE_MAX_PATH) {
            error_text(b->error, b->error_cap, L"A KiTTY-ng virtual path exceeds the Total Commander limit of 1023 characters."); return 0;
        }
        n->path = malloc((prefix + separator + leaf + 1) * sizeof(*n->path));
        if (!n->path) return 0;
        memcpy(n->path, p->path, prefix * sizeof(*n->path));
        if (separator) n->path[prefix++] = L'\\';
        memcpy(n->path + prefix, n->name, (leaf + 1) * sizeof(*n->path));
    }
    return 1;
}

void kg_tree_free(KgTree *tree)
{
    size_t i;
    if (!tree) return;
    for (i = 0; i < tree->count; ++i) {
        free(tree->nodes[i].name); free(tree->nodes[i].path); free(tree->nodes[i].children);
    }
    if (tree->legacy) { kg_tree_free(tree->legacy); free(tree->legacy); }
    free(tree->nodes); free(tree->profile_nodes); memset(tree, 0, sizeof(*tree));
}

static int build_tree(const KgProfiles *profiles, KgTree *out, wchar_t *error, size_t error_cap, int session_dots)
{
    Builder b;
    ProfileOrder *order = NULL;
    size_t stack[KG_TREE_MAX_DEPTH], i, j;
    wchar_t *current = NULL, *label = NULL;
    int ok = 0;
    if (error && error_cap) error[0] = 0;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    memset(&b, 0, sizeof(b)); b.tree = out; b.error = error; b.error_cap = error_cap;
    if (!profiles || (profiles->count && !profiles->items) || profiles->count > KG_TREE_MAX_PROFILES) {
        error_text(error, error_cap, L"Invalid or excessive KiTTY-ng profile count."); return 0;
    }
    for (i = 0; i < KG_TREE_MAX_DEPTH; ++i) stack[i] = KG_TREE_NONE;
    out->profile_count = profiles->count;
    if (profiles->count) {
        order = malloc(profiles->count * sizeof(*order));
        out->profile_nodes = malloc(profiles->count * sizeof(*out->profile_nodes));
        if (!order || !out->profile_nodes) goto done;
        for (i = 0; i < profiles->count; ++i) {
            if (!profiles->items[i].load_name || !profiles->items[i].load_name[0]) {
                error_text(error, error_cap, L"A KiTTY-ng profile has an empty logical name."); goto done;
            }
            order[i].index = i; order[i].profile = &profiles->items[i];
            out->profile_nodes[i] = KG_TREE_NONE;
        }
        qsort(order, profiles->count, sizeof(*order), profile_order);
        for (i = 1; i < profiles->count; ++i) if (!wcscmp(order[i-1].profile->load_name, order[i].profile->load_name)) {
            error_text(error, error_cap, L"Duplicate logical KiTTY-ng sessions would select the same server. Refresh the profiles."); goto done;
        }
    }
    label = duplicate(L"");
    if (!label) goto done;
    if (add_node(&b, KG_TREE_NONE, KG_TREE_GROUP, label, KG_TREE_NONE) == KG_TREE_NONE) {
        if (!out->count) free(label); label = NULL; goto done;
    }
    label = NULL;
    for (i = 0; i < profiles->count; ++i) {
        wchar_t *parts[KG_TREE_MAX_DEPTH + 1u], *start, *slash;
        size_t count = 0, parent = 0;
        current = duplicate(order[i].profile->load_name);
        if (!current) goto done;
        start = current;
        for (;;) {
            if (count >= KG_TREE_MAX_DEPTH + 1u || !*start) {
                error_text(error, error_cap, L"A KiTTY-ng session has an empty component or more than 16 nested groups."); goto done;
            }
            parts[count++] = start;
            slash = wcschr(start, L'/');
            if (!slash) break;
            *slash = 0;
            if (!*start) { error_text(error, error_cap, L"A KiTTY-ng session has an empty group name."); goto done; }
            start = slash + 1;
        }
        for (j = 0; j < count; ++j) {
            size_t index;
            int group = j + 1 < count;
            label = component_label(parts[j], session_dots && !group);
            if (!label) {
                error_text(error, error_cap, L"A KiTTY-ng profile component cannot fit a safe 255-character panel name."); goto done;
            }
            if (group && stack[j] != KG_TREE_NONE && out->nodes[stack[j]].parent == parent &&
                compare_i(out->nodes[stack[j]].name, label) == 0) {
                parent = stack[j]; free(label); label = NULL; continue;
            }
            {
                size_t previous_count = out->count;
                index = add_node(&b, parent, group ? KG_TREE_GROUP : KG_TREE_SESSION, label,
                                 group ? KG_TREE_NONE : order[i].index);
                if (index == KG_TREE_NONE) {
                    if (out->count != previous_count) label = NULL; /* owned */
                    goto done;
                }
            }
            label = NULL;
            if (group) { stack[j] = index; parent = index; }
            else out->profile_nodes[order[i].index] = index;
        }
        free(current); current = NULL;
    }
    for (i = 0; i < out->count; ++i)
        if (out->nodes[i].kind == KG_TREE_GROUP && !arrange_children(&b, profiles, i)) goto done;
    if (!build_paths(&b)) goto done;
    ok = 1;
done:
    free(label); free(current); free(order); free(b.child_capacity);
    if (!ok) {
        if (error && error_cap && !error[0]) error_text(error, error_cap, L"Not enough memory to build the KiTTY-ng session tree.");
        kg_tree_free(out);
    }
    return ok;
}

int kg_tree_build(const KgProfiles *profiles, KgTree *out, wchar_t *error, size_t error_cap)
{
    size_t i;
    int changed = 0;
    if (!build_tree(profiles, out, error, error_cap, 1)) return 0;
    for (i = 0; i < profiles->count; ++i) {
        const wchar_t *leaf = wcsrchr(profiles->items[i].load_name, L'/');
        leaf = leaf ? leaf + 1 : profiles->items[i].load_name;
        if (wcschr(leaf, L'.') || wcschr(leaf, 0x00b7)) { changed = 1; break; }
    }
    if (!changed) return 1;
    out->legacy = calloc(1, sizeof(*out->legacy));
    if (!out->legacy || !build_tree(profiles, out->legacy, error, error_cap, 0)) {
        if (error && error_cap && !error[0]) error_text(error, error_cap, L"Not enough memory to retain old session paths.");
        kg_tree_free(out); return 0;
    }
    return 1;
}

static size_t exact_child(const KgTree *tree, size_t parent, const wchar_t *name, size_t length)
{
    const KgTreeNode *p = &tree->nodes[parent];
    wchar_t component[KG_TREE_MAX_NAME + 1u];
    KgTreeKind kind;
    if (!length || length > KG_TREE_MAX_NAME) return KG_TREE_NONE;
    memcpy(component, name, length * sizeof(*component));
    component[length] = 0;
    /* children_visible sorts by kind, then compare_names. Search each kind
     * with exactly the same ordering; case folding alone must never select
     * a different saved session. No index allocation or per-lookup scan. */
    for (kind = KG_TREE_GROUP; kind <= KG_TREE_SESSION; ++kind) {
        size_t first = 0, last = p->child_count;
        while (first < last) {
            size_t middle = first + (last - first) / 2;
            const KgTreeNode *node = &tree->nodes[p->children[middle]];
            int c = node->kind < kind ? -1 : node->kind > kind ? 1 :
                    compare_names(node->name, component);
            if (c < 0) first = middle + 1;
            else last = middle;
        }
        if (first < p->child_count) {
            size_t child = p->children[first];
            if (tree->nodes[child].kind == kind &&
                !wcscmp(tree->nodes[child].name, component)) return child;
        }
    }
    return KG_TREE_NONE;
}

static void fill_match(const KgTree *tree, size_t node, const wchar_t *relative,
                       int legacy, KgTreeMatch *match)
{
    match->kind = tree->nodes[node].kind; match->node_index = node;
    match->profile_index = tree->nodes[node].profile_index;
    match->relative = relative; match->legacy_alias = legacy;
}

static int resolve_tree(const KgTree *tree, const KgProfiles *profiles, const wchar_t *path,
                    int allow_legacy, KgTreeMatch *match)
{
    const wchar_t *p = path, *end, *first;
    size_t parent = 0, child, n, i;
    int root_first = 1;
    if (match) { memset(match, 0, sizeof(*match)); match->node_index = match->profile_index = KG_TREE_NONE; }
    if (!tree || !tree->count || !path || !match || wcslen(path) > KG_TREE_MAX_PATH) return 0;
    while (*p == L'\\') ++p;
    if (!*p) { fill_match(tree, 0, p, 0, match); return 1; }
    first = p;
    for (;;) {
        end = wcschr(p, L'\\'); if (!end) end = p + wcslen(p);
        n = (size_t)(end - p);
        if (!n) return 0;
        child = exact_child(tree, parent, p, n);
        if (child == KG_TREE_NONE) {
            if (root_first && allow_legacy && profiles && profiles->count == tree->profile_count) {
                size_t found = KG_TREE_NONE;
                for (i = 0; i < profiles->count; ++i) {
                    const wchar_t *alias = profiles->items[i].display_name;
                    if (alias && wcslen(alias) == n && !wcsncmp(alias, first, n)) {
                        if (found != KG_TREE_NONE) return 0; /* Ambiguous legacy aliases never choose a server. */
                        found = i;
                    }
                }
                if (found != KG_TREE_NONE && tree->profile_nodes[found] != KG_TREE_NONE) {
                    while (*end == L'\\') ++end;
                    fill_match(tree, tree->profile_nodes[found], end, 1, match); return 1;
                }
            }
            return 0;
        }
        root_first = 0;
        if (tree->nodes[child].kind == KG_TREE_SESSION) {
            while (*end == L'\\') ++end;
            fill_match(tree, child, end, 0, match); return 1;
        }
        if (!*end) { fill_match(tree, child, end, 0, match); return 1; }
        p = end + 1;
        if (!*p) { fill_match(tree, child, p, 0, match); return 1; }
        if (*p == L'\\') return 0;
        parent = child;
    }
}

/* Current visible names always win. An old unambiguous path resolves to the
 * same profile index, but must return the new node so redirects stay canonical.
 * Never rewrite or decode the remaining remote filename. */
int kg_tree_resolve(const KgTree *tree, const KgProfiles *profiles, const wchar_t *path,
                    int allow_legacy, KgTreeMatch *match)
{
    KgTreeMatch old;
    if (resolve_tree(tree, profiles, path, allow_legacy, match)) return 1;
    if (!tree || !tree->legacy || !match || !allow_legacy ||
        !resolve_tree(tree->legacy, profiles, path, 0, &old) ||
        old.kind != KG_TREE_SESSION || old.profile_index >= tree->profile_count) return 0;
    fill_match(tree, tree->profile_nodes[old.profile_index], old.relative, 1, match);
    return 1;
}
