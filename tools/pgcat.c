/* pgcat - manifest validator and catalog generator for the Pi Games repository.
 *
 *   pgcat --root DIR validate [id...]      check manifests, icons and channel files (exit 1 on any error)
 *   pgcat --root DIR catalog               JSON catalog of every valid game on stdout
 *   pgcat --root DIR list [@channel]       tab separated: id version channel tested name
 *   pgcat --root DIR ids TOKEN...          expand ids and @channels to a sorted unique id list
 *   pgcat --root DIR get ID KEY            print one manifest value
 *   pgcat --root DIR channels              channel names (the channels directory plus the implicit "incubating")
 *
 * Plain C, no dependencies. The manifest format is documented in docs/manifest.md and is the contract
 * other programs (launchers, the `pg` subscriber) rely on - change it only together with that document.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "pg_version.h"

#define MAX_GAMES 256
#define MAX_CHAN  16
#define MAX_KV    24

static const char *root = ".";
static int errors;

typedef struct { char key[48]; char val[256]; } KV;
typedef struct { KV kv[MAX_KV]; int n; } Man;
typedef struct { char name[48]; char members[MAX_GAMES][64]; int n; } Channel;

static char games[MAX_GAMES][64];
static int  ngames;
static Channel chans[MAX_CHAN];
static int  nchan;

static void fail(const char *where, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "%s: ", where);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
    errors++;
}

static int cmpstr(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

/* ---------------------------------------------------------------- scanning */
static int is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }

static void scan_games(void)
{
    char path[512]; snprintf(path, sizeof path, "%s/games", root);
    DIR *d = opendir(path);
    if (!d) return;
    char *names[MAX_GAMES]; int n = 0; struct dirent *e;
    while ((e = readdir(d)) && n < MAX_GAMES) {
        if (e->d_name[0] == '.' || e->d_name[0] == '_' || strlen(e->d_name) >= 64) continue;
        char p[600]; snprintf(p, sizeof p, "%s/games/%s", root, e->d_name);
        if (is_dir(p)) names[n++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof *names, cmpstr);
    for (int i = 0; i < n; i++) { snprintf(games[i], sizeof games[i], "%s", names[i]); free(names[i]); }
    ngames = n;
}

static int game_exists(const char *id) { for (int i = 0; i < ngames; i++) if (!strcmp(games[i], id)) return 1; return 0; }

static int valid_channel_name(const char *s)
{
    if (!islower((unsigned char)s[0])) return 0;
    for (const char *p = s; *p; p++) if (!islower((unsigned char)*p) && !isdigit((unsigned char)*p) && *p != '-') return 0;
    return strlen(s) <= 32;
}

static void trim(char *s)
{
    char *a = s; while (isspace((unsigned char)*a)) a++;
    memmove(s, a, strlen(a) + 1);
    size_t n = strlen(s); while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

static void scan_channels(void)
{
    char path[512]; snprintf(path, sizeof path, "%s/channels", root);
    DIR *d = opendir(path);
    if (!d) return;
    char *names[MAX_CHAN]; int n = 0; struct dirent *e;
    while ((e = readdir(d)) && n < MAX_CHAN) {
        size_t l = strlen(e->d_name);
        if (l > 4 && !strcmp(e->d_name + l - 4, ".txt")) names[n++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof *names, cmpstr);
    for (int i = 0; i < n; i++) {
        Channel *c = &chans[nchan];
        snprintf(c->name, sizeof c->name, "%.*s", (int)(strlen(names[i]) - 4), names[i]);
        char fp[700]; snprintf(fp, sizeof fp, "%s/channels/%s", root, names[i]);
        FILE *f = fopen(fp, "r");
        free(names[i]);
        if (!f) continue;
        char line[256];
        while (fgets(line, sizeof line, f)) {
            char *h = strchr(line, '#'); if (h) *h = 0;
            trim(line);
            if (!line[0] || c->n >= MAX_GAMES) continue;
            snprintf(c->members[c->n++], 64, "%.63s", line);
        }
        fclose(f);
        nchan++;
    }
}

static const char *channel_of(const char *id)
{
    for (int i = 0; i < nchan; i++) for (int j = 0; j < chans[i].n; j++) if (!strcmp(chans[i].members[j], id)) return chans[i].name;
    return "incubating";
}

/* ---------------------------------------------------------------- manifests */
static const char *mget(const Man *m, const char *k) { for (int i = 0; i < m->n; i++) if (!strcmp(m->kv[i].key, k)) return m->kv[i].val; return NULL; }

static int utf8_ok(const unsigned char *s)
{
    while (*s) {
        if (*s < 0x20 || *s == 0x7F) return 0;
        if (*s < 0x80) { s++; continue; }
        int n = (*s & 0xE0) == 0xC0 ? 1 : (*s & 0xF0) == 0xE0 ? 2 : (*s & 0xF8) == 0xF0 ? 3 : -1;
        if (n < 0) return 0;
        s++;
        for (int i = 0; i < n; i++, s++) if ((*s & 0xC0) != 0x80) return 0;
    }
    return 1;
}

static const char *known[] = { "id", "name", "version", "author", "license", "description", "sdk_api", "players", "tags", "tested", NULL };
static const char *required[] = { "id", "name", "version", "author", "license", "description", "sdk_api", "players", NULL };
static const char *licenses[] = { "MIT", "BSD-2-Clause", "BSD-3-Clause", "ISC", "Zlib", "Apache-2.0", "CC0-1.0", "Unlicense", "GPL-2.0-or-later", "GPL-3.0-or-later", NULL };
static const char *reserved[] = { "installed", "saves", "src", "build", "subscriptions", "channels", "games", "tools", "sdk", "docs", NULL };

static int in_list(const char *const *l, const char *s) { for (; *l; l++) if (!strcmp(*l, s)) return 1; return 0; }

static int load_manifest(const char *id, Man *m)
{
    char path[600], where[640];
    snprintf(path, sizeof path, "%s/games/%s/game.ini", root, id);
    snprintf(where, sizeof where, "games/%s/game.ini", id);
    memset(m, 0, sizeof *m);
    FILE *f = fopen(path, "r");
    if (!f) { fail(where, "missing"); return 0; }
    char line[1024]; int ln = 0, ok = 1;
    while (fgets(line, sizeof line, f)) {
        ln++;
        size_t len = strlen(line);
        if (len && line[len - 1] != '\n' && !feof(f)) { fail(where, "line %d too long", ln); ok = 0; int c; while ((c = fgetc(f)) != '\n' && c != EOF) {} continue; }
        trim(line);
        if (!line[0] || line[0] == '#') continue;
        char *eq = strchr(line, '=');
        if (!eq) { fail(where, "line %d: expected 'key = value'", ln); ok = 0; continue; }
        *eq = 0;
        char *k = line, *v = eq + 1; trim(k); trim(v);
        if (!k[0] || strlen(k) >= sizeof m->kv[0].key || strlen(v) >= sizeof m->kv[0].val) { fail(where, "line %d: key or value too long/empty key", ln); ok = 0; continue; }
        for (char *p = k; *p; p++) if (!(islower((unsigned char)*p) || isdigit((unsigned char)*p) || *p == '_' || *p == '-')) { fail(where, "line %d: bad key '%s'", ln, k); ok = 0; break; }
        if (!utf8_ok((const unsigned char *)v)) { fail(where, "line %d: value has control characters or invalid UTF-8", ln); ok = 0; }
        if (mget(m, k)) { fail(where, "line %d: duplicate key '%s'", ln, k); ok = 0; continue; }
        if (m->n >= MAX_KV) { fail(where, "too many keys"); ok = 0; break; }
        snprintf(m->kv[m->n].key, sizeof m->kv[m->n].key, "%s", k);
        snprintf(m->kv[m->n].val, sizeof m->kv[m->n].val, "%s", v);
        m->n++;
    }
    fclose(f);
    return ok;
}

static int digits(const char *s, size_t max) { size_t n = strlen(s); if (!n || n > max) return 0; for (; *s; s++) if (!isdigit((unsigned char)*s)) return 0; return 1; }

static void validate_game(const char *id)
{
    char where[640], p[700];
    snprintf(where, sizeof where, "games/%s/game.ini", id);
    Man m;
    int before = errors;
    if (!load_manifest(id, &m)) return;
    for (int i = 0; i < m.n; i++)
        if (!in_list(known, m.kv[i].key) && strncmp(m.kv[i].key, "x-", 2)) fail(where, "unknown key '%s' (custom keys must start with x-)", m.kv[i].key);
    for (const char *const *r = required; *r; r++) { const char *v = mget(&m, *r); if (!v || !*v) fail(where, "missing required key '%s'", *r); }

    const char *v;
    if ((v = mget(&m, "id")) && *v) {
        const char *s = v; int ok = islower((unsigned char)*s) && strlen(v) >= 2 && strlen(v) <= 32;
        for (; ok && *s; s++) if (!islower((unsigned char)*s) && !isdigit((unsigned char)*s) && *s != '-') ok = 0;
        if (!ok) fail(where, "id '%s' must match [a-z][a-z0-9-]{1,31}", v);
        if (strcmp(v, id)) fail(where, "id '%s' must equal the directory name '%s'", v, id);
        if (in_list(reserved, v)) fail(where, "id '%s' is reserved", v);
    }
    if ((v = mget(&m, "name")) && (strlen(v) > 40)) fail(where, "name longer than 40 characters");
    if ((v = mget(&m, "author")) && (strlen(v) > 80)) fail(where, "author longer than 80 characters");
    if ((v = mget(&m, "description")) && (strlen(v) > 140)) fail(where, "description longer than 140 characters");
    if ((v = mget(&m, "version")) && *v) {
        char buf[64]; snprintf(buf, sizeof buf, "%s", v);
        char *a = strtok(buf, "."), *b = a ? strtok(NULL, ".") : NULL, *c = b ? strtok(NULL, ".") : NULL, *x = c ? strtok(NULL, ".") : NULL;
        if (!a || !b || !c || x || !digits(a, 5) || !digits(b, 5) || !digits(c, 5)) fail(where, "version '%s' must be MAJOR.MINOR.PATCH", v);
    }
    if ((v = mget(&m, "license")) && *v && !in_list(licenses, v)) fail(where, "license '%s' is not on the allowed SPDX list (see docs/manifest.md)", v);
    if ((v = mget(&m, "sdk_api")) && *v) {
        if (!digits(v, 3) || atoi(v) < 1 || atoi(v) > PG_API_VERSION) fail(where, "sdk_api '%s' must be an integer 1..%d", v, PG_API_VERSION);
    }
    if ((v = mget(&m, "players")) && *v) {
        int ok = v[0] >= '1' && v[0] <= '4' && (v[1] == 0 || (v[1] == '-' && v[2] >= v[0] && v[2] <= '4' && v[3] == 0));
        if (!ok) fail(where, "players '%s' must look like 1 or 1-2 (max 4)", v);
    }
    if ((v = mget(&m, "tags")) && *v) {
        char buf[256]; snprintf(buf, sizeof buf, "%s", v);
        int count = 0;
        for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
            trim(t); count++;
            if (!*t) { fail(where, "empty tag"); continue; }
            for (char *q = t; *q; q++) if (!islower((unsigned char)*q) && !isdigit((unsigned char)*q) && *q != '-') { fail(where, "tag '%s' must be lowercase letters, digits, dashes", t); break; }
        }
        if (count > 6) fail(where, "at most 6 tags");
    }
    if ((v = mget(&m, "tested")) && !(!strcmp(v, "none") || !strcmp(v, "desktop") || !strcmp(v, "pi"))) fail(where, "tested must be none, desktop or pi");

    /* files */
    snprintf(p, sizeof p, "%s/games/%s/src", root, id);
    if (!is_dir(p)) fail(where, "missing src/ directory");
    snprintf(p, sizeof p, "%s/games/%s/icon.bmp", root, id);
    FILE *f = fopen(p, "rb");
    if (!f) fail(where, "missing icon.bmp (run: make icon GAME=%s)", id);
    else {
        unsigned char h[32];
        size_t n = fread(h, 1, sizeof h, f);
        fclose(f);
        if (n < 30 || h[0] != 'B' || h[1] != 'M') fail(where, "icon.bmp is not a BMP file");
        else {
            int w = (int)(h[18] | h[19] << 8 | h[20] << 16 | h[21] << 24), ht = (int)(h[22] | h[23] << 8 | h[24] << 16 | h[25] << 24);
            int bpp = h[28] | h[29] << 8;
            if (ht < 0) ht = -ht;
            if (w != 256 || ht != 256) fail(where, "icon.bmp must be 256x256 (is %dx%d)", w, ht);
            if (bpp != 24 && bpp != 32) fail(where, "icon.bmp must be 24 or 32 bits per pixel (is %d)", bpp);
        }
    }
    (void)before;
}

static void validate_channels(void)
{
    for (int i = 0; i < nchan; i++) {
        char where[100]; snprintf(where, sizeof where, "channels/%.48s.txt", chans[i].name);
        if (!valid_channel_name(chans[i].name) || !strcmp(chans[i].name, "incubating")) fail(where, "bad channel name (lowercase, digits, dashes; 'incubating' is implicit)");
        for (int j = 0; j < chans[i].n; j++) {
            const char *id = chans[i].members[j];
            if (!game_exists(id)) fail(where, "lists '%s' but games/%s does not exist", id, id);
            for (int k = 0; k < nchan; k++)
                for (int l = 0; l < chans[k].n; l++)
                    if ((k > i || (k == i && l > j)) && !strcmp(chans[k].members[l], id))
                        fail(where, "'%s' is listed in more than one channel (%s and %s): a game lives in exactly one", id, chans[i].name, chans[k].name);
        }
    }
}

/* -------------------------------------------------------------------- JSON */
static void json_str(const char *s)
{
    putchar('"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') { putchar('\\'); putchar(*s); }
        else putchar(*s);
    }
    putchar('"');
}

static void cmd_catalog(void)
{
    printf("{\n  \"schema\": 1,\n  \"sdk_api\": %d,\n  \"channels\": [", PG_API_VERSION);
    for (int i = 0; i < nchan; i++) { printf("%s", i ? ", " : ""); json_str(chans[i].name); }
    printf("%s\"incubating\"],\n  \"games\": [", nchan ? ", " : "");
    int first = 1;
    for (int g = 0; g < ngames; g++) {
        int before = errors; Man m;
        if (!load_manifest(games[g], &m)) { fprintf(stderr, "pgcat: skipping %s (invalid manifest)\n", games[g]); continue; }
        (void)before;
        printf("%s\n    {", first ? "" : ",");
        first = 0;
        static const char *keys[] = { "id", "name", "version", "author", "license", "description", "players", NULL };
        for (int k = 0; keys[k]; k++) { printf("%s", k ? ", " : ""); json_str(keys[k]); printf(": "); json_str(mget(&m, keys[k]) ? mget(&m, keys[k]) : ""); }
        printf(", \"sdk_api\": %d", mget(&m, "sdk_api") ? atoi(mget(&m, "sdk_api")) : 0);
        printf(", \"tested\": "); json_str(mget(&m, "tested") ? mget(&m, "tested") : "none");
        printf(", \"tags\": [");
        const char *tg = mget(&m, "tags");
        if (tg) {
            char buf[256]; snprintf(buf, sizeof buf, "%s", tg); int c = 0;
            for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) { trim(t); printf("%s", c++ ? ", " : ""); json_str(t); }
        }
        printf("], \"channel\": "); json_str(channel_of(games[g]));
        char path[160]; snprintf(path, sizeof path, "games/%s", games[g]);
        printf(", \"path\": "); json_str(path);
        snprintf(path, sizeof path, "games/%s/icon.bmp", games[g]);
        printf(", \"icon\": "); json_str(path);
        printf("}");
    }
    printf("\n  ]\n}\n");
}

/* ------------------------------------------------------------------ others */
static int cmp_id(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

static int cmd_ids(int argc, char **argv)
{
    static char out[MAX_GAMES][64]; int n = 0, bad = 0;
    for (int i = 0; i < argc; i++) {
        const char *t = argv[i];
        if (t[0] == '@') {
            const char *name = t + 1; int found = !strcmp(name, "incubating");
            for (int c = 0; c < nchan; c++) if (!strcmp(chans[c].name, name)) found = 1;
            if (!found) { fprintf(stderr, "pgcat: no such channel: %s\n", t); bad = 1; continue; }
            for (int g = 0; g < ngames; g++) if (!strcmp(channel_of(games[g]), name) && n < MAX_GAMES) snprintf(out[n++], 64, "%s", games[g]);
        } else {
            if (!game_exists(t)) { fprintf(stderr, "pgcat: no such game: %s\n", t); bad = 1; continue; }
            if (n < MAX_GAMES) snprintf(out[n++], 64, "%s", t);
        }
    }
    qsort(out, (size_t)n, 64, cmp_id);
    for (int i = 0; i < n; i++) if (i == 0 || strcmp(out[i], out[i - 1])) puts(out[i]);
    return bad;
}

static int cmd_list(const char *filter)
{
    for (int g = 0; g < ngames; g++) {
        Man m;
        if (!load_manifest(games[g], &m)) continue;
        const char *ch = channel_of(games[g]);
        if (filter && filter[0] == '@' && strcmp(filter + 1, ch)) continue;
        printf("%s\t%s\t%s\t%s\t%s\n", games[g], mget(&m, "version") ? mget(&m, "version") : "?", ch, mget(&m, "tested") ? mget(&m, "tested") : "none", mget(&m, "name") ? mget(&m, "name") : "");
    }
    return 0;
}

int main(int argc, char **argv)
{
    int i = 1;
    if (i + 1 < argc && !strcmp(argv[i], "--root")) { root = argv[i + 1]; i += 2; }
    if (i >= argc) { fprintf(stderr, "usage: pgcat [--root DIR] validate|catalog|list|ids|get|channels ...\n"); return 64; }
    const char *cmd = argv[i++];
    scan_games();
    scan_channels();

    if (!strcmp(cmd, "validate")) {
        if (i < argc) { for (; i < argc; i++) { if (!game_exists(argv[i])) fail(argv[i], "no such game"); else validate_game(argv[i]); } }
        else for (int g = 0; g < ngames; g++) validate_game(games[g]);
        validate_channels();
        if (errors) { fprintf(stderr, "pgcat: %d problem(s)\n", errors); return 1; }
        printf("manifests: OK (%d game%s, %d channel file%s)\n", ngames, ngames == 1 ? "" : "s", nchan, nchan == 1 ? "" : "s");
        return 0;
    }
    if (!strcmp(cmd, "catalog")) { cmd_catalog(); return 0; }
    if (!strcmp(cmd, "list"))    return cmd_list(i < argc ? argv[i] : NULL);
    if (!strcmp(cmd, "ids"))     return cmd_ids(argc - i, argv + i);
    if (!strcmp(cmd, "channels")) { for (int c = 0; c < nchan; c++) puts(chans[c].name); puts("incubating"); return 0; }
    if (!strcmp(cmd, "get") && i + 1 < argc) {
        Man m;
        if (!game_exists(argv[i]) || !load_manifest(argv[i], &m)) return 1;
        const char *v = mget(&m, argv[i + 1]);
        if (!v) return 1;
        puts(v);
        return 0;
    }
    fprintf(stderr, "pgcat: unknown command %s\n", cmd);
    return 64;
}
