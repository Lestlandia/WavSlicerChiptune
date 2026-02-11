/*
furnace_gen.c - Generate Furnace Tracker text export from sliced WAV files.

Reads WAV slices from an input directory and produces a .txt file in the
exact format that Furnace 0.6.8.1 exports, including sample hex dumps,
orders, and pattern data on a Generic PCM DAC channel.

Usage: ./furnace_gen <input_dir> <bpm> <rows_per_beat> <pattern_rows> <output_file> [instrument_name]
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#define MAX_SAMPLES 256
#define WAV_HEADER_MIN 44

static const char *NOTE_NAMES[] = {
    "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"
};

// WAV PCM data extracted from a file
typedef struct {
    char filename[256];    // just the filename (e.g. "00.wav")
    char name[256];        // filename without extension (e.g. "00")
    unsigned char *pcm;    // raw PCM data bytes
    long pcm_len;          // length of PCM data in bytes
    long n_samples;        // number of audio samples (pcm_len / (bit_depth/8))
    int sample_rate;
    int bit_depth;
} SampleData;

// Compare function for sorting filenames alphabetically
static int cmp_samples(const void *a, const void *b) {
    const SampleData *sa = (const SampleData *)a;
    const SampleData *sb = (const SampleData *)b;
    return strcmp(sa->filename, sb->filename);
}

// Read a little-endian u16 from a buffer
static unsigned int read_u16_le(const unsigned char *buf) {
    return buf[0] | (buf[1] << 8);
}

// Read a little-endian u32 from a buffer
static unsigned long read_u32_le(const unsigned char *buf) {
    return buf[0] | (buf[1] << 8) | (buf[2] << 16) | ((unsigned long)buf[3] << 24);
}

// Read a WAV file, extract raw PCM data. Returns 0 on success.
static int read_wav(const char *filepath, SampleData *out) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open '%s': %s\n", filepath, strerror(errno));
        return -1;
    }

    // Read entire file
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size < WAV_HEADER_MIN) {
        fprintf(stderr, "Error: '%s' is too small to be a WAV file.\n", filepath);
        fclose(fp);
        return -1;
    }

    unsigned char *file_data = malloc(file_size);
    if (!file_data) {
        fprintf(stderr, "Error: Memory allocation failed for '%s'.\n", filepath);
        fclose(fp);
        return -1;
    }

    if (fread(file_data, 1, file_size, fp) != (size_t)file_size) {
        fprintf(stderr, "Error: Failed to read '%s'.\n", filepath);
        free(file_data);
        fclose(fp);
        return -1;
    }
    fclose(fp);

    // Validate RIFF/WAVE header
    if (memcmp(file_data, "RIFF", 4) != 0 || memcmp(file_data + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "Error: '%s' is not a valid WAV file.\n", filepath);
        free(file_data);
        return -1;
    }

    // Find fmt and data chunks
    int fmt_found = 0;
    int channels = 0, sample_rate = 0, bits_per_sample = 0;
    unsigned char *pcm_data = NULL;
    long pcm_len = 0;

    long offset = 12; // skip RIFF header
    while (offset + 8 <= file_size) {
        unsigned long chunk_size = read_u32_le(file_data + offset + 4);

        if (memcmp(file_data + offset, "fmt ", 4) == 0) {
            if (offset + 8 + chunk_size > (unsigned long)file_size || chunk_size < 16) {
                fprintf(stderr, "Error: Invalid fmt chunk in '%s'.\n", filepath);
                free(file_data);
                return -1;
            }
            unsigned int audio_format = read_u16_le(file_data + offset + 8);
            if (audio_format != 1) { // PCM
                fprintf(stderr, "Error: '%s' is not PCM format (format=%u).\n", filepath, audio_format);
                free(file_data);
                return -1;
            }
            channels = read_u16_le(file_data + offset + 10);
            sample_rate = (int)read_u32_le(file_data + offset + 12);
            bits_per_sample = read_u16_le(file_data + offset + 22);
            fmt_found = 1;
        } else if (memcmp(file_data + offset, "data", 4) == 0) {
            pcm_len = (long)chunk_size;
            if (offset + 8 + pcm_len > file_size) {
                pcm_len = file_size - offset - 8;
            }
            pcm_data = file_data + offset + 8;
        }

        offset += 8 + chunk_size;
        // Chunks are word-aligned
        if (chunk_size % 2 != 0) offset++;
    }

    if (!fmt_found || !pcm_data || pcm_len <= 0) {
        fprintf(stderr, "Error: Could not find fmt/data chunks in '%s'.\n", filepath);
        free(file_data);
        return -1;
    }

    // Copy PCM data to output (file_data will be freed)
    out->pcm = malloc(pcm_len);
    if (!out->pcm) {
        fprintf(stderr, "Error: Memory allocation failed for PCM data.\n");
        free(file_data);
        return -1;
    }
    memcpy(out->pcm, pcm_data, pcm_len);
    out->pcm_len = pcm_len;
    out->n_samples = pcm_len / (bits_per_sample / 8) / channels;
    out->sample_rate = sample_rate;
    out->bit_depth = bits_per_sample;

    free(file_data);
    return 0;
}

// Write the hex dump of PCM data in Furnace text export format
static void write_hex_dump(FILE *fp, const unsigned char *data, long len) {
    for (long offset = 0; offset < len; offset += 16) {
        fprintf(fp, "%08lX:", offset);
        long remaining = len - offset;
        long count = remaining < 16 ? remaining : 16;
        for (long i = 0; i < count; i++) {
            fprintf(fp, " %02X", data[offset + i]);
        }
        fputc('\n', fp);
    }
}

// Get note name for a sample index (0=C-0, 1=C#0, 2=D-0, ...)
static void index_to_note(int index, char *buf, size_t buf_size) {
    int octave = index / 12;
    int note = index % 12;
    snprintf(buf, buf_size, "%s%d", NOTE_NAMES[note], octave);
}

int main(int argc, char *argv[]) {
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        printf("Usage: ./furnace_gen <input_dir> <bpm> <rows_per_beat> <pattern_rows> <output_file> [instrument_name]\n");
        printf("\nGenerates a Furnace Tracker text export from sliced WAV files.\n");
        return 0;
    }

    if (argc < 6) {
        fprintf(stderr, "Error: Insufficient arguments.\n");
        fprintf(stderr, "Usage: ./furnace_gen <input_dir> <bpm> <rows_per_beat> <pattern_rows> <output_file> [instrument_name]\n");
        return 1;
    }

    const char *input_dir = argv[1];
    const char *output_file = argv[5];
    const char *instrument_name = (argc > 6) ? argv[6] : "Sample Kit";

    // Validate numeric arguments
    char *endptr;
    errno = 0;
    double bpm = strtod(argv[2], &endptr);
    if (*endptr != '\0' || errno != 0 || bpm <= 0) {
        fprintf(stderr, "Error: BPM must be a positive number, got '%s'.\n", argv[2]);
        return 1;
    }

    errno = 0;
    long rows_per_beat = strtol(argv[3], &endptr, 10);
    if (*endptr != '\0' || errno != 0 || rows_per_beat <= 0) {
        fprintf(stderr, "Error: rows_per_beat must be a positive integer, got '%s'.\n", argv[3]);
        return 1;
    }

    errno = 0;
    long pattern_rows = strtol(argv[4], &endptr, 10);
    if (*endptr != '\0' || errno != 0 || pattern_rows <= 0) {
        fprintf(stderr, "Error: pattern_rows must be a positive integer, got '%s'.\n", argv[4]);
        return 1;
    }

    // Scan input directory for .wav files
    DIR *dir = opendir(input_dir);
    if (!dir) {
        fprintf(stderr, "Error: Cannot open directory '%s': %s\n", input_dir, strerror(errno));
        return 1;
    }

    SampleData samples[MAX_SAMPLES];
    int n_samples = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (n_samples >= MAX_SAMPLES) {
            fprintf(stderr, "Warning: Maximum %d samples reached, skipping remaining files.\n", MAX_SAMPLES);
            break;
        }

        const char *name = entry->d_name;
        size_t len = strlen(name);
        if (len < 5) continue;
        // Check for .wav extension (case-insensitive)
        if (strcasecmp(name + len - 4, ".wav") != 0) continue;

        strncpy(samples[n_samples].filename, name, sizeof(samples[n_samples].filename) - 1);
        samples[n_samples].filename[sizeof(samples[n_samples].filename) - 1] = '\0';

        // Strip extension for sample name
        strncpy(samples[n_samples].name, name, sizeof(samples[n_samples].name) - 1);
        samples[n_samples].name[sizeof(samples[n_samples].name) - 1] = '\0';
        char *dot = strrchr(samples[n_samples].name, '.');
        if (dot) *dot = '\0';

        samples[n_samples].pcm = NULL;
        n_samples++;
    }
    closedir(dir);

    if (n_samples == 0) {
        fprintf(stderr, "Error: No .wav files found in '%s'.\n", input_dir);
        return 1;
    }

    // Sort alphabetically
    qsort(samples, n_samples, sizeof(SampleData), cmp_samples);

    // Read WAV data for each sample
    printf("Reading %d WAV files from '%s'...\n", n_samples, input_dir);
    for (int i = 0; i < n_samples; i++) {
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", input_dir, samples[i].filename);

        if (read_wav(filepath, &samples[i]) != 0) {
            // Cleanup already-loaded samples
            for (int j = 0; j < i; j++) free(samples[j].pcm);
            return 1;
        }
        printf("  [%02X] %s (%ld samples, %d Hz, %d-bit)\n",
               i, samples[i].filename, samples[i].n_samples,
               samples[i].sample_rate, samples[i].bit_depth);
    }

    // Calculate virtual tempo
    long speed = rows_per_beat;
    int tick_rate = 60;
    double base_bpm = (double)(tick_rate * 60) / (double)(speed * rows_per_beat);
    int vt_num = (int)bpm;
    int vt_den = (int)round(base_bpm);

    // Open output file
    FILE *fp = fopen(output_file, "w");
    if (!fp) {
        fprintf(stderr, "Error: Cannot create '%s': %s\n", output_file, strerror(errno));
        for (int i = 0; i < n_samples; i++) free(samples[i].pcm);
        return 1;
    }

    printf("Generating Furnace text export...\n");

    // --- Header ---
    fprintf(fp, "# Furnace Text Export\n\n");
    fprintf(fp, "generated by Furnace 0.6.8.1 (228)\n\n");

    // --- Song Information ---
    fprintf(fp, "# Song Information\n\n");
    fprintf(fp, "- name: \n");
    fprintf(fp, "- author: \n");
    fprintf(fp, "- album: \n");
    fprintf(fp, "- system: Generic PCM DAC\n");
    fprintf(fp, "- tuning: 440\n\n");
    fprintf(fp, "- instruments: 1\n");
    fprintf(fp, "- wavetables: 0\n");
    fprintf(fp, "- samples: %d\n\n", n_samples);

    // --- Sound Chips ---
    fprintf(fp, "# Sound Chips\n\n");
    fprintf(fp, "- Generic PCM DAC\n");
    fprintf(fp, "  - id: 56\n");
    fprintf(fp, "  - volume: 1\n");
    fprintf(fp, "  - panning: 0\n");
    fprintf(fp, "  - front/rear: 0\n\n");

    // --- Instruments ---
    fprintf(fp, "# Instruments\n\n");
    fprintf(fp, "## 00: %s\n\n", instrument_name);
    fprintf(fp, "- type: 4\n\n\n");

    // --- Wavetables ---
    fprintf(fp, "# Wavetables\n\n\n");

    // --- Samples ---
    fprintf(fp, "# Samples\n\n");
    for (int i = 0; i < n_samples; i++) {
        fprintf(fp, "## %02X: %s\n\n", i, samples[i].name);
        fprintf(fp, "- format: %d\n", samples[i].bit_depth);
        fprintf(fp, "- data length: %ld\n", samples[i].pcm_len);
        fprintf(fp, "- samples: %ld\n", samples[i].n_samples);
        fprintf(fp, "- rate: %d\n", samples[i].sample_rate);
        fprintf(fp, "- compat rate: %d\n", samples[i].sample_rate);
        fprintf(fp, "- loop: no\n");
        fprintf(fp, "- BRR emphasis: yes\n");
        fprintf(fp, "- no BRR filters: no\n");
        fprintf(fp, "- dither: no\n\n");

        fprintf(fp, "```\n");
        write_hex_dump(fp, samples[i].pcm, samples[i].pcm_len);
        fprintf(fp, "```\n\n\n");

        printf("  Sample %d/%d written.\n", i + 1, n_samples);
    }

    // --- Subsongs ---
    fprintf(fp, "# Subsongs\n\n");
    fprintf(fp, "## 0: \n\n");
    fprintf(fp, "- tick rate: %d\n", tick_rate);
    fprintf(fp, "- speeds: %ld\n", speed);
    fprintf(fp, "- virtual tempo: %d/%d\n", vt_num, vt_den);
    fprintf(fp, "- time base: 0\n");
    fprintf(fp, "- pattern length: %ld\n\n", pattern_rows);

    // --- Orders ---
    fprintf(fp, "orders:\n");
    fprintf(fp, "```\n");
    for (int i = 0; i < n_samples; i++) {
        fprintf(fp, "%02X | %02X\n", i, i);
    }
    fprintf(fp, "```\n\n");

    // --- Patterns ---
    fprintf(fp, "## Patterns\n\n");
    for (int i = 0; i < n_samples; i++) {
        char note[8];
        index_to_note(i, note, sizeof(note));

        fprintf(fp, "----- ORDER %02X\n", i);
        fprintf(fp, "00 |%s 00 .. ....\n", note);
        for (long row = 1; row < pattern_rows; row++) {
            fprintf(fp, "%02lX |... .. .. ....\n", row);
        }
    }

    fclose(fp);

    // Cleanup
    for (int i = 0; i < n_samples; i++) free(samples[i].pcm);

    printf("Furnace text export written to: %s\n", output_file);
    printf("  %d samples, %d orders, BPM=%d, virtual tempo=%d/%d\n",
           n_samples, n_samples, vt_num, vt_num, vt_den);

    return 0;
}
