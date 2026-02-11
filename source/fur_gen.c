/*
fur_gen.c - Generate binary Furnace Tracker .fur files from sliced WAV files.

Reads WAV slices from an input directory and produces a .fur file compatible
with Furnace 0.6.8.1 (version 228). Creates one instrument per sample, each
with its own sample map, plus pattern data on a Generic PCM DAC channel.
Individual instruments keep playing through pause unlike drum kit instruments.

Usage: ./fur_gen <input_dir> <bpm> <rows_per_beat> <pattern_rows> <output_file>

Requires: zlib (link with -lz)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdint.h>
#include <zlib.h>

#define MAX_SAMPLES    120   /* Max samples mappable in Furnace sample map */
#define WAV_HEADER_MIN 44
#define SM_ENTRIES     120   /* Fixed entry count in instrument sample map */
#define FURNACE_VER    228

/* ---------- WAV sample data ---------- */

typedef struct {
    char filename[256];
    char name[256];
    unsigned char *pcm;
    long pcm_len;       /* raw PCM byte count */
    long n_samples;     /* audio sample count (per channel) */
    int channels;
    int sample_rate;
    int bit_depth;
} SampleData;

/* ---------- Dynamic buffer ---------- */

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} Buffer;

static void buf_init(Buffer *b) {
    b->cap = 4 * 1024 * 1024;
    b->data = malloc(b->cap);
    b->len = 0;
    if (!b->data) { fprintf(stderr, "Fatal: initial buffer alloc failed\n"); exit(1); }
}

static void buf_ensure(Buffer *b, size_t extra) {
    while (b->len + extra > b->cap) {
        b->cap *= 2;
        unsigned char *tmp = realloc(b->data, b->cap);
        if (!tmp) { fprintf(stderr, "Fatal: buffer realloc failed\n"); exit(1); }
        b->data = tmp;
    }
}

static void buf_write(Buffer *b, const void *src, size_t n) {
    buf_ensure(b, n);
    memcpy(b->data + b->len, src, n);
    b->len += n;
}

static void buf_u8(Buffer *b, uint8_t v) { buf_write(b, &v, 1); }

static void buf_u16le(Buffer *b, uint16_t v) {
    uint8_t t[2] = { v & 0xFF, (v >> 8) & 0xFF };
    buf_write(b, t, 2);
}

static void buf_u32le(Buffer *b, uint32_t v) {
    uint8_t t[4] = { v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF };
    buf_write(b, t, 4);
}

static void buf_i32le(Buffer *b, int32_t v) { buf_u32le(b, (uint32_t)v); }

static void buf_float_le(Buffer *b, float v) {
    /* Assumes host is little-endian (x86/ARM) */
    buf_write(b, &v, 4);
}

static void buf_zeros(Buffer *b, size_t n) {
    buf_ensure(b, n);
    memset(b->data + b->len, 0, n);
    b->len += n;
}

static void buf_fill(Buffer *b, uint8_t v, size_t n) {
    buf_ensure(b, n);
    memset(b->data + b->len, v, n);
    b->len += n;
}

static void buf_str(Buffer *b, const char *s) {
    buf_write(b, s, strlen(s) + 1);  /* include null terminator */
}

static void buf_tag(Buffer *b, const char *tag) {
    buf_write(b, tag, 4);  /* 4-byte block tag, no null */
}

/* Patch a u32 LE value at a previously-written offset */
static void buf_patch_u32(Buffer *b, size_t off, uint32_t v) {
    b->data[off]   =  v        & 0xFF;
    b->data[off+1] = (v >>  8) & 0xFF;
    b->data[off+2] = (v >> 16) & 0xFF;
    b->data[off+3] = (v >> 24) & 0xFF;
}

static void buf_patch_u16(Buffer *b, size_t off, uint16_t v) {
    b->data[off]   =  v       & 0xFF;
    b->data[off+1] = (v >> 8) & 0xFF;
}

static void buf_free(Buffer *b) { free(b->data); b->data = NULL; b->len = b->cap = 0; }

/* ---------- WAV reading ---------- */

static unsigned int  rd16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned long rd32(const unsigned char *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned long)p[3] << 24);
}

static int cmp_samples(const void *a, const void *b) {
    return strcmp(((const SampleData *)a)->filename, ((const SampleData *)b)->filename);
}

static int read_wav(const char *path, SampleData *out) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "Error: Cannot open '%s': %s\n", path, strerror(errno)); return -1; }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize < WAV_HEADER_MIN) {
        fprintf(stderr, "Error: '%s' too small for WAV.\n", path);
        fclose(fp); return -1;
    }

    unsigned char *fd = malloc(fsize);
    if (!fd) { fprintf(stderr, "Error: alloc failed for '%s'.\n", path); fclose(fp); return -1; }
    if ((long)fread(fd, 1, fsize, fp) != fsize) {
        fprintf(stderr, "Error: read failed '%s'.\n", path);
        free(fd); fclose(fp); return -1;
    }
    fclose(fp);

    if (memcmp(fd, "RIFF", 4) || memcmp(fd + 8, "WAVE", 4)) {
        fprintf(stderr, "Error: '%s' not a valid WAV.\n", path);
        free(fd); return -1;
    }

    int fmt_ok = 0, chans = 0, rate = 0, bits = 0;
    unsigned char *pcm_src = NULL;
    long pcm_len = 0;
    long off = 12;

    while (off + 8 <= fsize) {
        unsigned long csz = rd32(fd + off + 4);
        if (!memcmp(fd + off, "fmt ", 4)) {
            if (off + 8 + csz > (unsigned long)fsize || csz < 16) {
                fprintf(stderr, "Error: bad fmt in '%s'.\n", path);
                free(fd); return -1;
            }
            if (rd16(fd + off + 8) != 1) {
                fprintf(stderr, "Error: '%s' not PCM.\n", path);
                free(fd); return -1;
            }
            chans = rd16(fd + off + 10);
            rate  = (int)rd32(fd + off + 12);
            bits  = rd16(fd + off + 22);
            fmt_ok = 1;
        } else if (!memcmp(fd + off, "data", 4)) {
            pcm_len = (long)csz;
            if (off + 8 + pcm_len > fsize) pcm_len = fsize - off - 8;
            pcm_src = fd + off + 8;
        }
        off += 8 + csz;
        if (csz & 1) off++;
    }

    if (!fmt_ok || !pcm_src || pcm_len <= 0) {
        fprintf(stderr, "Error: missing fmt/data in '%s'.\n", path);
        free(fd); return -1;
    }
    if (chans != 1) {
        fprintf(stderr, "Error: '%s' is not mono (%d channels). Furnace PCM DAC requires mono.\n", path, chans);
        free(fd); return -1;
    }
    if (bits != 8 && bits != 16) {
        fprintf(stderr, "Error: '%s' has unsupported bit depth %d (need 8 or 16).\n", path, bits);
        free(fd); return -1;
    }

    out->pcm = malloc(pcm_len);
    if (!out->pcm) { fprintf(stderr, "Error: PCM alloc failed.\n"); free(fd); return -1; }
    memcpy(out->pcm, pcm_src, pcm_len);
    out->pcm_len    = pcm_len;
    out->channels   = chans;
    out->n_samples  = pcm_len / (bits / 8);
    out->sample_rate = rate;
    out->bit_depth  = bits;

    free(fd);
    return 0;
}

/* ---------- Post-order template (260 bytes) ----------
   Extracted from a reference bass.fur (Furnace 0.6.8.1, Generic PCM DAC).
   Contains effect-column counts, speed flags, chip config, system name,
   and ADIR directory pointers.
   Variable fields patched at runtime:
     +0x26  u16 virtual-tempo numerator
     +0x28  u16 virtual-tempo denominator
     +0xF8  u32 ADIR[0] pointer  (instruments)
     +0xFC  u32 ADIR[1] pointer  (wavetables)
     +0x100 u32 ADIR[2] pointer  (samples)
*/
static const unsigned char POST_ORDER[260] = {
    0x01,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x3F,0x00,0x00,0x00,0x00,0x00,0x01,
    0x01,0x00,0x00,0x01,0x00,0x00,0x01,0x04,0x00,0x00,0x01,0x01,0x00,0x00,0x00,0x00,
    0x02,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x47,0x65,0x6E,0x65,0x72,0x69,0x63,0x20,0x50,0x43,0x4D,0x20,0x44,0x41,0x43,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x3F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x22,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x00,0x00,0xD0,
    0xFF,0x01,0x00,0xD0,0xFF,0x02,0x00,0xD0,0xFF,0x03,0x00,0xD0,0xFF,0x04,0x00,0xD0,
    0xFF,0x05,0x00,0xD0,0xFF,0x06,0x00,0xD0,0xFF,0x07,0x00,0xD0,0xFF,0x08,0x00,0xD0,
    0xFF,0x09,0x00,0xD0,0xFF,0x0A,0x00,0xD0,0xFF,0x0B,0x00,0xD0,0xFF,0x0C,0x00,0xD0,
    0xFF,0x0D,0x00,0xD0,0xFF,0x0E,0x00,0xD0,0xFF,0x0F,0x00,0xD0,0xFF,0x00,0x00,0xE0,
    0xFF,0x01,0x00,0xE0,0xFF,0x02,0x00,0xE0,0xFF,0x03,0x00,0xE0,0xFF,0x04,0x00,0xE0,
    0xFF,0x05,0x00,0xE0,0xFF,0x06,0x00,0xE0,0xFF,0x07,0x00,0xE0,0xFF,0x08,0x00,0xE0,
    0xFF,0x09,0x00,0xE0,0xFF,0x0A,0x00,0xE0,0xFF,0x0B,0x00,0xE0,0xFF,0x0C,0x00,0xE0,
    0xFF,0x0D,0x00,0xE0,0xFF,0x0E,0x00,0xE0,0xFF,0x0F,0x00,0xE0,0xFF,0x01,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x04,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,
    0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00
};

/* Config flags at INFO payload offset 0xFC-0x111 (22 bytes) */
static const unsigned char CONFIG_FLAGS[22] = {
    0xDC,0x43,0x00,0x02,0x02,0x01,0x00,0x00,0x00,0x00,0x01,0x01,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01
};

/* ---------- Block writers ---------- */

/* Write the INFO block.  Returns with pointer-table and ADIR-pointer
   slots filled with zeros; caller patches them afterwards. */
static void write_info(Buffer *b, int n, int speed, int pattern_rows,
                       uint16_t vt_num, uint16_t vt_den,
                       size_t *ptr_table_off,   /* out: offset of INS2 pointer slot */
                       size_t *post_order_off)   /* out: offset of post-order section */
{
    buf_tag(b, "INFO");
    size_t size_slot = b->len;
    buf_u32le(b, 0);           /* placeholder for block size */
    size_t payload_start = b->len;

    /* --- Head section (274 bytes, offsets 0x00-0x111) --- */
    /* +0x00 */ buf_u8(b, 0);                    /* timeBase */
    /* +0x01 */ buf_u8(b, (uint8_t)speed);       /* speed1 */
    /* +0x02 */ buf_u8(b, (uint8_t)speed);       /* speed2 */
    /* +0x03 */ buf_u8(b, 1);                    /* arpSpeed */
    /* +0x04 */ buf_float_le(b, 60.0f);          /* ticksPerSec */
    /* +0x08 */ buf_u16le(b, (uint16_t)pattern_rows); /* patternLen */
    /* +0x0A */ buf_u16le(b, (uint16_t)n);       /* ordersLen */
    /* +0x0C */ buf_u8(b, 4);                    /* highlight_a */
    /* +0x0D */ buf_u8(b, 16);                   /* highlight_b */
    /* +0x0E */ buf_u16le(b, (uint16_t)n);       /* insCount */
    /* +0x10 */ buf_u16le(b, 0);                 /* wavCount */
    /* +0x12 */ buf_u16le(b, (uint16_t)n);       /* smpCount */
    /* +0x14 */ buf_u16le(b, (uint16_t)n);       /* patCount */
    /* +0x16 */ buf_u16le(b, 0);                 /* reserved/channels */
    /* +0x18 */ buf_u8(b, 0xC0);                 /* system[0] = Generic PCM DAC */
    /* +0x19 */ buf_zeros(b, 31);                /* systems 1-31 */
    /* +0x38 */ buf_fill(b, 0x40, 32);           /* volumes (32 slots) */
    /* +0x58 */ buf_zeros(b, 32);                /* pannings */
    /* +0x78 */ buf_zeros(b, 132);               /* reserved/flags */
    /* +0xFC */ buf_write(b, CONFIG_FLAGS, 22);  /* config flags */
    /* now at +0x112 = 274 bytes into payload */

    /* --- Pointer table --- */
    *ptr_table_off = b->len;
    for (int i = 0; i < n; i++)
        buf_u32le(b, 0);                 /* INS2[i] pointer placeholders */
    for (int i = 0; i < n; i++)
        buf_u32le(b, 0);                 /* SMP2[i] pointer placeholders */
    for (int i = 0; i < n; i++)
        buf_u32le(b, 0);                 /* PATN[i] pointer placeholders */

    /* --- Order table --- */
    for (int i = 0; i < n; i++)
        buf_u8(b, (uint8_t)i);

    /* --- Post-order section (260 bytes) --- */
    *post_order_off = b->len;
    buf_write(b, POST_ORDER, 260);

    /* Patch virtual tempo in post-order */
    buf_patch_u16(b, *post_order_off + 0x26, vt_num);
    buf_patch_u16(b, *post_order_off + 0x28, vt_den);

    /* Patch INFO block size */
    uint32_t info_size = (uint32_t)(b->len - payload_start);
    buf_patch_u32(b, size_slot, info_size);
}

/* ADIR block for instruments (N instruments in one group) */
static void write_adir_ins(Buffer *b, int n) {
    uint32_t bsz = (uint32_t)(n + 7);
    buf_tag(b, "ADIR");
    buf_u32le(b, bsz);
    buf_u32le(b, 1);       /* numGroups */
    buf_u8(b, 0);          /* start */
    buf_u8(b, (uint8_t)n); /* count */
    buf_u16le(b, 0);       /* padding */
    for (int i = 1; i < n; i++)
        buf_u8(b, (uint8_t)i);  /* group member indices */
}

/* ADIR block for wavetables (0 wavetables) */
static void write_adir_wav(Buffer *b) {
    buf_tag(b, "ADIR");
    buf_u32le(b, 4);       /* block size */
    buf_u32le(b, 0);       /* numGroups */
}

/* ADIR block for samples (N samples in one group) */
static void write_adir_smp(Buffer *b, int n) {
    uint32_t bsz = (uint32_t)(n + 7);
    buf_tag(b, "ADIR");
    buf_u32le(b, bsz);
    buf_u32le(b, 1);       /* numGroups */
    buf_u8(b, 0);          /* start */
    buf_u8(b, (uint8_t)n); /* count */
    buf_u16le(b, 0);       /* padding */
    for (int i = 1; i < n; i++)
        buf_u8(b, (uint8_t)i);  /* group member indices */
}

/* INS2 block: single-sample instrument with sample map */
static void write_ins2(Buffer *b, const char *inst_name, int sample_index) {
    buf_tag(b, "INS2");
    size_t size_slot = b->len;
    buf_u32le(b, 0);  /* placeholder */
    size_t payload_start = b->len;

    buf_u16le(b, FURNACE_VER);   /* version */
    buf_u16le(b, 4);             /* type = sample */

    /* NA sub-block: instrument name */
    size_t name_len = strlen(inst_name);
    buf_write(b, "NA", 2);
    buf_u16le(b, (uint16_t)(name_len + 1));
    buf_str(b, inst_name);

    /* SM sub-block: sample map (120 entries, all mapped to same sample) */
    buf_write(b, "SM", 2);
    buf_u16le(b, 484);          /* fixed size: 4 header + 120*4 entries */
    /* SM header */
    buf_u8(b, 0x00);
    buf_u8(b, 0x00);
    buf_u8(b, 0x01);
    buf_u8(b, 0x1F);
    /* 120 entries: all mapped to the same sample */
    for (int i = 0; i < SM_ENTRIES; i++) {
        buf_u16le(b, 48);       /* note = C-4 (play at natural pitch) */
        buf_u16le(b, (uint16_t)sample_index);
    }

    /* NE sub-block: note/envelope data (120 entries) */
    buf_write(b, "NE", 2);
    buf_u16le(b, 241);          /* 1 + 120*2 */
    buf_u8(b, 0x01);            /* enabled flag */
    for (int i = 0; i < 120; i++) {
        buf_u8(b, 0x0F);
        buf_u8(b, 0xFF);
    }

    /* EN marker: end of instrument */
    buf_write(b, "EN", 2);

    /* Patch block size */
    buf_patch_u32(b, size_slot, (uint32_t)(b->len - payload_start));
}

/* SMP2 block: one sample */
static void write_smp2(Buffer *b, const SampleData *s) {
    buf_tag(b, "SMP2");
    size_t size_slot = b->len;
    buf_u32le(b, 0);  /* placeholder */
    size_t payload_start = b->len;

    buf_str(b, s->name);                        /* name + null */
    buf_u32le(b, (uint32_t)s->n_samples);       /* sample count */
    buf_u32le(b, (uint32_t)s->sample_rate);     /* compatRate */
    buf_u32le(b, (uint32_t)s->sample_rate);     /* c4Rate */
    buf_u8(b, (uint8_t)s->bit_depth);           /* depth */
    buf_u8(b, 0);                               /* loopMode = none */
    buf_u8(b, 1);                               /* brrEmphasis = yes */
    buf_u8(b, 0);                               /* dpcmMode = off */
    buf_i32le(b, -1);                           /* loopStart */
    buf_i32le(b, -1);                           /* loopEnd */
    buf_fill(b, 0xFF, 16);                      /* extra reserved fields */
    buf_write(b, s->pcm, s->pcm_len);           /* raw PCM data */

    buf_patch_u32(b, size_slot, (uint32_t)(b->len - payload_start));
}

/* PATN block: one pattern (single note trigger on row 0) */
static void write_patn(Buffer *b, int index) {
    buf_tag(b, "PATN");
    buf_u32le(b, 9);        /* block payload = 9 bytes */
    buf_u8(b, 0);           /* subsong */
    buf_u8(b, 0);           /* channel */
    buf_u16le(b, (uint16_t)index); /* patIndex */
    /* Compressed row data: */
    buf_u8(b, 0);           /* row 0 */
    buf_u8(b, 0x03);        /* field mask: note + instrument */
    buf_u8(b, 60);          /* note value (C-0 = 60) */
    buf_u8(b, (uint8_t)index); /* instrument index */
    buf_u8(b, 0xFF);        /* end marker */
}

/* ---------- Main ---------- */

int main(int argc, char *argv[]) {
    if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        printf("Usage: ./fur_gen <input_dir> <bpm> <rows_per_beat> <pattern_rows>"
               " <output_file>\n\n"
               "Generates a binary Furnace .fur file from sliced WAV files.\n"
               "Each WAV becomes its own instrument (persists through pause).\n");
        return 0;
    }
    if (argc < 6) {
        fprintf(stderr, "Error: Insufficient arguments.\n"
                "Usage: ./fur_gen <input_dir> <bpm> <rows_per_beat> <pattern_rows>"
                " <output_file>\n");
        return 1;
    }

    const char *input_dir  = argv[1];
    const char *output_file = argv[5];

    /* Parse numeric args */
    char *endptr;
    errno = 0;
    double bpm = strtod(argv[2], &endptr);
    if (*endptr || errno || bpm <= 0) {
        fprintf(stderr, "Error: BPM must be positive, got '%s'.\n", argv[2]);
        return 1;
    }
    errno = 0;
    long rows_per_beat = strtol(argv[3], &endptr, 10);
    if (*endptr || errno || rows_per_beat <= 0) {
        fprintf(stderr, "Error: rows_per_beat must be positive integer, got '%s'.\n", argv[3]);
        return 1;
    }
    errno = 0;
    long pattern_rows = strtol(argv[4], &endptr, 10);
    if (*endptr || errno || pattern_rows <= 0) {
        fprintf(stderr, "Error: pattern_rows must be positive integer, got '%s'.\n", argv[4]);
        return 1;
    }

    /* Scan input directory */
    DIR *dir = opendir(input_dir);
    if (!dir) {
        fprintf(stderr, "Error: Cannot open '%s': %s\n", input_dir, strerror(errno));
        return 1;
    }

    SampleData samples[MAX_SAMPLES];
    int n = 0;
    struct dirent *ent;

    while ((ent = readdir(dir)) != NULL) {
        if (n >= MAX_SAMPLES) {
            fprintf(stderr, "Warning: Max %d samples reached, skipping rest.\n", MAX_SAMPLES);
            break;
        }
        const char *fn = ent->d_name;
        size_t flen = strlen(fn);
        if (flen < 5) continue;
        if (strcasecmp(fn + flen - 4, ".wav") != 0) continue;

        strncpy(samples[n].filename, fn, sizeof(samples[n].filename) - 1);
        samples[n].filename[sizeof(samples[n].filename) - 1] = '\0';
        strncpy(samples[n].name, fn, sizeof(samples[n].name) - 1);
        samples[n].name[sizeof(samples[n].name) - 1] = '\0';
        char *dot = strrchr(samples[n].name, '.');
        if (dot) *dot = '\0';
        samples[n].pcm = NULL;
        n++;
    }
    closedir(dir);

    if (n == 0) {
        fprintf(stderr, "Error: No .wav files found in '%s'.\n", input_dir);
        return 1;
    }

    qsort(samples, n, sizeof(SampleData), cmp_samples);

    /* Read WAV data */
    printf("Reading %d WAV files from '%s'...\n", n, input_dir);
    for (int i = 0; i < n; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", input_dir, samples[i].filename);
        if (read_wav(path, &samples[i]) != 0) {
            for (int j = 0; j < i; j++) free(samples[j].pcm);
            return 1;
        }
        printf("  [%02X] %s (%ld samples, %d Hz, %d-bit)\n",
               i, samples[i].filename, samples[i].n_samples,
               samples[i].sample_rate, samples[i].bit_depth);
    }

    /* Calculate tempo */
    int speed = (int)rows_per_beat;
    int tick_rate = 60;
    double base_bpm = (double)(tick_rate * 60) / (double)(speed * rows_per_beat);
    uint16_t vt_num = (uint16_t)(int)bpm;
    uint16_t vt_den = (uint16_t)(int)round(base_bpm);

    printf("Virtual tempo: %d/%d (BPM=%.1f)\n", vt_num, vt_den, bpm);

    /* ---- Build decompressed .fur data ---- */
    Buffer buf;
    buf_init(&buf);

    /* File header (24 bytes) */
    buf_write(&buf, "-Furnace module-", 16);
    buf_u16le(&buf, FURNACE_VER);    /* version */
    buf_u16le(&buf, 0);              /* reserved */
    buf_u32le(&buf, 32);             /* song info pointer */

    /* 8 bytes padding */
    buf_zeros(&buf, 8);

    /* INFO block */
    size_t ptr_table_off, post_order_off;
    write_info(&buf, n, speed, (int)pattern_rows, vt_num, vt_den,
               &ptr_table_off, &post_order_off);

    /* ADIR blocks */
    size_t adir0_off = buf.len;
    write_adir_ins(&buf, n);
    size_t adir1_off = buf.len;
    write_adir_wav(&buf);
    size_t adir2_off = buf.len;
    write_adir_smp(&buf, n);

    /* Patch ADIR pointers in post-order section */
    buf_patch_u32(&buf, post_order_off + 0xF8,  (uint32_t)adir0_off);
    buf_patch_u32(&buf, post_order_off + 0xFC,  (uint32_t)adir1_off);
    buf_patch_u32(&buf, post_order_off + 0x100, (uint32_t)adir2_off);

    /* INS2 blocks (one per sample) */
    printf("Writing %d instruments...\n", n);
    for (int i = 0; i < n; i++) {
        size_t ins_off = buf.len;
        write_ins2(&buf, samples[i].name, i);
        buf_patch_u32(&buf, ptr_table_off + (size_t)i * 4, (uint32_t)ins_off);
    }

    /* SMP2 blocks */
    printf("Writing %d samples...\n", n);
    for (int i = 0; i < n; i++) {
        size_t smp_off = buf.len;
        write_smp2(&buf, &samples[i]);
        buf_patch_u32(&buf, ptr_table_off + (size_t)n * 4 + (size_t)i * 4, (uint32_t)smp_off);
        printf("  Sample %d/%d written (%ld bytes).\n", i + 1, n, samples[i].pcm_len);
    }

    /* PATN blocks */
    for (int i = 0; i < n; i++) {
        size_t patn_off = buf.len;
        write_patn(&buf, i);
        buf_patch_u32(&buf, ptr_table_off + (size_t)n * 2 * 4 + (size_t)i * 4,
                      (uint32_t)patn_off);
    }

    printf("Uncompressed size: %zu bytes\n", buf.len);

    /* ---- zlib compress ---- */
    uLong comp_bound = compressBound((uLong)buf.len);
    unsigned char *comp = malloc(comp_bound);
    if (!comp) {
        fprintf(stderr, "Error: compress buffer alloc failed.\n");
        buf_free(&buf);
        for (int i = 0; i < n; i++) free(samples[i].pcm);
        return 1;
    }

    uLongf comp_len = comp_bound;
    int zret = compress2(comp, &comp_len, buf.data, (uLong)buf.len, Z_DEFAULT_COMPRESSION);
    if (zret != Z_OK) {
        fprintf(stderr, "Error: zlib compress failed (code %d).\n", zret);
        free(comp);
        buf_free(&buf);
        for (int i = 0; i < n; i++) free(samples[i].pcm);
        return 1;
    }

    printf("Compressed size: %lu bytes\n", (unsigned long)comp_len);

    /* ---- Write output file ---- */
    FILE *fp = fopen(output_file, "wb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot create '%s': %s\n", output_file, strerror(errno));
        free(comp);
        buf_free(&buf);
        for (int i = 0; i < n; i++) free(samples[i].pcm);
        return 1;
    }

    if (fwrite(comp, 1, comp_len, fp) != comp_len) {
        fprintf(stderr, "Error: Write failed.\n");
        fclose(fp);
        free(comp);
        buf_free(&buf);
        for (int i = 0; i < n; i++) free(samples[i].pcm);
        return 1;
    }
    fclose(fp);

    /* Cleanup */
    free(comp);
    buf_free(&buf);
    for (int i = 0; i < n; i++) free(samples[i].pcm);

    printf("Furnace .fur file written to: %s\n", output_file);
    printf("  %d instruments, %d samples, %d orders, speed=%d, virtual tempo=%d/%d\n",
           n, n, n, speed, vt_num, vt_den);

    return 0;
}
