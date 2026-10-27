/* Real offline hierarchy tests. No plugin implementation or SSH is mocked;
 * this program links only the production tree model and Windows string API. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../src/session_tree.h"

static unsigned checks, failures;
#define CHECK(expression) do { ++checks; if (!(expression)) { ++failures; \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression); } } while (0)

static int build(KgProfiles *p, KgTree *t)
{
    wchar_t error[512];
    int ok = kg_tree_build(p, t, error, 512);
    CHECK(ok);
    if (!ok) fwprintf(stderr, L"Tree error: %ls\n", error);
    return ok;
}

static KgTreeMatch resolve(KgTree *t, KgProfiles *p, const wchar_t *path,
                           KgTreeKind kind, size_t profile, const wchar_t *relative)
{
    KgTreeMatch m;
    int ok = kg_tree_resolve(t, p, path, 1, &m);
    CHECK(ok);
    if (!ok) { memset(&m, 0, sizeof(m)); m.node_index = m.profile_index = KG_TREE_NONE; return m; }
    CHECK(m.kind == kind);
    CHECK(m.profile_index == profile);
    CHECK(!wcscmp(m.relative, relative));
    return m;
}

static int has_child(KgTree *t, size_t parent, KgTreeKind kind, const wchar_t *name)
{
    size_t i;
    if (parent >= t->count) return 0;
    for (i = 0; i < t->nodes[parent].child_count; ++i) {
        KgTreeNode *n = &t->nodes[t->nodes[parent].children[i]];
        if (n->kind == kind && !wcscmp(n->name, name)) return 1;
    }
    return 0;
}

static void test_empty(void)
{
    KgProfiles p = {0}; KgTree t = {0}; KgTreeMatch m;
    if (!build(&p, &t)) return;
    CHECK(t.count == 1 && t.profile_count == 0);
    CHECK(t.nodes[0].kind == KG_TREE_GROUP && t.nodes[0].child_count == 0);
    CHECK(!wcscmp(t.nodes[0].path, L"\\"));
    resolve(&t, &p, L"\\", KG_TREE_GROUP, KG_TREE_NONE, L"");
    resolve(&t, &p, L"", KG_TREE_GROUP, KG_TREE_NONE, L"");
    CHECK(!kg_tree_resolve(&t, &p, L"\\missing", 1, &m));
    kg_tree_free(&t); CHECK(!t.nodes && !t.count && !t.profile_nodes);
    kg_tree_free(&t);
}

static void test_hierarchy(void)
{
    KgProfile items[] = {
        {L"VPN/Team/RU", L"VPN%2FTeam%2FRU", L"a", 22},
        {L"CONTRACT-MAIL", L"CONTRACT-MAIL", L"b", 22},
        {L"AUTOBUD/AB HOSTING", L"AUTOBUD%2FAB HOSTING", L"c", 2222},
        {L"VPN/ACTIVE VPN", L"VPN%2FACTIVE VPN", L"d", 22},
        {L"AUTOBUD/AB MAIL", L"AUTOBUD%2FAB MAIL", L"e", 22},
        {L"Работа/Сервер", L"Работа%2FСервер", L"f", 22}
    };
    KgProfiles p = {items, sizeof(items) / sizeof(items[0])};
    KgTree t = {0}; KgTreeMatch m, group;
    if (!build(&p, &t)) return;
    CHECK(t.count == 11 && t.profile_count == 6 && t.nodes[0].child_count == 4);
    CHECK(has_child(&t, 0, KG_TREE_GROUP, L"AUTOBUD"));
    CHECK(has_child(&t, 0, KG_TREE_GROUP, L"VPN"));
    CHECK(has_child(&t, 0, KG_TREE_GROUP, L"Работа"));
    CHECK(has_child(&t, 0, KG_TREE_SESSION, L"CONTRACT-MAIL"));
    CHECK(!has_child(&t, 0, KG_TREE_SESSION, L"VPN%2FTeam%2FRU"));
    CHECK(t.nodes[t.nodes[0].children[0]].kind == KG_TREE_GROUP);
    CHECK(t.nodes[t.nodes[0].children[3]].kind == KG_TREE_SESSION);
    group = resolve(&t, &p, L"\\VPN", KG_TREE_GROUP, KG_TREE_NONE, L"");
    CHECK(has_child(&t, group.node_index, KG_TREE_GROUP, L"Team"));
    CHECK(has_child(&t, group.node_index, KG_TREE_SESSION, L"ACTIVE VPN"));
    resolve(&t, &p, L"\\VPN\\", KG_TREE_GROUP, KG_TREE_NONE, L"");
    resolve(&t, &p, L"\\VPN\\Team\\", KG_TREE_GROUP, KG_TREE_NONE, L"");
    m = resolve(&t, &p, L"\\VPN\\Team\\RU", KG_TREE_SESSION, 0, L"");
    CHECK(!m.legacy_alias && m.node_index == t.profile_nodes[0]);
    CHECK(!wcscmp(t.nodes[m.node_index].path, L"\\VPN\\Team\\RU"));
    resolve(&t, &p, L"\\VPN\\Team\\RU\\etc\\ssh", KG_TREE_SESSION, 0, L"etc\\ssh");
    resolve(&t, &p, L"\\VPN\\Team\\RU\\~kg~613A62", KG_TREE_SESSION, 0, L"~kg~613A62");
    resolve(&t, &p, L"\\Работа\\Сервер\\дом\\100%20.txt", KG_TREE_SESSION, 5, L"дом\\100%20.txt");
    m = resolve(&t, &p, L"\\VPN%2FTeam%2FRU\\home\\test", KG_TREE_SESSION, 0, L"home\\test");
    CHECK(m.legacy_alias);
    CHECK(!kg_tree_resolve(&t, &p, L"\\VPN%2FTeam%2FRU", 0, &m));
    CHECK(!kg_tree_resolve(&t, &p, L"\\VPN\\unknown", 1, &m));
    CHECK(!kg_tree_resolve(&t, &p, L"\\VPN\\\\Team", 1, &m));
    CHECK(!kg_tree_resolve(&t, &p, L"\\VPN\\..", 1, &m));
    CHECK(!kg_tree_resolve(&t, &p, L"\\vpn", 1, &m));
    kg_tree_free(&t);
}

static void test_escaping_and_reserved(void)
{
    KgProfile items[] = {
        {L"100%/raw%session", L"100%25%2Fraw%25session", L"a", 22},
        {L"Lab:One/Host?", L"Lab%3AOne%2FHost%3F", L"b", 22},
        {L"Folder\\literal/Name", L"Folder%5Cliteral%2FName", L"c", 22},
        {L"CON/Server.", L"CON%2FServer%2E", L"d", 22},
        {KG_SETTINGS_NAME, L"%5BНастройки KiTTY-ng]", L"e", 22},
        {L"Literal%2F/Leaf", L"Literal%252F%2FLeaf", L"f", 22}
    };
    KgProfiles p = {items, sizeof(items) / sizeof(items[0])};
    KgTree t = {0}; KgTreeMatch m;
    if (!build(&p, &t)) return;
    resolve(&t, &p, L"\\100%25\\raw%25session", KG_TREE_SESSION, 0, L"");
    resolve(&t, &p, L"\\Lab%3AOne\\Host%3F", KG_TREE_SESSION, 1, L"");
    resolve(&t, &p, L"\\Folder%5Cliteral\\Name", KG_TREE_SESSION, 2, L"");
    resolve(&t, &p, L"\\%43ON\\Server%2E", KG_TREE_SESSION, 3, L"");
    resolve(&t, &p, L"\\%5BНастройки KiTTY-ng]", KG_TREE_SESSION, 4, L"");
    resolve(&t, &p, L"\\Literal%252F\\Leaf", KG_TREE_SESSION, 5, L"");
    CHECK(!kg_tree_resolve(&t, &p, L"\\Literal%2F\\Leaf", 1, &m));
    CHECK(!has_child(&t, 0, KG_TREE_SESSION, KG_SETTINGS_NAME));
    kg_tree_free(&t);
}

static void test_collisions_and_stability(void)
{
    KgProfile items[] = {
        {L"VPN", L"VPN", L"standalone", 22},
        {L"VPN/Group member", L"VPN%2FGroup member", L"member", 22},
        {L"VPN [SSH]", L"VPN [SSH]", L"literal", 22},
        {L"Alpha", L"Alpha", L"upper", 22},
        {L"alpha", L"alpha", L"lower", 22},
        {L"A/B", L"A%2FB", L"b", 22},
        {L"a/C", L"a%2FC", L"c", 22}
    };
    KgProfile reversed[sizeof(items) / sizeof(items[0])];
    KgProfiles p = {items, sizeof(items) / sizeof(items[0])}, r = {reversed, sizeof(items) / sizeof(items[0])};
    KgTree t = {0}, second = {0}; KgTreeMatch m;
    size_t i, j;
    if (!build(&p, &t)) return;
    resolve(&t, &p, L"\\VPN", KG_TREE_GROUP, KG_TREE_NONE, L"");
    m = resolve(&t, &p, L"\\VPN\\Group member", KG_TREE_SESSION, 1, L"");
    CHECK(!m.legacy_alias);
    resolve(&t, &p, L"\\VPN [SSH]", KG_TREE_SESSION, 2, L"");
    resolve(&t, &p, L"\\VPN [SSH 2]", KG_TREE_SESSION, 0, L"");
    resolve(&t, &p, L"\\Alpha [SSH]", KG_TREE_SESSION, 3, L"");
    resolve(&t, &p, L"\\alpha [SSH 2]", KG_TREE_SESSION, 4, L"");
    resolve(&t, &p, L"\\A\\B", KG_TREE_SESSION, 5, L"");
    resolve(&t, &p, L"\\A\\C", KG_TREE_SESSION, 6, L"");
    CHECK(!kg_tree_resolve(&t, &p, L"\\VPN\\oldRemotePath", 1, &m));
    /* Reversing source discovery order changes profile indexes, not visible
     * names, sort order, logical connection identity or navigation. */
    for (i = 0; i < p.count; ++i) reversed[i] = items[p.count - i - 1];
    if (build(&r, &second)) {
        CHECK(t.count == second.count && t.nodes[0].child_count == second.nodes[0].child_count);
        for (i = 0; i < t.nodes[0].child_count; ++i)
            CHECK(!wcscmp(t.nodes[t.nodes[0].children[i]].name, second.nodes[second.nodes[0].children[i]].name));
        for (i = 0; i < p.count; ++i) {
            for (j = 0; j < r.count; ++j) if (!wcscmp(p.items[i].load_name, r.items[j].load_name)) break;
            CHECK(j < r.count);
            if (j < r.count) CHECK(!wcscmp(t.nodes[t.profile_nodes[i]].path, second.nodes[second.profile_nodes[j]].path));
        }
    }
    kg_tree_free(&t); kg_tree_free(&second);
}

static void test_failures(void)
{
    wchar_t error[256], long_name[256], deep[2048];
    KgProfile item[] = {{L"", L"", L"a", 22}, {L"", L"", L"b", 22}};
    KgProfiles p = {item, 1}; KgTree t = {0}; size_t i;
    const wchar_t *bad[] = {L"", L"/name", L"group/", L"group//name"};
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        item[0].load_name = (wchar_t *)bad[i];
        CHECK(!kg_tree_build(&p, &t, error, 256)); CHECK(error[0] && !t.count && !t.nodes);
    }
    item[0].load_name = item[1].load_name = L"Duplicate"; p.count = 2;
    CHECK(!kg_tree_build(&p, &t, error, 256)); CHECK(error[0] && !t.count);
    p.count = 1;
    deep[0] = 0;
    for (i = 0; i < 17; ++i) wcscat(deep, L"g/");
    wcscat(deep, L"session"); item[0].load_name = deep;
    CHECK(!kg_tree_build(&p, &t, error, 256)); CHECK(error[0] && !t.count);
    /* All 16 supported groups still must obey the total WFX path bound. */
    deep[0] = 0;
    for (i = 0; i < 16; ++i) wcscat(deep, L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghij/");
    wcscat(deep, L"session"); item[0].load_name = deep;
    CHECK(!kg_tree_build(&p, &t, error, 256)); CHECK(error[0] && !t.count);
    for (i = 0; i < 255; ++i) long_name[i] = L'a'; long_name[255] = 0;
    item[0].load_name = long_name;
    if (build(&p, &t)) { CHECK(wcslen(t.nodes[t.profile_nodes[0]].name) == 255); kg_tree_free(&t); }
    /* A long collision must fail explicitly, never truncate into an alias. */
    wcscpy(deep, long_name); wcscat(deep, L"/member"); item[1].load_name = deep; p.count = 2;
    CHECK(!kg_tree_build(&p, &t, error, 256)); CHECK(error[0] && !t.count);
}

static void test_large_list(void)
{
    enum { COUNT = 10000 };
    KgProfiles p = {0}; KgTree t = {0};
    size_t i;
    ULONGLONG start = GetTickCount64();
    p.count = COUNT; p.items = calloc(p.count, sizeof(*p.items));
    CHECK(p.items != NULL); if (!p.items) return;
    for (i = 0; i < p.count; ++i) {
        p.items[i].load_name = malloc(80 * sizeof(wchar_t));
        p.items[i].display_name = malloc(80 * sizeof(wchar_t));
        if (!p.items[i].load_name || !p.items[i].display_name) { CHECK(0); goto done; }
        swprintf(p.items[i].load_name, 80, L"Group%03u/Region%03u/Server%05u", (unsigned)(i % 100), (unsigned)(i % 10), (unsigned)i);
        swprintf(p.items[i].display_name, 80, L"Group%03u%%2FRegion%03u%%2FServer%05u", (unsigned)(i % 100), (unsigned)(i % 10), (unsigned)i);
    }
    if (build(&p, &t)) {
        CHECK(t.profile_count == COUNT && t.nodes[0].child_count == 100);
        CHECK(t.count == COUNT + 201);
        for (i = 0; i < p.count; i += 73) {
            KgTreeMatch m;
            CHECK(t.profile_nodes[i] < t.count);
            CHECK(kg_tree_resolve(&t, &p, t.nodes[t.profile_nodes[i]].path, 0, &m));
            CHECK(m.kind == KG_TREE_SESSION && m.profile_index == i && !*m.relative);
        }
    }
done:
    kg_tree_free(&t);
    for (i = 0; i < p.count; ++i) { free(p.items[i].load_name); free(p.items[i].display_name); }
    free(p.items);
    printf("10,000-profile hierarchy fixture: %llu ms.\n", (unsigned long long)(GetTickCount64() - start));
}

/* A wide mixed group catches assumptions that the entire list is sorted
 * by name: all groups precede all sessions, even when names interleave. */
static void test_wide_lookup(void)
{
    enum { COUNT = 10000 };
    KgProfiles p = {0}; KgTree t = {0}; KgTreeMatch m;
    size_t i;
    wchar_t path[128];
    ULONGLONG start;
    p.count = COUNT; p.items = calloc(COUNT, sizeof(*p.items));
    CHECK(p.items != NULL); if (!p.items) return;
    for (i = 0; i < p.count; ++i) {
        p.items[i].load_name = malloc(80 * sizeof(wchar_t));
        if (!p.items[i].load_name) { CHECK(0); goto done; }
        swprintf(p.items[i].load_name, 80, i % 2 ? L"Item%05u" : L"Item%05u/Member", (unsigned)i);
    }
    if (!build(&p, &t)) goto done;
    CHECK(t.nodes[0].child_count == COUNT);
    start = GetTickCount64();
    for (i = 0; i < p.count; ++i) {
        CHECK(kg_tree_resolve(&t, &p, t.nodes[t.profile_nodes[i]].path, 0, &m));
        CHECK(m.kind == KG_TREE_SESSION && m.profile_index == i && !*m.relative);
        swprintf(path, 128, L"\\Item%05u-missing", (unsigned)i);
        CHECK(!kg_tree_resolve(&t, &p, path, 0, &m));
        swprintf(path, 128, L"\\item%05u", (unsigned)i);
        CHECK(!kg_tree_resolve(&t, &p, path, 0, &m));
    }
    printf("10,000 mixed siblings / 30,000 lookups: %llu ms.\n",
           (unsigned long long)(GetTickCount64() - start));
done:
    kg_tree_free(&t);
    for (i = 0; i < p.count; ++i) free(p.items[i].load_name);
    free(p.items);
}

int main(void)
{
    test_empty(); test_hierarchy(); test_escaping_and_reserved();
    test_collisions_and_stability(); test_failures(); test_large_list(); test_wide_lookup();
    printf("Session tree: %u checks, %u failures.\n", checks, failures);
    return failures ? 1 : 0;
}
