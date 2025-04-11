/*
slicer.c is a software that computes slices from an input audio file utulizing fprobe and ffmpeg for duration and slicing respectively
You can utulize the sliced files in Furnace Tracker as samples for audio reference during chiptune creation.

Usage: ./slicer <FILENAME> <BPM> <rows_per_beat> <pattern rows> <naming_mode>

naming_mode: DEC for decimal naming, HEX for hexadecimal naming OBVIOUSLY
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

double get_audio_duration(const char *FILENAME) {
    char command[1024];
    snprintf(command, sizeof(command), "ffprobe -i \"%s\" -show_entries format=duration -v quiet -of csv=\"p=0\"", FILENAME);

    FILE *fp = popen(command, "r");
    if (!fp) {
        fprintf(stderr, "command run failure! ffprobe couldn't be executed. \n");
        return -1;
    }

    char buffer[128];
    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        pclose(fp);
        return -1;
    }
    pclose(fp);
    double duration = atof(buffer);
    return (duration > 0) ? duration : -1;
}

void get_base_name(const char *FILENAME, char *base, size_t base_size) {
    const char *slash = strrchr(FILENAME, '/'); 
#ifdef _WIN32                                                // windows support bullshit 
    const char *backslash = strrchr(FILENAME, '\\'); 
    if (backslash && (!slash || backslash > slash)) {
        slash = backslash;
    }
#endif
    if (slash) {
        strncpy(base, slash + 1, base_size);
    } else {
        strncpy(base, FILENAME, base_size);
    }

    // Remove unnecessary path and extension
    char *dot = strrchr(base, '.');
    if (dot) {
        *dot = '\0'; 
    }
}

int main(int argc, char *argv[]){
    if(argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        printf("Usage: ./slicer <FILENAME> <BPM> <rows_per_beat> <pattern_rows> <naming_mode>\n");
        printf("naming_mode: DEC for decimal naming, HEX for hexadecimal naming\n");
        return 0;
    }
    if(argc < 6) {
        fprintf(stderr, "Error: Insufficient arguments provided.\n");
        return 1;
    }
    const char *FILENAME = argv[1];
    double BPM = atof(argv[2]);
    int rows_per_beat = atoi(argv[3]);
    int pattern_rows = atoi(argv[4]);
    const char *naming_mode = argv[5];

    double beat_duration = 60.0 / BPM;
    double seconds_per_row = beat_duration / rows_per_beat;
    double slice_duration = seconds_per_row * pattern_rows;

    double total_duration = get_audio_duration(FILENAME);
    if(total_duration < 0) {
        fprintf(stderr, "Error: Could not get audio duration of %s\n", FILENAME);
        return 1;
    }

    int total_slices = (int) floor(total_duration / slice_duration);
    printf("Total duration: %.2f seconds\n", total_duration);
    printf("Slice duration: %.5f seconds\n", slice_duration);
    printf("Total slices: %d\n", total_slices);

    char base[256];
    get_base_name(FILENAME, base, sizeof(base));

    char output_dir[256];
    snprintf(output_dir, sizeof(output_dir), "%s", base);
#ifdef _WIN32
    _mkdir(output_dir);
#else
    mkdir(output_dir, 0755);
#endif

    printf("Input file: %s\n", FILENAME);
    printf("Base name: %s\n", base);
    printf("Output directory: %s\n", output_dir);

    for (int i = 0; i < total_slices; i++) {
        double start_time = i * slice_duration;
        char filename[256];
        if (strcmp(naming_mode, "DEC") == 0) {
#ifdef _WIN32
            snprintf(filename, sizeof(filename), "%s\\%s_%02d.wav", output_dir, base, i + 1);
#else
            snprintf(filename, sizeof(filename), "%s/%s_%02d.wav", output_dir, base, i + 1);
#endif
        } else if (strcmp(naming_mode, "HEX") == 0) {
#ifdef _WIN32
            snprintf(filename, sizeof(filename), "%s\\%s_%X.wav", output_dir, base, i + 1);
#else
            snprintf(filename, sizeof(filename), "%s/%s_%X.wav", output_dir, base, i + 1);
#endif
        } else {
            fprintf(stderr, "Error: Invalid naming mode provided: %s. Please use DEC or HEX.\n", naming_mode);
            return 1;
        }
        char command[1024];
#ifdef _WIN32
        snprintf(command, sizeof(command),
                 "ffmpeg -ss %.5f -t %.5f -i \"%s\" -acodec pcm_s16le -ar 44100 -ac 1 -y \"%s\" > NUL 2>&1",
                 start_time, slice_duration, FILENAME, filename);
#else
        snprintf(command, sizeof(command),
                 "ffmpeg -ss %.5f -t %.5f -i \"%s\" -acodec pcm_s16le -ar 44100 -ac 1 -y \"%s\" > /dev/null 2>&1",
                 start_time, slice_duration, FILENAME, filename);
#endif
        printf("Processing slice %d/%d: %s\n", i + 1, total_slices, filename);
        int ret = system(command);
        if (ret != 0) {
            fprintf(stderr, "Error processing slice %d\n", i + 1);
            return 1;
        }
    }

    printf("All slices processed successfully.\n");
    return 0;
}