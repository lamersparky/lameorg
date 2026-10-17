#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef NAME_MAX
#define NAME_MAX 255
#endif

#define MANIFEST_PREFIX ".lameorg-manifest-"
#define MANIFEST_SUFFIX ".tsv"

struct counts { size_t moved, skipped, errors; };

static void usage(FILE *out, const char *argv0) {
    fprintf(out,
        "Usage: %s [--dry-run] [--undo MANIFEST]\n"
        "Organize visible regular files in the current directory by extension.\n\n"
        "  --dry-run       Preview planned moves without changing anything\n"
        "  --undo FILE     Reverse moves recorded in FILE\n"
        "  --help          Show this help\n\n"
        "Successful organization runs create a hidden manifest for undo.\n", argv0);
}

static const char *extension_for(const char *name, char *out, size_t outsz) {
    static const char *compound[] = {
        ".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst", ".tar.lz", ".tar.lzma",
        ".tar.lzo", ".tar.Z", ".tar.br", ".tar.7z", ".tar.zip", NULL
    };
    size_t n = strlen(name);
    for (size_t i = 0; compound[i]; ++i) {
        size_t m = strlen(compound[i]);
        if (n > m && strcasecmp(name + n - m, compound[i]) == 0) {
            if (m >= outsz) return NULL;
            memcpy(out, compound[i] + 1, m); out[m - 1] = '\0';
            return out;
        }
    }
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name || dot[1] == '\0') return NULL;
    size_t m = strlen(dot + 1);
    if (m >= outsz) return NULL;
    for (size_t i = 0; i < m; ++i) {
        unsigned char c = (unsigned char)dot[1 + i];
        if (!(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') &&
            !(c >= '0' && c <= '9') && c != '_' && c != '-' && c != '+') return NULL;
        out[i] = (char)c;
    }
    out[m] = '\0';
    for (size_t i = 0; i < m; ++i)
        if (out[i] >= 'A' && out[i] <= 'Z') out[i] = (char)(out[i] - 'A' + 'a');
    return out;
}

static int hex_encode(const char *s, char *out, size_t cap) {
    static const char h[] = "0123456789abcdef";
    size_t n = strlen(s);
    if (n > (cap - 1) / 2) return -1;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        out[i * 2] = h[c >> 4]; out[i * 2 + 1] = h[c & 15];
    }
    out[n * 2] = '\0';
    return 0;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char *hex_decode(const char *s) {
    size_t n = strlen(s);
    if (n % 2) return NULL;
    char *out = malloc(n / 2 + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < n; i += 2) {
        int hi = hex_value(s[i]), lo = hex_value(s[i + 1]);
        if (hi < 0 || lo < 0 || (hi == 0 && lo == 0)) { free(out); return NULL; }
        out[i / 2] = (char)((hi << 4) | lo);
    }
    out[n / 2] = '\0';
    return out;
}

/* linkat() fails rather than replacing an existing destination. Since the
 * destination is a subdirectory of the source directory, it is on the same
 * filesystem. Unlink the source only after the link succeeds. */
static int move_noreplace(const char *src, const char *dir, const char *dst) {
    if (linkat(AT_FDCWD, src, AT_FDCWD, dst, 0) != 0) return -1;
    if (unlinkat(AT_FDCWD, src, 0) != 0) {
        int saved = errno;
        (void)unlinkat(AT_FDCWD, dst, 0);
        errno = saved;
        return -1;
    }
    (void)dir;
    return 0;
}

static int create_manifest(char *path, size_t cap) {
    time_t now = time(NULL); struct tm tmv;
    if (localtime_r(&now, &tmv) == NULL) return -1;
    char stamp[32];
    if (strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tmv) == 0) return -1;
    for (unsigned int i = 0; i < 1000; ++i) {
        int n = snprintf(path, cap, "%s%s-%ld-%u%s", MANIFEST_PREFIX, stamp,
                         (long)getpid(), i, MANIFEST_SUFFIX);
        if (n < 0 || (size_t)n >= cap) { errno = ENAMETOOLONG; return -1; }
        int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd >= 0) {
            FILE *f = fdopen(fd, "w");
            if (!f) { int e = errno; close(fd); unlink(path); errno = e; return -1; }
            if (fprintf(f, "LAMEORG1\n") < 0 || fflush(f) != 0) {
                int e = errno; fclose(f); unlink(path); errno = e; return -1;
            }
            fclose(f);
            return 0;
        }
        if (errno != EEXIST) return -1;
    }
    errno = EEXIST; return -1;
}

static int append_manifest(const char *manifest, const char *src, const char *dst) {
    char a[(NAME_MAX + 1) * 2 + 1], b[(NAME_MAX * 2) + 1];
    if (hex_encode(src, a, sizeof a) || hex_encode(dst, b, sizeof b)) { errno = ENAMETOOLONG; return -1; }
    FILE *f = fopen(manifest, "a");
    if (!f) return -1;
    int rc = fprintf(f, "%s\t%s\n", a, b) < 0 ? -1 : 0;
    if (rc == 0 && fflush(f) != 0) rc = -1;
    if (fclose(f) != 0) rc = -1;
    return rc;
}

static int organize(bool dry_run) {
    DIR *dir = opendir(".");
    if (!dir) { perror("lameorg: opendir"); return 1; }
    struct dirent *ent;
    struct counts c = {0};
    char manifest[NAME_MAX + 1] = {0};
    bool manifest_created = false;

    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        if (name[0] == '.' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) { c.skipped++; continue; }
        struct stat st;
        if (lstat(name, &st) != 0) { fprintf(stderr, "lameorg: cannot inspect '%s': %s\n", name, strerror(errno)); c.errors++; continue; }
        if (!S_ISREG(st.st_mode)) { c.skipped++; continue; }
        char ext[NAME_MAX + 1];
        if (!extension_for(name, ext, sizeof ext)) { c.skipped++; continue; }
        char dest[NAME_MAX + 1];
        int n = snprintf(dest, sizeof dest, "%s/%s", ext, name);
        if (n < 0 || (size_t)n >= sizeof dest) { fprintf(stderr, "lameorg: destination path too long for '%s'\n", name); c.errors++; continue; }
        struct stat dstst;
        if (lstat(dest, &dstst) == 0 || errno != ENOENT) {
            printf("SKIP  %s -> %s (destination exists or cannot be checked)\n", name, dest);
            c.skipped++; continue;
        }
        struct stat dirst;
        if (lstat(ext, &dirst) == 0 && !S_ISDIR(dirst.st_mode)) {
            printf("SKIP  %s (extension path '%s' is not a directory)\n", name, ext);
            c.skipped++; continue;
        }
        if (dry_run) { printf("WOULD %s -> %s\n", name, dest); c.moved++; continue; }
        if (!manifest_created) {
            if (create_manifest(manifest, sizeof manifest) != 0) {
                fprintf(stderr, "lameorg: cannot create undo manifest: %s\n", strerror(errno)); closedir(dir); return 1;
            }
            manifest_created = true;
        }
        if (mkdir(ext, 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "lameorg: cannot create '%s': %s\n", ext, strerror(errno)); c.errors++; continue;
        }
        if (lstat(ext, &dirst) != 0 || !S_ISDIR(dirst.st_mode)) {
            fprintf(stderr, "lameorg: '%s' is not a directory\n", ext); c.errors++; continue;
        }
        if (move_noreplace(name, ext, dest) != 0) {
            fprintf(stderr, "lameorg: cannot move '%s': %s\n", name, strerror(errno)); c.errors++; continue;
        }
        if (append_manifest(manifest, name, dest) != 0) {
            fprintf(stderr, "lameorg: WARNING: moved '%s' but failed to record it in '%s': %s\n", name, manifest, strerror(errno)); c.errors++;
        }
        printf("MOVE  %s -> %s\n", name, dest); c.moved++;
    }
    closedir(dir);
    printf("\n%s: %zu %s, %zu skipped, %zu errors\n", dry_run ? "Dry run" : "LAMEORG",
           c.moved, dry_run ? "planned" : "moved", c.skipped, c.errors);
    if (manifest_created) printf("Undo manifest: %s\n", manifest);
    return c.errors ? 1 : 0;
}

static int undo_manifest(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "lameorg: cannot open manifest '%s': %s\n", path, strerror(errno)); return 1; }
    char *line = NULL; size_t cap = 0; ssize_t len = getline(&line, &cap, f);
    if (len < 0 || strcmp(line, "LAMEORG1\n") != 0) {
        fprintf(stderr, "lameorg: invalid manifest format\n"); free(line); fclose(f); return 1;
    }
    struct counts c = {0};
    while ((len = getline(&line, &cap, f)) >= 0) {
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        char *tab = strchr(line, '\t');
        if (!tab) { fprintf(stderr, "lameorg: malformed manifest record\n"); c.errors++; continue; }
        *tab++ = '\0';
        char *src = hex_decode(line), *dst = hex_decode(tab);
        if (!src || !dst || strchr(src, '/') || strchr(dst, '/') == NULL || strchr(dst, '/') != strrchr(dst, '/')) {
            fprintf(stderr, "lameorg: invalid manifest path record\n"); free(src); free(dst); c.errors++; continue;
        }
        struct stat st;
        if (lstat(src, &st) == 0 || errno != ENOENT) {
            fprintf(stderr, "SKIP  %s (original path already exists)\n", src); c.skipped++; free(src); free(dst); continue;
        }
        if (move_noreplace(dst, ".", src) != 0) {
            fprintf(stderr, "lameorg: cannot restore '%s': %s\n", src, strerror(errno)); c.errors++; free(src); free(dst); continue;
        }
        printf("UNDO  %s -> %s\n", dst, src); c.moved++;
        free(src); free(dst);
    }
    if (ferror(f)) { perror("lameorg: read manifest"); c.errors++; }
    free(line); fclose(f);
    printf("\nUndo: %zu restored, %zu skipped, %zu errors\n", c.moved, c.skipped, c.errors);
    return c.errors ? 1 : 0;
}

int main(int argc, char **argv) {
    bool dry_run = false;
    const char *undo = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) { usage(stdout, argv[0]); return 0; }
        else if (strcmp(argv[i], "--dry-run") == 0) dry_run = true;
        else if (strcmp(argv[i], "--undo") == 0) {
            if (++i >= argc || undo) { usage(stderr, argv[0]); return 2; }
            undo = argv[i];
        } else { fprintf(stderr, "lameorg: unknown argument '%s'\n", argv[i]); usage(stderr, argv[0]); return 2; }
    }
    if (undo && dry_run) { fprintf(stderr, "lameorg: --undo and --dry-run cannot be combined\n"); return 2; }
    return undo ? undo_manifest(undo) : organize(dry_run);
}
