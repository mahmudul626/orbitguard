/*
 * Space Debris Tracker - C Server Backend (Multi-threaded with Auth)
 * -------------------------------------------------------------------
 * Version with user authentication, freemium model, API key generation,
 * and mission details lookup from SATCAT.
 *
 * COMPILE:
 * gcc -o space_debris_server server.c cJSON.c -lcurl -lm -lpthread
 *
 * RUN:
 * ./space_debris_server
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <curl/curl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include "cJSON.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- Constants and Structs ---
#define MAX_SATS 10000
#define MAX_USERS 100
#define BUFFER_SIZE 8192
#define LINE_LEN 256
#define NAME_LEN 128
#define USERS_DB_FILE "users.json"

/* Physical constants */
const double EARTH_MU = 398600.4418; /* km^3 / s^2 */
const double EARTH_RADIUS = 6378.137; /* km (equatorial) */

typedef struct {
    char name[NAME_LEN];
    char tle1[LINE_LEN];
    char tle2[LINE_LEN];
    int norad_id;
    double altitude;
    double inclination;
    double raan;
    double eccentricity;
    double arg_perigee;
    double mean_anomaly;
    double mean_motion;
    double semi_major_axis;
    double epoch_time;
    int valid;
} Satellite;

// --- NEW: SATCAT Data Structure ---
typedef struct {
    int norad_id;
    char official_name[NAME_LEN];
    char country[NAME_LEN];
    char launch_date[12];
    char purpose[NAME_LEN];
    char status[16];
} SatCatData;


typedef struct {
    char email[128];
    char password_hash[64];
    char plan[16]; // "free" or "pro"
    char token[64];
    char api_key[64];
    long plan_expiry_date; // timestamp
} User;

// --- Global Data ---
static Satellite SATS_DB[MAX_SATS];
static int SATS_COUNT = 0;
// --- NEW: Global Database for SATCAT data ---
static SatCatData SATCAT_DB[MAX_SATS];
static int SATCAT_COUNT = 0;

static User USERS_DB[MAX_USERS];
static int USERS_COUNT = 0;
pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;

// --- Core Satellite Logic ---
static double deg2rad(double deg) { return deg * M_PI / 180.0; }
static double get_tle_val(const char *tle_line, int start, int len) {
    char buf[32];
    strncpy(buf, tle_line + start, len);
    buf[len] = '\0';
    return atof(buf);
}
static int get_tle_int(const char* tle_line, int start, int len) {
    char buf[32];
    strncpy(buf, tle_line + start, len);
    buf[len] = '\0';
    return atoi(buf);
}

static int parse_tle_elements(Satellite *sat) {
    char tle2[LINE_LEN];
    strncpy(tle2, sat->tle2, LINE_LEN);
    sat->inclination = deg2rad(get_tle_val(tle2, 8, 8));
    sat->raan = deg2rad(get_tle_val(tle2, 17, 8));
    char ecc_str[16] = "0.";
    strncat(ecc_str, tle2 + 26, 7);
    sat->eccentricity = atof(ecc_str);
    sat->arg_perigee = deg2rad(get_tle_val(tle2, 34, 8));
    sat->mean_anomaly = deg2rad(get_tle_val(tle2, 43, 8));
    sat->mean_motion = get_tle_val(tle2, 52, 11);
    if (sat->mean_motion <= 0) return 0;
    double n_rad_per_sec = sat->mean_motion * 2.0 * M_PI / 86400.0;
    double a_cubed = EARTH_MU / (n_rad_per_sec * n_rad_per_sec);
    sat->semi_major_axis = cbrt(a_cubed);
    sat->altitude = sat->semi_major_axis - EARTH_RADIUS;
    if (sat->altitude < -0.5) return 0;
    return 1;
}

static void trim_newline(char *s) {
    size_t L = strlen(s);
    while (L > 0 && (s[L-1] == '\n' || s[L-1] == '\r')) { s[--L] = '\0'; }
}

static int load_tle_file(const char *filename, Satellite sats[], int max_sats) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;
    char line[LINE_LEN];
    int count = 0;
    while (count < max_sats) {
        if (!fgets(line, LINE_LEN, f)) break;
        trim_newline(line);
        if (strlen(line) == 0) continue;
        strncpy(sats[count].name, line, NAME_LEN-1);
        sats[count].name[NAME_LEN-1] = '\0';
        if (!fgets(sats[count].tle1, LINE_LEN, f)) break;
        trim_newline(sats[count].tle1);
        if (!fgets(sats[count].tle2, LINE_LEN, f)) break;
        trim_newline(sats[count].tle2);

        sats[count].norad_id = get_tle_int(sats[count].tle1, 2, 5);

        char epoch_str[15];
        char year_str[3];
        strncpy(epoch_str, sats[count].tle1 + 18, 14);
        epoch_str[14] = '\0';
        strncpy(year_str, epoch_str, 2);
        year_str[2] = '\0';
        int epoch_year = atoi(year_str);
        double epoch_day = atof(epoch_str + 2);
        int full_year = (epoch_year < 57) ? (2000 + epoch_year) : (1900 + epoch_year);
        struct tm t = {0};
        t.tm_year = full_year - 1900;
        t.tm_mday = 1;
        time_t jan1 = timegm(&t);
        sats[count].epoch_time = jan1 + (epoch_day - 1.0) * 86400.0;

        if (parse_tle_elements(&sats[count])) {
            sats[count].valid = 1;
        } else {
            sats[count].valid = 0;
        }
        count++;
    }
    fclose(f);
    return count;
}

// --- NEW: Load SATCAT data from sat_data.txt ---
static int load_satcat_file(const char* filename, SatCatData satcat_db[], int max_sats) {
    FILE* f = fopen(filename, "r");
    if (!f) return -1;
    char line[512]; // Increased buffer for long SATCAT lines
    int count = 0;
    while (count < max_sats && fgets(line, sizeof(line), f)) {
        if (strlen(line) < 100) continue; // Skip malformed lines

        char norad_str[6];
        strncpy(norad_str, line + 13, 5);
        norad_str[5] = '\0';
        satcat_db[count].norad_id = atoi(norad_str);

        char name_buf[64];
        strncpy(name_buf, line + 23, 25);
        name_buf[25] = '\0';
        trim_newline(name_buf);
        strcpy(satcat_db[count].official_name, name_buf);

        char country_buf[16];
        strncpy(country_buf, line + 49, 5);
        country_buf[5] = '\0';
        trim_newline(country_buf);
        strcpy(satcat_db[count].country, country_buf);

        char launch_date_buf[12];
        strncpy(launch_date_buf, line + 64, 10);
        launch_date_buf[10] = '\0';
        strcpy(satcat_db[count].launch_date, launch_date_buf);
        
        // Simple status check based on decay date
        if (line[83] == ' ' || line[83] == '0') {
            strcpy(satcat_db[count].status, "Active");
        } else {
            strcpy(satcat_db[count].status, "Decayed/Inactive");
        }

        // Dummy purpose for demonstration
        switch(satcat_db[count].norad_id % 5) {
            case 0: strcpy(satcat_db[count].purpose, "Communications"); break;
            case 1: strcpy(satcat_db[count].purpose, "Earth Observation"); break;
            case 2: strcpy(satcat_db[count].purpose, "Navigation"); break;
            case 3: strcpy(satcat_db[count].purpose, "Scientific"); break;
            default: strcpy(satcat_db[count].purpose, "Commercial"); break;
        }

        count++;
    }
    fclose(f);
    return count;
}


void propagate_orbit(const Satellite *sat, double sim_time, double *x, double *y, double *z) {
    if (!sat->valid) return;
    double dt = sim_time - sat->epoch_time;
    double n_rad_per_sec = sat->mean_motion * 2.0 * M_PI / 86400.0;
    double M = sat->mean_anomaly + n_rad_per_sec * dt;
    M = fmod(M, 2.0 * M_PI);
    if (M < 0) M += 2.0 * M_PI;
    double E = M;
    for (int i = 0; i < 7; i++) {
        E = E - (E - sat->eccentricity * sin(E) - M) / (1.0 - sat->eccentricity * cos(E));
    }
    double nu = 2.0 * atan2(sqrt(1.0 + sat->eccentricity) * sin(E / 2.0),
                           sqrt(1.0 - sat->eccentricity) * cos(E / 2.0));
    double r = sat->semi_major_axis * (1.0 - sat->eccentricity * cos(E));
    double ox = r * cos(nu);
    double oy = r * sin(nu);
    double cos_raan = cos(sat->raan);
    double sin_raan = sin(sat->raan);
    double cos_argp = cos(sat->arg_perigee);
    double sin_argp = sin(sat->arg_perigee);
    double cos_inc = cos(sat->inclination);
    double sin_inc = sin(sat->inclination);
    *x = ox * (cos_raan * cos_argp - sin_raan * sin_argp * cos_inc) -
         oy * (cos_raan * sin_argp + sin_raan * cos_argp * cos_inc);
    *y = ox * (sin_raan * cos_argp + cos_raan * sin_argp * cos_inc) -
         oy * (sin_raan * sin_argp - cos_raan * cos_argp * cos_inc);
    *z = ox * (sin_argp * sin_inc) + oy * (cos_argp * sin_inc);
}
static int is_same_system(const char *name1, const char *name2) {
    char prefix1[32], prefix2[32];
    sscanf(name1, "%s", prefix1);
    sscanf(name2, "%s", prefix2);
    return (strlen(prefix1) > 2 && strcmp(prefix1, prefix2) == 0);
}
static size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    return fwrite(ptr, size, nmemb, stream);
}
static int download_tle_file(const char *url, const char *output_filename) {
    CURL *curl_handle;
    FILE *pagefile;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();
    if (curl_handle) {
        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
        pagefile = fopen(output_filename, "wb");
        if (pagefile) {
            curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data);
            curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, pagefile);
            res = curl_easy_perform(curl_handle);
            fclose(pagefile);
            if (res != CURLE_OK) return 0;
        } else return 0;
        curl_easy_cleanup(curl_handle);
    }
    curl_global_cleanup();
    return 1;
}

// --- USER MANAGEMENT & UTILS (Unchanged)---
void simple_hash(const char *str, char *output) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    sprintf(output, "%lx", hash);
}
void generate_random_string(char *str, size_t size) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    if (size) {
        --size;
        for (size_t n = 0; n < size; n++) {
            int key = rand() % (int) (sizeof charset - 1);
            str[n] = charset[key];
        }
        str[size] = '\0';
    }
}
void save_users_db() {
    pthread_mutex_lock(&db_mutex);
    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < USERS_COUNT; i++) {
        cJSON *user_json = cJSON_CreateObject();
        cJSON_AddStringToObject(user_json, "email", USERS_DB[i].email);
        cJSON_AddStringToObject(user_json, "password_hash", USERS_DB[i].password_hash);
        cJSON_AddStringToObject(user_json, "plan", USERS_DB[i].plan);
        cJSON_AddStringToObject(user_json, "token", USERS_DB[i].token);
        cJSON_AddStringToObject(user_json, "api_key", USERS_DB[i].api_key);
        cJSON_AddNumberToObject(user_json, "plan_expiry_date", USERS_DB[i].plan_expiry_date);
        cJSON_AddItemToArray(root, user_json);
    }
    char *json_string = cJSON_Print(root);
    FILE *f = fopen(USERS_DB_FILE, "w");
    if (f) {
        fprintf(f, "%s", json_string);
        fclose(f);
    }
    free(json_string);
    cJSON_Delete(root);
    pthread_mutex_unlock(&db_mutex);
}
void load_users_db() {
    FILE *f = fopen(USERS_DB_FILE, "r");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buffer = malloc(length + 1);
    fread(buffer, 1, length, f);
    fclose(f);
    buffer[length] = '\0';

    cJSON *root = cJSON_Parse(buffer);
    if (!root) { free(buffer); return; }

    cJSON *user_json;
    int i = 0;
    cJSON_ArrayForEach(user_json, root) {
        if (i >= MAX_USERS) break;
        strcpy(USERS_DB[i].email, cJSON_GetObjectItem(user_json, "email")->valuestring);
        strcpy(USERS_DB[i].password_hash, cJSON_GetObjectItem(user_json, "password_hash")->valuestring);
        strcpy(USERS_DB[i].plan, cJSON_GetObjectItem(user_json, "plan")->valuestring);
        strcpy(USERS_DB[i].token, cJSON_GetObjectItem(user_json, "token")->valuestring);
        strcpy(USERS_DB[i].api_key, cJSON_GetObjectItem(user_json, "api_key")->valuestring);
        USERS_DB[i].plan_expiry_date = (long)cJSON_GetObjectItem(user_json, "plan_expiry_date")->valuedouble;
        i++;
    }
    USERS_COUNT = i;
    cJSON_Delete(root);
    free(buffer);
}
User* find_user_by_email(const char* email) {
    pthread_mutex_lock(&db_mutex);
    User* found_user = NULL;
    for (int i = 0; i < USERS_COUNT; i++) {
        if (strcmp(USERS_DB[i].email, email) == 0) {
            found_user = &USERS_DB[i];
            break;
        }
    }
    pthread_mutex_unlock(&db_mutex);
    return found_user;
}

// --- AUTHENTICATION & AUTHORIZATION (Unchanged) ---
User* authenticate_user(const cJSON* json) {
    const cJSON* email_json = cJSON_GetObjectItem(json, "email");
    const cJSON* token_json = cJSON_GetObjectItem(json, "token");
    if (!email_json || !token_json || !cJSON_IsString(email_json) || !cJSON_IsString(token_json)) return NULL;

    User* user = find_user_by_email(email_json->valuestring);
    if (user && strcmp(user->token, token_json->valuestring) == 0) {
        if (strcmp(user->plan, "pro") == 0 && time(NULL) > user->plan_expiry_date) {
            strcpy(user->plan, "free");
            save_users_db();
        }
        return user;
    }
    return NULL;
}
int is_pro_user(User* user) {
    return user != NULL && strcmp(user->plan, "pro") == 0;
}

// --- API HANDLERS ---
char* handle_list_sats() {
    cJSON *root = cJSON_CreateObject();
    cJSON *satellites = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "satellites", satellites);
    for (int i = 0; i < SATS_COUNT; ++i) {
        if (!SATS_DB[i].valid) continue;
        cJSON *sat = cJSON_CreateObject();
        cJSON_AddStringToObject(sat, "name", SATS_DB[i].name);
        cJSON_AddNumberToObject(sat, "altitude", SATS_DB[i].altitude);
        cJSON_AddNumberToObject(sat, "norad_id", SATS_DB[i].norad_id);
        cJSON_AddItemToArray(satellites, sat);
    }
    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);
    return json_string;
}

char* handle_filter_sats(const cJSON *json) {
    const cJSON *min_alt_json = cJSON_GetObjectItem(json, "min_alt");
    const cJSON *max_alt_json = cJSON_GetObjectItem(json, "max_alt");
    if (!min_alt_json || !max_alt_json || !cJSON_IsNumber(min_alt_json) || !cJSON_IsNumber(max_alt_json)) return NULL;

    double min_alt = min_alt_json->valuedouble;
    double max_alt = max_alt_json->valuedouble;

    cJSON *root = cJSON_CreateObject();
    cJSON *satellites = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "satellites", satellites);
    for (int i = 0; i < SATS_COUNT; ++i) {
        if (!SATS_DB[i].valid) continue;
        if (SATS_DB[i].altitude >= min_alt && SATS_DB[i].altitude <= max_alt) {
            cJSON *sat = cJSON_CreateObject();
            cJSON_AddStringToObject(sat, "name", SATS_DB[i].name);
            cJSON_AddNumberToObject(sat, "altitude", SATS_DB[i].altitude);
            cJSON_AddNumberToObject(sat, "norad_id", SATS_DB[i].norad_id);
            cJSON_AddItemToArray(satellites, sat);
        }
    }
    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);
    return json_string;
}

char* handle_risk_check(const cJSON* json) {
    const cJSON *target_alt_json = cJSON_GetObjectItem(json, "target_alt");
    const cJSON *tolerance_json = cJSON_GetObjectItem(json, "tolerance");
    if (!target_alt_json || !tolerance_json || !cJSON_IsNumber(target_alt_json) || !cJSON_IsNumber(tolerance_json)) return NULL;

    double target = target_alt_json->valuedouble;
    double tolerance = tolerance_json->valuedouble;

    cJSON *root = cJSON_CreateObject();
    cJSON *risks = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "risks", risks);
    int found = 0;
    for (int i = 0; i < SATS_COUNT; ++i) {
        if (!SATS_DB[i].valid) continue;
        if (fabs(SATS_DB[i].altitude - target) <= tolerance) {
            cJSON *risk_item = cJSON_CreateObject();
            cJSON_AddStringToObject(risk_item, "name", SATS_DB[i].name);
            cJSON_AddNumberToObject(risk_item, "altitude", SATS_DB[i].altitude);
            cJSON_AddNumberToObject(risk_item, "norad_id", SATS_DB[i].norad_id);
            cJSON_AddItemToArray(risks, risk_item);
            found = 1;
        }
    }
    cJSON_AddBoolToObject(root, "risk_found", found);
    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);
    return json_string;
}

char* handle_predict_collisions(const cJSON* json, User* user) {
    if (!is_pro_user(user)) { return strdup("{\"error\":\"This is a Pro feature. Please upgrade your plan.\"}"); }
    
    const cJSON *duration_json = cJSON_GetObjectItem(json, "duration");
    const cJSON *step_json = cJSON_GetObjectItem(json, "step");
    const cJSON *threshold_json = cJSON_GetObjectItem(json, "threshold");
    if (!duration_json || !step_json || !threshold_json || !cJSON_IsNumber(duration_json) || !cJSON_IsNumber(step_json) || !cJSON_IsNumber(threshold_json)) return NULL;

    int duration_days = duration_json->valueint;
    int time_step_min = step_json->valueint;
    double threshold_km = threshold_json->valuedouble;

    const double MIN_DIST_KM = 0.01;
    long duration_sec = duration_days * 86400;
    long step_sec = time_step_min * 60;
    time_t now = time(NULL);
    cJSON *root = cJSON_CreateObject();
    cJSON *events = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "events", events);
    for (int i = 0; i < SATS_COUNT; ++i) {
        if (!SATS_DB[i].valid) continue;
        for (int j = i + 1; j < SATS_COUNT; ++j) {
            if (!SATS_DB[j].valid) continue;
            if (is_same_system(SATS_DB[i].name, SATS_DB[j].name)) continue;
            double min_dist = 1e9, min_time = 0;
            for (long t = 0; t <= duration_sec; t += step_sec) {
                double sim_time = now + t;
                double x1, y1, z1, x2, y2, z2;
                propagate_orbit(&SATS_DB[i], sim_time, &x1, &y1, &z1);
                propagate_orbit(&SATS_DB[j], sim_time, &x2, &y2, &z2);
                double dist = sqrt(pow(x1 - x2, 2) + pow(y1 - y2, 2) + pow(z1 - z2, 2));
                if (dist < min_dist) {
                    min_dist = dist;
                    min_time = (double)t / 3600.0;
                }
            }
            if (min_dist < threshold_km && min_dist > MIN_DIST_KM) {
                cJSON *event = cJSON_CreateObject();
                cJSON_AddStringToObject(event, "object1_name", SATS_DB[i].name);
                cJSON_AddStringToObject(event, "object2_name", SATS_DB[j].name);
                cJSON_AddNumberToObject(event, "min_distance_km", min_dist);
                cJSON_AddNumberToObject(event, "time_from_now_hr", min_time);
                cJSON_AddItemToArray(events, event);
            }
        }
    }
    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);
    return json_string;
}

char* handle_safe_path(const cJSON* json, User* user) {
    if (!is_pro_user(user)) { return strdup("{\"error\":\"This is a Pro feature. Please upgrade your plan.\"}"); }
    
    const cJSON *target_alt_json = cJSON_GetObjectItem(json, "target_alt");
    if (!target_alt_json || !cJSON_IsNumber(target_alt_json)) return NULL;
    double target_alt = target_alt_json->valuedouble;

    #define MAX_ALTITUDE_BINS 1000
    #define ALTITUDE_BIN_SIZE 20
    int altitude_bins[MAX_ALTITUDE_BINS] = {0};
    for (int i = 0; i < SATS_COUNT; ++i) {
        if (!SATS_DB[i].valid || SATS_DB[i].altitude < 0) continue;
        int bin_index = (int)(SATS_DB[i].altitude / ALTITUDE_BIN_SIZE);
        if (bin_index >= 0 && bin_index < MAX_ALTITUDE_BINS) {
            altitude_bins[bin_index]++;
        }
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *analysis = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "analysis", analysis);
    int target_bin = (int)(target_alt / ALTITUDE_BIN_SIZE);
    int min_objects = -1;
    int safest_bin = -1;
    for (int i = target_bin - 5; i <= target_bin + 5; ++i) {
        if (i < 0 || i >= MAX_ALTITUDE_BINS) continue;
        cJSON *bin_data = cJSON_CreateObject();
        cJSON_AddNumberToObject(bin_data, "alt_start_km", i * ALTITUDE_BIN_SIZE);
        cJSON_AddNumberToObject(bin_data, "alt_end_km", (i + 1) * ALTITUDE_BIN_SIZE);
        cJSON_AddNumberToObject(bin_data, "object_count", altitude_bins[i]);
        cJSON_AddBoolToObject(bin_data, "is_target_bin", i == target_bin);
        cJSON_AddItemToArray(analysis, bin_data);
        if (min_objects == -1 || altitude_bins[i] < min_objects) {
            min_objects = altitude_bins[i];
            safest_bin = i;
        }
    }
    cJSON *recommendation = cJSON_CreateObject();
    cJSON_AddNumberToObject(recommendation, "safe_alt_start_km", safest_bin * ALTITUDE_BIN_SIZE);
    cJSON_AddNumberToObject(recommendation, "safe_alt_end_km", (safest_bin + 1) * ALTITUDE_BIN_SIZE);
    cJSON_AddNumberToObject(recommendation, "object_count", min_objects);
    cJSON_AddItemToObject(root, "recommendation", recommendation);
    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);
    return json_string;
}

// --- MODIFIED: Handler for mission details using real SATCAT data ---
char* handle_details(const cJSON* json) {
    const cJSON* norad_id_json = cJSON_GetObjectItem(json, "norad_id");
    if (!norad_id_json || !cJSON_IsNumber(norad_id_json)) return NULL;

    int norad_id = norad_id_json->valueint;
    SatCatData* sat_details = NULL;

    // Search for the satellite in our loaded SATCAT database
    for (int i = 0; i < SATCAT_COUNT; i++) {
        if (SATCAT_DB[i].norad_id == norad_id) {
            sat_details = &SATCAT_DB[i];
            break;
        }
    }

    if (sat_details == NULL) {
        return strdup("{\"error\":\"Details not found for this NORAD ID.\"}");
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "official_name", sat_details->official_name);
    cJSON_AddStringToObject(root, "launch_date", sat_details->launch_date);
    cJSON_AddStringToObject(root, "country", sat_details->country);
    cJSON_AddStringToObject(root, "purpose", sat_details->purpose);
    cJSON_AddStringToObject(root, "status", sat_details->status);

    char* json_string = cJSON_Print(root);
    cJSON_Delete(root);
    return json_string;
}


char* handle_signup(const cJSON* json) {
    const cJSON *email_json = cJSON_GetObjectItem(json, "email");
    const cJSON *password_json = cJSON_GetObjectItem(json, "password");
    if (!email_json || !password_json || !cJSON_IsString(email_json) || !cJSON_IsString(password_json)) return NULL;

    const char* email = email_json->valuestring;
    const char* password = password_json->valuestring;

    pthread_mutex_lock(&db_mutex);
    for (int i = 0; i < USERS_COUNT; i++) {
        if (strcmp(USERS_DB[i].email, email) == 0) {
            pthread_mutex_unlock(&db_mutex);
            return strdup("{\"error\":\"User with this email already exists.\"}");
        }
    }

    if (USERS_COUNT >= MAX_USERS) {
        pthread_mutex_unlock(&db_mutex);
        return strdup("{\"error\":\"Maximum number of users reached.\"}");
    }
    User* newUser = &USERS_DB[USERS_COUNT];
    strcpy(newUser->email, email);
    simple_hash(password, newUser->password_hash);
    strcpy(newUser->plan, "free");
    generate_random_string(newUser->token, 64);
    strcpy(newUser->api_key, "none");
    newUser->plan_expiry_date = 0;
    USERS_COUNT++;
    pthread_mutex_unlock(&db_mutex);
    
    save_users_db();
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "token", newUser->token);
    cJSON *user_json = cJSON_CreateObject();
    cJSON_AddStringToObject(user_json, "email", newUser->email);
    cJSON_AddStringToObject(user_json, "plan", newUser->plan);
    cJSON_AddItemToObject(root, "user", user_json);
    char* json_string = cJSON_Print(root);
    cJSON_Delete(root);
    return json_string;
}
char* handle_login(const cJSON* json) {
    const cJSON *email_json = cJSON_GetObjectItem(json, "email");
    const cJSON *password_json = cJSON_GetObjectItem(json, "password");
    if (!email_json || !password_json || !cJSON_IsString(email_json) || !cJSON_IsString(password_json)) return NULL;

    const char* email = email_json->valuestring;
    const char* password = password_json->valuestring;

    char password_hash[64];
    simple_hash(password, password_hash);
    
    User* user = find_user_by_email(email);
    if (user && strcmp(user->password_hash, password_hash) == 0) {
        generate_random_string(user->token, 64);
        save_users_db();
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "token", user->token);
        cJSON *user_json = cJSON_CreateObject();
        cJSON_AddStringToObject(user_json, "email", user->email);
        cJSON_AddStringToObject(user_json, "plan", user->plan);
        cJSON_AddItemToObject(root, "user", user_json);
        char* json_string = cJSON_Print(root);
        cJSON_Delete(root);
        return json_string;
    }
    return strdup("{\"error\":\"Invalid email or password.\"}");
}
char* handle_upgrade(User* user) {
    strcpy(user->plan, "pro");
    user->plan_expiry_date = time(NULL) + (30 * 24 * 60 * 60); // 30 days
    save_users_db();
    cJSON *root = cJSON_CreateObject();
    cJSON *user_json = cJSON_CreateObject();
    cJSON_AddStringToObject(user_json, "email", user->email);
    cJSON_AddStringToObject(user_json, "plan", user->plan);
    cJSON_AddItemToObject(root, "user", user_json);
    char* json_string = cJSON_Print(root);
    cJSON_Delete(root);
    return json_string;
}
char* handle_generate_key(User* user) {
    generate_random_string(user->api_key, 64);
    save_users_db();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "api_key", user->api_key);
    char* json_string = cJSON_Print(root);
    cJSON_Delete(root);
    return json_string;
}

// --- HTTP Server Implementation (Unchanged) ---
void parse_request(const char* request, char* method, char* path, char** body) {
    sscanf(request, "%s %s", method, path);
    *body = strstr(request, "\r\n\r\n");
    if (*body) {
        *body += 4;
    }
}
void send_response(int client_socket, const char* body) {
    if (body == NULL) return;
    char headers[BUFFER_SIZE];
    sprintf(headers, "HTTP/1.1 200 OK\r\n"
                      "Content-Type: application/json\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Content-Length: %zu\r\n"
                      "\r\n", strlen(body));
    write(client_socket, headers, strlen(headers));
    write(client_socket, body, strlen(body));
}
void send_options_response(int client_socket) {
    char response[512] = {0};
     sprintf(response, "HTTP/1.1 204 No Content\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
                      "Access-Control-Allow-Headers: Content-Type\r\n"
                      "Access-Control-Max-Age: 86400\r\n"
                      "\r\n");
    write(client_socket, response, strlen(response));
}
void send_error_response(int client_socket, int status_code, const char* message) {
    char headers[BUFFER_SIZE];
    char body[256];
    const char *status_text = "Bad Request";
    if (status_code == 401) status_text = "Unauthorized";
    if (status_code == 403) status_text = "Forbidden";
    if (status_code == 500) status_text = "Internal Server Error";

    sprintf(body, "{\"error\":\"%s\"}", message);
    sprintf(headers, "HTTP/1.1 %d %s\r\n"
                      "Content-Type: application/json\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Content-Length: %zu\r\n"
                      "\r\n", status_code, status_text, strlen(body));
    write(client_socket, headers, strlen(headers));
    write(client_socket, body, strlen(body));
}

void *handle_connection(void *socket_desc) {
    int sock = *(int*)socket_desc;
    free(socket_desc);
    char buffer[BUFFER_SIZE] = {0};

    read(sock, buffer, BUFFER_SIZE - 1);

    char method[16], path[256];
    char *body;
    parse_request(buffer, method, path, &body);
    printf("Thread %ld: Received request: %s %s\n", pthread_self(), method, path);

    if (strcmp(method, "OPTIONS") == 0) {
        send_options_response(sock);
    } else if (strcmp(method, "POST") == 0) {
        char* response_body = NULL;
        cJSON* json_body = cJSON_Parse(body);
        
        if (!json_body) {
            send_error_response(sock, 400, "Invalid JSON");
        } else {
            if (strcmp(path, "/signup") == 0) response_body = handle_signup(json_body);
            else if (strcmp(path, "/login") == 0) response_body = handle_login(json_body);
            else {
                User* user = authenticate_user(json_body);
                if (!user) {
                    send_error_response(sock, 401, "Authentication failed.");
                } else {
                    if (strcmp(path, "/list") == 0) response_body = handle_list_sats();
                    else if (strcmp(path, "/filter") == 0) response_body = handle_filter_sats(json_body);
                    else if (strcmp(path, "/risk") == 0) response_body = handle_risk_check(json_body);
                    else if (strcmp(path, "/details") == 0) response_body = handle_details(json_body);
                    else if (strcmp(path, "/predict") == 0) response_body = handle_predict_collisions(json_body, user);
                    else if (strcmp(path, "/plan") == 0) response_body = handle_safe_path(json_body, user);
                    else if (strcmp(path, "/upgrade") == 0) response_body = handle_upgrade(user);
                    else if (strcmp(path, "/generate-key") == 0) {
                        if (is_pro_user(user)) {
                            response_body = handle_generate_key(user);
                        } else {
                            send_error_response(sock, 403, "Forbidden: Pro plan required.");
                        }
                    } else {
                        response_body = strdup("{\"error\":\"Endpoint not found\"}");
                    }
                }
            }

            if (response_body) {
                if (response_body == NULL) {
                     send_error_response(sock, 400, "Missing or invalid parameters.");
                } else {
                    send_response(sock, response_body);
                    free(response_body);
                }
            }
            cJSON_Delete(json_body);
        }
    }
    
    close(sock);
    return 0;
}

int main(void) {
    srand(time(NULL));
    load_users_db();
    printf("Loaded %d users from %s\n", USERS_COUNT, USERS_DB_FILE);

    // --- TLE Data ---
    const char *live_tle_url = "https://celestrak.org/NORAD/elements/gp.php?GROUP=active&FORMAT=tle";
    const char *tle_filename = "tle_data.txt";
    printf("Downloading latest satellite TLE data...\n");
    if (!download_tle_file(live_tle_url, tle_filename)) {
        fprintf(stderr, "Failed to download live TLE data. Using local cache if available.\n");
    } else {
        printf("Live TLE data downloaded successfully.\n");
    }
    printf("Loading satellite TLE data from '%s'...\n", tle_filename);
    SATS_COUNT = load_tle_file(tle_filename, SATS_DB, MAX_SATS);
    if (SATS_COUNT < 0) {
        fprintf(stderr, "Error: could not open '%s'. Exiting.\n", tle_filename);
        return 1;
    }
    printf("Loaded %d satellite TLE entries.\n", SATS_COUNT);

    // --- NEW: SATCAT Data ---
    const char *live_satcat_url = "https://celestrak.org/pub/satcat.txt";
    const char *satcat_filename = "sat_data.txt";
    printf("Downloading latest SATCAT data...\n");
    if (!download_tle_file(live_satcat_url, satcat_filename)) {
        fprintf(stderr, "Failed to download live SATCAT data. Using local cache if available.\n");
    } else {
        printf("Live SATCAT data downloaded successfully.\n");
    }
    printf("Loading satellite catalog data from '%s'...\n", satcat_filename);
    SATCAT_COUNT = load_satcat_file(satcat_filename, SATCAT_DB, MAX_SATS);
    if (SATCAT_COUNT < 0) {
        fprintf(stderr, "Error: could not open '%s'. Details will not be available.\n", satcat_filename);
    } else {
        printf("Loaded %d SATCAT entries. Server is ready.\n", SATCAT_COUNT);
    }


    int server_fd;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);
    
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed"); exit(EXIT_FAILURE);
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt"); exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed"); exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 10) < 0) {
        perror("listen"); exit(EXIT_FAILURE);
    }
    printf("\nMulti-threaded server with Auth listening on port 8080...\n");
    
    while(1) {
        int new_socket;
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0) {
            perror("accept");
            continue;
        }
        pthread_t sniffer_thread;
        int *new_sock = malloc(sizeof(int));
        *new_sock = new_socket;

        if (pthread_create(&sniffer_thread, NULL, handle_connection, (void*) new_sock) < 0) {
            perror("could not create thread");
            free(new_sock);
        }
    }
    return 0;
}