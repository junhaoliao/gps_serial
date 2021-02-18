#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>

const char *LINE_DELIM = "\r\n";
const char *FIELD_DELIM = ",";

#define ERROR(...) do { \
    printf("[ERROR] "__VA_ARGS__); \
} while(0)

#define WARNING(...) do { \
    printf("[WARNING] "__VA_ARGS__); \
} while(0)

#define MESSAGE(...) do { \
    printf("[MESSAGE] "__VA_ARGS__); \
} while(0)

#define ERROR(...)
#define WARNING(...)
#define MESSAGE(...)

static char *ADDRESS[] = {
        "$GNTXT",
        "$GNRMC",
        "$GNVTG",
        "$GNGGA",
        "$GNGSA",
        "$GNGSA",
        "$GPGSV",
        "$GLGSV",
        "$GNGLL"
};
enum ADDRESS_TYPE {
    GNTXT = 0,
    GNRMC,
    GNVTG,
    GNGGA,
    GNGSA,
    GPGSV,
    GLGSV,
    GNGLL,
    ADDRESS_TYPE_NONE = 0x7ffffff
};
const unsigned long ADDRESS_COUNT = sizeof(ADDRESS) / sizeof(ADDRESS[0]);

char * read_tok(char** p_line, const char* token_name){
    char* token = strsep(p_line, FIELD_DELIM);
    if (0 == strcmp("",token)){
        MESSAGE("%s missing \n", token_name);
    }
    return token;
}

void skip_field(char** p_line, int count){
    for (int skip_counter = 0; skip_counter < count; skip_counter++){
        strsep(p_line, FIELD_DELIM);
    }
}

void gnrmc_handler(char ** p_line){
    float  utc = .0f;
    unsigned char hour = 0;
    unsigned char minute = 0;
    float second = .0f;

    float lat = .0f;
    float lon = .0f;

    unsigned date = 0;
    unsigned char day = 0;
    unsigned char month = 0;
    unsigned char year = 0;

    char* tok_utc = NULL;
    char* tok_pos_status = NULL;
    char* tok_lat = NULL;
    char* lat_dir = NULL;
    char* tok_lon = NULL;
    char* lon_dir = NULL;
    char* tok_speed = NULL; // unused for now
    char* tok_track = NULL; // unused for now
    char* tok_date = NULL;
    
    // get the UTC
    if (!(tok_utc = read_tok(p_line,"UTC"))){
        goto gnrmc_position;
    }
    utc = strtof(tok_utc, NULL);
    hour = (unsigned short)(utc / 10000);
    minute = (unsigned short)(utc / 100) - hour * 100;
    second = utc - (float)hour * 10000 - (float)minute * 100;

    gnrmc_position:
    // get the position
    tok_pos_status = read_tok(p_line,"Position Status");
    if (0 == strcmp(tok_pos_status, "V")){
        MESSAGE("Position data invalid \n");
        skip_field(p_line,4);
        goto gnrmc_speed;
    } else if (0 != strcmp(tok_pos_status, "A")){
        ERROR("Position status corrupted \n");
        goto gnrmc_skip_rest;
    }

    tok_lat = read_tok(p_line,"Latitude");
    lat = strtof(tok_lat, NULL) / 100;

    lat_dir = read_tok(p_line,"Latitude Direction");
    if (((0 != strcmp("N", lat_dir))&&(0 != strcmp("S", lat_dir)))){
        ERROR("Latitude Direction corrupted \n");
        goto gnrmc_skip_rest;
    }

    tok_lon = read_tok(p_line,"Longitude");
    lon = strtof(tok_lon, NULL) / 100;

    lon_dir = read_tok(p_line,"Longitude Direction");
    if (((0 != strcmp("E", lon_dir))&&(0 != strcmp("W", lon_dir)))){
        ERROR("Longitude Direction corrupted \n");
        goto gnrmc_skip_rest;
    }

    gnrmc_speed:
    tok_speed = read_tok(p_line,"Speed");
    gnrmc_track:
    tok_track = read_tok(p_line,"Track");

    gnrmc_date:
    tok_date = read_tok(p_line,"Date");
    date = strtoul(tok_date, NULL, 10);
    day = date / 10000;
    month = date / 100 - day * 100;
    year = date - day * 10000 - month * 100;

    printf("hour:%02u, minute:%02u, second:%05.2f| "
           "%s, lat:%f%s, lon:%f%s| "
           "date:%02u/%02u/%02u\n",
           hour, minute, second,
           tok_pos_status, lat, lat_dir, lon, lon_dir,
           year,month,day);

    gnrmc_skip_rest:
    while (strtok(NULL, FIELD_DELIM));
}



int main() {
    int serial_port = open("/dev/ttyACM0", O_RDWR);

    struct termios tty = {};

    if (tcgetattr(serial_port, &tty) != 0) {
        ERROR("%i from tcgetattr: %s\n", errno, strerror(errno));
        return 1;
    }

    cfsetispeed(&tty, B4800);
    cfsetospeed(&tty, B4800);

    if (tcsetattr(serial_port, TCSANOW, &tty) != 0) {
        ERROR("%i from tcsetattr: %s\n", errno, strerror(errno));
        return 1;
    }

    char read_buf[256];


    while (true) {
        memset(read_buf, 0, sizeof(read_buf));

        int num_bytes = read(serial_port, &read_buf, sizeof(read_buf));

        if (num_bytes < 0) {
            ERROR("reading: %s", strerror(errno));
            return 1;
        }

        char *line = strtok(read_buf, LINE_DELIM);
        char *field = NULL;
        while (line) {
            if (line[0] != '$') {
                WARNING("Line doesn't start with 's'\n");
                goto next_line;
            }

            field = strsep(&line, FIELD_DELIM);
            if (field == NULL) {
                WARNING("Didn't find any field in this line\n");
                goto next_line;
            }
            int address_type;
            for (address_type = 0; address_type < ADDRESS_COUNT; address_type++) {
                if (0 == strcmp(ADDRESS[address_type], field)) {
                    break;
                }
            }

            switch (address_type) {
                case GNRMC:
                    gnrmc_handler(&line);
                    break;
                case GNVTG:
                case GNGGA:
                case GNGSA:
                case GPGSV:
                case GLGSV:
                case GNGLL:
                case GNTXT:
                default:
                    WARNING("Unsupported address field: %s\n", field);
                    while ((field = strtok(NULL, FIELD_DELIM)));
                    goto next_line;
            }

            next_line:
            line = strtok(NULL, LINE_DELIM);
        }
    }
    close(serial_port);
    return 0; // success
}