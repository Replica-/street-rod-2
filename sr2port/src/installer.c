/* PORT (installer): before the game starts, the game folder is checked for the original's files. If any is missing, a
 * small window downloads the release from Street Rod Online (the copyright holder's site), unpacks it into the folder
 * and shows what it is doing with a progress bar; then the game starts. With the Amiga graphics (the default) and no
 * gfx/ folder the Amiga disks are fetched as well (the game makes gfx/ from them: enh/amiga_export.c).
 * Nothing is shown when the files are there. */
#include "installer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include <zlib.h>

#include "enh/amiga.h"

#ifdef _WIN32
#include <windows.h>
#include <wininet.h>
#endif

#define URL_PC    "https://www.streetrodonline.com/wp-content/uploads/2017/05/sr2pc.zip"
#define URL_AMIGA "https://www.streetrodonline.com/wp-content/uploads/2017/05/sr2amiga.zip"

static const char *const pc_files[] = {
    "SR.EXE", "LIB1", "LIB1C", "LIB2", "LIB2C", "LIB3", "LIB3C", "SR2_DATA", "SR2_END", "SR2_SHP",
    "MSR.SND", "MSRBEG.MUS", "MSR2.MUS", "MSR20.MUS", "MSR21.MUS", "MSR24.MUS", "PAUSE.MUS",
};
static const char *const amiga_files[] = { "SR2AMIGA_DISK1.adf", "SR2AMIGA_DISK2.adf" };

/* ---------------------------------------------------------------- state shared with the worker thread */

static SDL_Mutex *lock;
static char status[160], detail[160], error_msg[256];
static Sint64 got, total;                       /* bytes of the current download (total 0 = unknown) */
static float overall;                           /* 0..1 over the whole job */
static SDL_AtomicInt done, cancel;
static const char *dir;
static bool need_pc, need_amiga, make_gfx;

static void set_status(const char *s, const char *d)
{
    SDL_LockMutex(lock);
    SDL_strlcpy(status, s, sizeof status);
    SDL_strlcpy(detail, d ? d : "", sizeof detail);
    SDL_UnlockMutex(lock);
}

static bool fail(const char *fmt, const char *arg)
{
    SDL_LockMutex(lock);
    SDL_snprintf(error_msg, sizeof error_msg, fmt, arg);
    SDL_UnlockMutex(lock);
    return false;
}

static bool have_file(const char *name)
{
    int count = 0;
    char **entries = SDL_GlobDirectory(dir, NULL, 0, &count);
    bool found = false;
    for (int i = 0; entries && i < count && !found; i++) found = SDL_strcasecmp(entries[i], name) == 0;
    SDL_free(entries);
    return found;
}


/* ---------------------------------------------------------------- download */

static bool download(const char *url, Uint8 **out, size_t *out_n)
{
    size_t cap = 1 << 20, n = 0;
    Uint8 *buf = malloc(cap);
    if (!buf) return fail("Out of memory%s", "");
    SDL_LockMutex(lock); got = 0; total = 0; SDL_UnlockMutex(lock);
#ifdef _WIN32
    HINTERNET net = InternetOpenA("StreetRod2Installer", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    HINTERNET h = net ? InternetOpenUrlA(net, url, NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0) : NULL;
    if (!h) {
        if (net) InternetCloseHandle(net);
        free(buf);
        return fail("Could not connect to %s (no internet connection?)", "www.streetrodonline.com");
    }
    DWORD code = 0, len = sizeof code;
    if (HttpQueryInfoA(h, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &code, &len, NULL) && code != 200) {
        InternetCloseHandle(h); InternetCloseHandle(net); free(buf);
        return fail("The download was refused by the server (%s)", url);
    }
    DWORD size = 0;
    len = sizeof size;
    if (HttpQueryInfoA(h, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &size, &len, NULL)) {
        SDL_LockMutex(lock); total = size; SDL_UnlockMutex(lock);
    }
    for (;;) {
        if (SDL_GetAtomicInt(&cancel)) { InternetCloseHandle(h); InternetCloseHandle(net); free(buf); return fail("Cancelled%s", ""); }
        if (n + 65536 > cap) {
            Uint8 *b = realloc(buf, cap *= 2);
            if (!b) { InternetCloseHandle(h); InternetCloseHandle(net); free(buf); return fail("Out of memory%s", ""); }
            buf = b;
        }
        DWORD r = 0;
        if (!InternetReadFile(h, buf + n, 65536, &r)) {
            InternetCloseHandle(h); InternetCloseHandle(net); free(buf);
            return fail("The download broke off (%s)", url);
        }
        if (r == 0) break;
        n += r;
        SDL_LockMutex(lock); got = (Sint64)n; SDL_UnlockMutex(lock);
    }
    InternetCloseHandle(h);
    InternetCloseHandle(net);
#else
    /* no progress here: curl into a temporary file */
    char tmp[512];
    SDL_snprintf(tmp, sizeof tmp, "%s/.download.zip", dir);
    const char *args[] = { "curl", "-fsSL", "-o", tmp, url, NULL };
    SDL_Process *pr = SDL_CreateProcess(args, false);
    int rc = -1;
    if (!pr || !SDL_WaitProcess(pr, true, &rc) || rc != 0) {
        if (pr) SDL_DestroyProcess(pr);
        free(buf);
        return fail("Could not download %s (is curl installed?)", url);
    }
    SDL_DestroyProcess(pr);
    free(buf);
    buf = SDL_LoadFile(tmp, &n);
    SDL_RemovePath(tmp);
    if (!buf) return fail("Could not read the download%s", "");
    SDL_LockMutex(lock); got = total = (Sint64)n; SDL_UnlockMutex(lock);
#endif
    *out = buf;
    *out_n = n;
    return true;
}

/* ---------------------------------------------------------------- MD5 check of every unpacked file */

/* the release files as Street Rod Online serves them (checked against a copy of the game 2026-11-15) */
static const struct { const char *name, *md5; } known[] = {
    { "LIB1", "446c85b67bb002c672d762492c9aea28" }, { "LIB1C", "5b4bc29ec5d52dcea6a628cc74e9068d" },
    { "LIB2", "2ebf8c545269055163a99031cd01f883" }, { "LIB2C", "be9be2d5b5242b2a94f1f516d81d6057" },
    { "LIB3", "4feb49ee5e7dabd634484c671b15ec41" }, { "LIB3C", "2844f03afa6e2d71b42e8999bf8e7817" },
    { "MSR.SND", "bd650d07a55fc19b2a1f6718e6a25364" }, { "MSR2.MUS", "9baf532f7af6947633228fee31b6c1a2" },
    { "MSR20.MUS", "dd58cf63973319d363bd010f2392f90f" }, { "MSR21.MUS", "313a59796f23a99eeca3220697e328be" },
    { "MSR24.MUS", "9a15ab86347d6a45759cb1cd0aabf0ef" }, { "MSRBEG.MUS", "67780b0b9025e7dac42f9542f4d7d56e" },
    { "PAUSE.MUS", "e1f0808cf49ee47a73b2d7496053982d" }, { "SR.EXE", "d122a18c91bb8d0003d5acbca5736a99" },
    { "SR2_DATA", "f121dcd412c3875e304a1d5d5feeda5c" }, { "SR2_END", "8c783d4e20a7d71426667cde622235e8" },
    { "SR2_SHP", "e6074d1ba830546f74fa9d86c1e27691" },
    { "SR2AMIGA_DISK1.adf", "832bcfbf1911140b44513daf97b1a88c" }, { "SR2AMIGA_DISK2.adf", "afd80b1ccdb2be2ad20714b4b9ef61af" },
};

/* MD5 (RFC 1321) of n bytes as 32 lower-case hex digits */
static void md5_hex(const Uint8 *msg, size_t n, char out[33])
{
    static const Uint32 K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8,
        0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
        0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87,
        0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
        0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039,
        0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
        0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb,
        0xeb86d391 };
    static const int R[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20,
                               5, 9, 14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                               6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
    Uint32 h[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
    size_t total = ((n + 8) / 64 + 1) * 64;
    Uint8 *m = calloc(total, 1);
    if (!m) { out[0] = 0; return; }
    memcpy(m, msg, n);
    m[n] = 0x80;
    Uint64 bits = (Uint64)n * 8;
    for (int i = 0; i < 8; i++) m[total - 8 + i] = (Uint8)(bits >> (8 * i));
    for (size_t off = 0; off < total; off += 64) {
        Uint32 w[16], a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 16; i++) w[i] = m[off + 4 * i] | m[off + 4 * i + 1] << 8 | (Uint32)m[off + 4 * i + 2] << 16 | (Uint32)m[off + 4 * i + 3] << 24;
        for (int i = 0; i < 64; i++) {
            Uint32 f;
            int g;
            if (i < 16) { f = (b & c) | (~b & d); g = i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
            else { f = c ^ (b | ~d); g = (7 * i) % 16; }
            Uint32 t = d;
            d = c; c = b;
            Uint32 x = a + f + K[i] + w[g];
            b = b + (x << R[i] | x >> (32 - R[i]));
            a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    }
    free(m);
    for (int i = 0; i < 16; i++) SDL_snprintf(out + 2 * i, 3, "%02x", (h[i / 4] >> (8 * (i % 4))) & 255);
}

/* ---------------------------------------------------------------- unzip (flat archives: the files at the top level) */

static Uint32 zrd32(const Uint8 *p) { return p[0] | p[1] << 8 | (Uint32)p[2] << 16 | (Uint32)p[3] << 24; }
static Uint16 zrd16(const Uint8 *p) { return (Uint16)(p[0] | p[1] << 8); }

static bool unzip(const Uint8 *z, size_t n, const char *what)
{
    size_t e = n >= 22 ? n - 22 : 0;
    while (e > 0 && zrd32(z + e) != 0x06054b50) e--;
    if (n < 22 || zrd32(z + e) != 0x06054b50) return fail("The %s download is not a zip file", what);
    int count = zrd16(z + e + 10);
    size_t cd = zrd32(z + e + 16);
    for (int k = 0; k < count; k++) {
        if (cd + 46 > n || zrd32(z + cd) != 0x02014b50) return fail("The %s zip file is damaged", what);
        int method = zrd16(z + cd + 10), nl = zrd16(z + cd + 28), xl = zrd16(z + cd + 30), cl = zrd16(z + cd + 32);
        Uint32 csize = zrd32(z + cd + 20), usize = zrd32(z + cd + 24), loc = zrd32(z + cd + 42);
        char name[256];
        SDL_snprintf(name, sizeof name, "%.*s", nl < 255 ? nl : 255, (const char *)z + cd + 46);
        cd += 46u + nl + xl + cl;
        if (strchr(name, '/') || strchr(name, '\\') || name[0] == '.' || !name[0]) continue;   /* __MACOSX/ etc. */
        const char *want = NULL;                        /* only the known files, each checked */
        for (size_t i = 0; i < SDL_arraysize(known); i++) if (SDL_strcasecmp(known[i].name, name) == 0) want = known[i].md5;
        if (!want) continue;
        if (loc + 30 > n || zrd32(z + loc) != 0x04034b50) return fail("The %s zip file is damaged", what);
        size_t data = loc + 30u + zrd16(z + loc + 26) + zrd16(z + loc + 28);
        if (data + csize > n) return fail("The %s zip file is damaged", what);
        set_status("Unpacking", name);
        Uint8 *out = malloc(usize ? usize : 1);
        if (!out) return fail("Out of memory%s", "");
        if (method == 0 && csize == usize) memcpy(out, z + data, usize);
        else if (method == 8) {
            z_stream s;
            memset(&s, 0, sizeof s);
            s.next_in = (Bytef *)(z + data);
            s.avail_in = csize;
            s.next_out = out;
            s.avail_out = usize;
            if (inflateInit2(&s, -MAX_WBITS) != Z_OK || inflate(&s, Z_FINISH) != Z_STREAM_END || s.total_out != usize) {
                inflateEnd(&s);
                free(out);
                return fail("Could not unpack %s", name);
            }
            inflateEnd(&s);
        } else { free(out); return fail("Unknown packing in %s", name); }
        char sum[33];
        set_status("Checking", name);
        md5_hex(out, usize, sum);
        if (strcmp(sum, want) != 0) { free(out); return fail("%s does not match its MD5 check (damaged download?)", name); }
        char path[512];
        SDL_snprintf(path, sizeof path, "%s/%s", dir, name);
        bool ok = SDL_SaveFile(path, out, usize);
        free(out);
        if (!ok) return fail("Could not write %s (is the folder writable?)", path);
    }
    return true;
}

/* ---------------------------------------------------------------- the worker */

static int SDLCALL worker(void *unused)
{
    (void)unused;
    bool ok = true;
    Uint8 *z = NULL;
    size_t n = 0;
    SDL_CreateDirectory(dir);
    if (need_pc) {
        set_status("Downloading Street Rod 2 (PC)", URL_PC);
        SDL_LockMutex(lock); overall = 0.0f; SDL_UnlockMutex(lock);
        ok = download(URL_PC, &z, &n) && unzip(z, n, "PC");
        free(z); z = NULL;
    }
    if (ok && need_amiga) {
        set_status("Downloading the Amiga disks", URL_AMIGA);
        SDL_LockMutex(lock); overall = need_pc ? 0.5f : 0.0f; SDL_UnlockMutex(lock);
        if (download(URL_AMIGA, &z, &n) && unzip(z, n, "Amiga"))
            set_status("Amiga disks saved", "The Amiga pictures are made when the game starts");
        else {
            set_status("Amiga disks not downloaded", error_msg);     /* the PC game still runs */
            SDL_LockMutex(lock); error_msg[0] = 0; SDL_UnlockMutex(lock);
            SDL_Delay(2500);
        }
        free(z);
    }
    if (ok) {
        SDL_LockMutex(lock); overall = 1.0f; got = total = 1; SDL_UnlockMutex(lock);
        set_status("All done", "Starting the game...");
        SDL_Delay(900);
    }
    SDL_SetAtomicInt(&done, ok ? 1 : 2);
    return 0;
}

/* ---------------------------------------------------------------- the window */

static void text(SDL_Renderer *r, float x, float y, const char *s, Uint8 cr, Uint8 cg, Uint8 cb)
{
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_RenderDebugText(r, x, y, s);
}

bool installer_run(const char *game_dir, bool want_amiga)
{
    dir = game_dir;
    need_pc = false;
    for (size_t i = 0; i < SDL_arraysize(pc_files); i++) if (!have_file(pc_files[i])) need_pc = true;
    make_gfx = want_amiga && !amiga_gfx_present();
    need_amiga = make_gfx && (!have_file(amiga_files[0]) || !have_file(amiga_files[1]));
    if (!need_pc && !need_amiga) return true;              /* (disks there, no gfx/: the game makes it) */

    if (!SDL_Init(SDL_INIT_VIDEO)) return true;          /* no window: let the game report the missing files */
    SDL_Window *w = NULL;
    SDL_Renderer *r = NULL;
    if (!SDL_CreateWindowAndRenderer("Street Rod 2 - Setup", 640, 360, 0, &w, &r)) return true;
    SDL_SetRenderVSync(r, 1);
    lock = SDL_CreateMutex();
    SDL_SetAtomicInt(&done, 0);
    SDL_SetAtomicInt(&cancel, 0);
    error_msg[0] = 0;
    SDL_Thread *t = SDL_CreateThread(worker, "installer", NULL);
    bool quit = false;
    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) { SDL_SetAtomicInt(&cancel, 1); quit = true; }
            if ((ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) && SDL_GetAtomicInt(&done) == 2)
                quit = true;
        }
        int st = SDL_GetAtomicInt(&done);
        if (st == 1) break;
        char s[160], d[160], e[256], line[96];
        Sint64 g, tt;
        float ov;
        SDL_LockMutex(lock);
        SDL_strlcpy(s, status, sizeof s); SDL_strlcpy(d, detail, sizeof d); SDL_strlcpy(e, error_msg, sizeof e);
        g = got; tt = total; ov = overall;
        SDL_UnlockMutex(lock);

        SDL_SetRenderDrawColor(r, 12, 14, 34, 255);
        SDL_RenderClear(r);
        SDL_SetRenderScale(r, 2, 2);
        text(r, 16, 14, "STREET ROD 2", 255, 90, 200);
        text(r, 16, 28, "First-time setup", 140, 200, 255);
        SDL_SetRenderScale(r, 1, 1);
        text(r, 32, 92, "The game files were not found in the game folder. They are being", 200, 200, 210);
        text(r, 32, 104, "downloaded from Street Rod Online (www.streetrodonline.com).", 200, 200, 210);
        text(r, 32, 140, s, 255, 255, 255);
        char dd[80];
        SDL_snprintf(dd, sizeof dd, "%.76s", d);
        text(r, 32, 154, dd, 150, 150, 170);
        /* the bar: this download's bytes, within its share of the whole job */
        float part = tt > 0 ? (float)g / (float)tt : 0.0f, share = (need_pc && need_amiga) ? 0.5f : 1.0f;
        float frac = ov >= 1.0f ? 1.0f : ov + share * part;
        if (frac > 1) frac = 1;
        SDL_FRect bg = { 32, 180, 576, 22 }, fg = { 34, 182, 572 * frac, 18 };
        SDL_SetRenderDrawColor(r, 60, 60, 80, 255);
        SDL_RenderFillRect(r, &bg);
        SDL_SetRenderDrawColor(r, 255, 80, 190, 255);
        SDL_RenderFillRect(r, &fg);
        if (tt > 0 && ov < 1.0f) SDL_snprintf(line, sizeof line, "%lld KB of %lld KB   (%d%%)", (long long)(g / 1024),
                                              (long long)(tt / 1024), (int)(frac * 100));
        else if (g > 0 && ov < 1.0f) SDL_snprintf(line, sizeof line, "%lld KB", (long long)(g / 1024));
        else SDL_snprintf(line, sizeof line, "%d%%", (int)(frac * 100));
        text(r, 32, 212, line, 200, 200, 210);
        SDL_snprintf(line, sizeof line, "Game folder: %.60s", dir);
        text(r, 32, 236, line, 120, 120, 140);
        if (st == 2) {
            SDL_snprintf(dd, sizeof dd, "%.76s", e);
            text(r, 32, 272, "Setup did not finish:", 255, 110, 110);
            text(r, 32, 286, dd, 255, 170, 170);
            text(r, 32, 312, "Press a key or click to close.", 200, 200, 210);
        } else text(r, 32, 312, "Close this window to cancel.", 120, 120, 140);
        SDL_RenderPresent(r);
    }
    SDL_WaitThread(t, NULL);
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(w);
    SDL_DestroyMutex(lock);
    return SDL_GetAtomicInt(&done) == 1;
}
