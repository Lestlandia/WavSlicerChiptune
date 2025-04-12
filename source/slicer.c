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
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

// Function to get the duration of the audio file using ffprobe
double get_audio_duration(const char *FILENAME) {
    char command[1024];
    // Construct the ffprobe command to fetch the duration of the audio file
    snprintf(command, sizeof(command), "ffprobe -i \"%s\" -show_entries format=duration -v quiet -of csv=\"p=0\"", FILENAME);

    FILE *fp = popen(command, "r"); // Execute the command and open a pipe to read the output
    if (!fp) {
        fprintf(stderr, "command run failure! ffprobe couldn't be executed. \n");
        return -1; // Return -1 if the command fails
    }

    char buffer[128];
    if (fgets(buffer, sizeof(buffer), fp) == NULL) { // Read the duration from the command output
        pclose(fp);
        return -1; // Return -1 if reading fails
    }
    pclose(fp);
    double duration = atof(buffer); // Convert the duration to a double
    return (duration > 0) ? duration : -1; // Return the duration if valid, otherwise -1
}

// Function to extract the base name (file name without path and extension) from the input file path
void get_base_name(const char *FILENAME, char *base, size_t base_size) {
    const char *slash = strrchr(FILENAME, '/'); // Find the last forward slash in the path
#ifdef _WIN32
    const char *backslash = strrchr(FILENAME, '\\'); // Find the last backslash for Windows paths
    if (backslash && (!slash || backslash > slash)) {
        slash = backslash; // Use the backslash if it's the last separator
    }
#endif
    if (slash) {
        strncpy(base, slash + 1, base_size); // Copy the file name after the last slash
    } else {
        strncpy(base, FILENAME, base_size); // If no slash, use the entire FILENAME
    }

    // Remove unnecessary path and extension
    char *dot = strrchr(base, '.'); // Find the last dot for the file extension
    if (dot) {
        *dot = '\0'; // Terminate the string to remove the extension
    }
}

int main(int argc, char *argv[]){
    // Display help message if the user provides --help or -h as an argument
    if(argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        printf("Usage: ./slicer <FILENAME> <BPM> <rows_per_beat> <pattern_rows> <naming_mode>\n");
        printf("naming_mode: DEC for decimal naming, HEX for hexadecimal naming\n");
        return 0;
    }

    // Check if the required number of arguments is provided
    if(argc < 6) {
        fprintf(stderr, "Error: Insufficient arguments provided.\n");
        return 1;
    }

    // Parse command-line arguments
    const char *FILENAME = argv[1];
    double BPM = atof(argv[2]); // Convert BPM to a double
    int rows_per_beat = atoi(argv[3]); // Convert rows_per_beat to an integer
    int pattern_rows = atoi(argv[4]); // Convert pattern_rows to an integer
    const char *naming_mode = argv[5]; // Naming mode (DEC or HEX)

    // Calculate durations based on BPM and rows
    double beat_duration = 60.0 / BPM; // Duration of one beat in seconds
    double seconds_per_row = beat_duration / rows_per_beat; // Duration of one row in seconds
    double slice_duration = seconds_per_row * pattern_rows; // Duration of one slice in seconds

    // Get the total duration of the audio file
    double total_duration = get_audio_duration(FILENAME);
    if(total_duration < 0) {
        fprintf(stderr, "Error: Could not get audio duration of %s\n", FILENAME);
        return 1;
    }

    // Calculate the total number of slices
    int total_slices = (int) floor(total_duration / slice_duration);
    printf("Total duration: %.2f seconds\n", total_duration);
    printf("Slice duration: %.5f seconds\n", slice_duration);
    printf("Total slices: %d\n", total_slices);

    // Extract the base name of the input file
    char base[256];
    get_base_name(FILENAME, base, sizeof(base));

    // Create an output directory named after the base name
    char output_dir[256];
    snprintf(output_dir, sizeof(output_dir), "%s", base);
#ifdef _WIN32
    _mkdir(output_dir); // Create directory on Windows
#else
    mkdir(output_dir, 0755); // Create directory on Unix-like systems
#endif

    printf("Input file: %s\n", FILENAME);
    printf("Base name: %s\n", base);
    printf("Output directory: %s\n", output_dir);

    // Loop through each slice and process it
    for (int i = 0; i < total_slices; i++) {
        double start_time = i * slice_duration; // Calculate the start time of the slice
        char filename[256];

        // Generate the output file name based on the naming mode
        if (strcmp(naming_mode, "DEC") == 0) {
#ifdef _WIN32
            snprintf(filename, sizeof(filename), "%s\\%s_%02d.wav", output_dir, base, i + 1);
#else
            snprintf(filename, sizeof(filename), "%s/%s_%02d.wav", output_dir, base, i + 1);
#endif
        } else if (strcmp(naming_mode, "HEX") == 0) {
#ifdef _WIN32
            snprintf(filename, sizeof(filename), "%s\\%s_%02X.wav", output_dir, base, i + 1);
#else
            snprintf(filename, sizeof(filename), "%s/%s_%02X.wav", output_dir, base, i + 1);
#endif
        } else {
            // Handle invalid naming mode
            fprintf(stderr, "Error: Invalid naming mode provided: %s. Please use DEC or HEX.\n", naming_mode);
            return 1;
        }

        // Construct the ffmpeg command to extract the slice
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

        // Print progress and execute the command
        printf("Processing slice %d/%d: %s\n", i + 1, total_slices, filename);
        int ret = system(command);
        if (ret != 0) {
            fprintf(stderr, "Error processing slice %d\n", i + 1);
            return 1;
        }
    }

    // Print success message after all slices are processed
    printf("All slices processed successfully.\n");
    return 0;
}