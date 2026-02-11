/*
slicer.c is a software that computes slices from an input audio file utilizing ffprobe and ffmpeg for duration and slicing respectively.
You can utilize the sliced files in Furnace Tracker as samples for audio reference during chiptune creation.

Usage: ./slicer <FILENAME> <BPM> <rows_per_beat> <pattern rows> <naming_mode>

naming_mode: DEC for decimal naming, HEX for hexadecimal naming OBVIOUSLY
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

// Escape a string for safe use in a shell command.
// Returns a newly allocated string that must be freed by the caller.
static char *shell_escape(const char *input) {
    size_t len = strlen(input);
#ifdef _WIN32
    // Windows: wrap in double quotes, escape internal double quotes and percent signs
    char *escaped = malloc(len * 2 + 3);
    if (!escaped) return NULL;

    char *p = escaped;
    *p++ = '"';
    for (size_t i = 0; i < len; i++) {
        if (input[i] == '"') {
            *p++ = '\\';
            *p++ = '"';
        } else if (input[i] == '%') {
            *p++ = '%';
            *p++ = '%';
        } else {
            *p++ = input[i];
        }
    }
    *p++ = '"';
    *p = '\0';
    return escaped;
#else
    // Unix: wrap in single quotes, escape internal single quotes as '\''
    char *escaped = malloc(len * 4 + 3);
    if (!escaped) return NULL;

    char *p = escaped;
    *p++ = '\'';
    for (size_t i = 0; i < len; i++) {
        if (input[i] == '\'') {
            *p++ = '\'';
            *p++ = '\\';
            *p++ = '\'';
            *p++ = '\'';
        } else {
            *p++ = input[i];
        }
    }
    *p++ = '\'';
    *p = '\0';
    return escaped;
#endif
}

// Function to get the duration of the audio file using ffprobe
double get_audio_duration(const char *FILENAME) {
    char *escaped_filename = shell_escape(FILENAME);
    if (!escaped_filename) {
        fprintf(stderr, "Error: Memory allocation failed.\n");
        return -1;
    }

    char command[2048];
    snprintf(command, sizeof(command),
             "ffprobe -i %s -show_entries format=duration -v quiet -of csv=\"p=0\"",
             escaped_filename);
    free(escaped_filename);

    FILE *fp = popen(command, "r"); // Execute the command and open a pipe to read the output
    if (!fp) {
        fprintf(stderr, "Error: ffprobe couldn't be executed.\n");
        return -1;
    }

    char buffer[128];
    if (fgets(buffer, sizeof(buffer), fp) == NULL) { // Read the duration from the command output
        pclose(fp);
        return -1; // Return -1 if reading fails
    }

    int status = pclose(fp);
    if (status != 0) {
        fprintf(stderr, "Error: ffprobe exited with non-zero status %d.\n", status);
        return -1;
    }

    double duration = atof(buffer); // Convert the duration to a double
    return (duration > 0) ? duration : -1; // Return the duration if valid, otherwise -1
}

int main(int argc, char *argv[]){
    // Display help message if the user provides --help or -h as an argument
    if(argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        printf("Usage: ./slicer <FILENAME> <BPM> <rows_per_beat> <pattern_rows> <naming_mode> <output_folder> <slice_prefix>\n");
        printf("naming_mode: DEC for decimal naming, HEX for hexadecimal naming\n");
        return 0;
    }

    // Check if the required number of arguments is provided
    if(argc < 8) {
        fprintf(stderr, "Error: Insufficient arguments provided.\n");
        fprintf(stderr, "Usage: ./slicer <FILENAME> <BPM> <rows_per_beat> <pattern_rows> <naming_mode> <output_folder> <slice_prefix>\n");
        return 1;
    }

    // Parse command-line arguments
    const char *FILENAME = argv[1];
    const char *naming_mode = argv[5]; // Naming mode (DEC or HEX)
    const char *output_folder = argv[6]; // Custom output folder name
    const char *slice_prefix = argv[7]; // Custom slice prefix

    // Validate naming mode early, before any processing
    if (strcmp(naming_mode, "DEC") != 0 && strcmp(naming_mode, "HEX") != 0) {
        fprintf(stderr, "Error: Invalid naming mode '%s'. Please use DEC or HEX.\n", naming_mode);
        return 1;
    }

    // Validate numeric arguments using strtod/strtol for proper error detection
    char *endptr;

    errno = 0;
    double BPM = strtod(argv[2], &endptr);
    if (*endptr != '\0' || errno != 0 || BPM <= 0) {
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

    // Check that input file exists
    struct stat file_stat;
    if (stat(FILENAME, &file_stat) != 0) {
        fprintf(stderr, "Error: Input file '%s' not found: %s\n", FILENAME, strerror(errno));
        return 1;
    }

    // Calculate durations based on BPM and rows
    double beat_duration = 60.0 / BPM; // Duration of one beat in seconds
    double seconds_per_row = beat_duration / (double)rows_per_beat; // Duration of one row in seconds
    double slice_duration = seconds_per_row * (double)pattern_rows; // Duration of one slice in seconds

    // Get the total duration of the audio file
    double total_duration = get_audio_duration(FILENAME);
    if(total_duration < 0) {
        fprintf(stderr, "Error: Could not get audio duration of '%s'.\n", FILENAME);
        return 1;
    }

    // Calculate the total number of slices
    int total_slices = (int) floor(total_duration / slice_duration + 1e-9);
    if (total_slices <= 0) {
        fprintf(stderr, "Error: Slice duration (%.5f s) exceeds total duration (%.2f s). No slices to produce.\n",
                slice_duration, total_duration);
        return 1;
    }

    // Warn if slice count exceeds naming format capacity
    if (strcmp(naming_mode, "DEC") == 0 && total_slices > 100) {
        fprintf(stderr, "Warning: %d slices exceeds 2-digit decimal range (00-99). Filenames will have 3+ digits.\n", total_slices);
    } else if (strcmp(naming_mode, "HEX") == 0 && total_slices > 256) {
        fprintf(stderr, "Warning: %d slices exceeds 2-digit hexadecimal range (00-FF). Filenames will have 3+ digits.\n", total_slices);
    }

    printf("Total duration: %.2f seconds\n", total_duration);
    printf("Slice duration: %.5f seconds\n", slice_duration);
    printf("Total slices: %d\n", total_slices);

    // Create an output directory
    char output_dir[512];
    snprintf(output_dir, sizeof(output_dir), "%s", output_folder);
    int mkdir_ret;
#ifdef _WIN32
    mkdir_ret = _mkdir(output_dir);
#else
    mkdir_ret = mkdir(output_dir, 0755);
#endif
    if (mkdir_ret != 0 && errno != EEXIST) {
        fprintf(stderr, "Error: Could not create output directory '%s': %s\n", output_dir, strerror(errno));
        return 1;
    }

    printf("Input file: %s\n", FILENAME);
    printf("Output directory: %s\n", output_dir);
    printf("Slice prefix: %s\n", strlen(slice_prefix) > 0 ? slice_prefix : "(none)");

    // Pre-escape input filename for use in the loop
    char *escaped_input = shell_escape(FILENAME);
    if (!escaped_input) {
        fprintf(stderr, "Error: Memory allocation failed.\n");
        return 1;
    }

    // Loop through each slice and process it
    for (int i = 0; i < total_slices; i++) {
        // Compute start time from index to avoid cumulative floating-point drift
        double start_time = (double)i * slice_duration;
        char filepath[1024];
        const char *separator = (strlen(slice_prefix) > 0) ? "_" : "";

        // Generate the output file path based on the naming mode
        if (strcmp(naming_mode, "DEC") == 0) {
#ifdef _WIN32
            snprintf(filepath, sizeof(filepath), "%s\\%s%s%02d.wav", output_dir, slice_prefix, separator, i);
#else
            snprintf(filepath, sizeof(filepath), "%s/%s%s%02d.wav", output_dir, slice_prefix, separator, i);
#endif
        } else {
#ifdef _WIN32
            snprintf(filepath, sizeof(filepath), "%s\\%s%s%02X.wav", output_dir, slice_prefix, separator, i);
#else
            snprintf(filepath, sizeof(filepath), "%s/%s%s%02X.wav", output_dir, slice_prefix, separator, i);
#endif
        }

        // Shell-escape the output filepath
        char *escaped_output = shell_escape(filepath);
        if (!escaped_output) {
            fprintf(stderr, "Error: Memory allocation failed.\n");
            free(escaped_input);
            return 1;
        }

        // Construct the ffmpeg command to extract the slice
        char command[4096];
#ifdef _WIN32
        snprintf(command, sizeof(command),
                 "ffmpeg -ss %.5f -t %.5f -i %s -acodec pcm_s16le -ar 44100 -ac 1 -y %s > NUL 2>&1",
                 start_time, slice_duration, escaped_input, escaped_output);
#else
        snprintf(command, sizeof(command),
                 "ffmpeg -ss %.5f -t %.5f -i %s -acodec pcm_s16le -ar 44100 -ac 1 -y %s > /dev/null 2>&1",
                 start_time, slice_duration, escaped_input, escaped_output);
#endif
        free(escaped_output);

        // Print progress and execute the command
        printf("Processing slice %d/%d: %s\n", i + 1, total_slices, filepath);
        int ret = system(command);
        if (ret != 0) {
            fprintf(stderr, "Error processing slice %d\n", i + 1);
            free(escaped_input);
            return 1;
        }
    }

    free(escaped_input);

    // Print success message after all slices are processed
    printf("All slices processed successfully.\n");
    return 0;
}
