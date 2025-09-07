#include "weather_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h> // JSON parser
#include <curl/curl.h>   // HTTP client

struct WEATHER_APP_T {
    char city[32];
    double lat;
    double lon;
    double temperature;
    int relative_humidity;
    char last_error[128];
};

// THESE ARE HELPER FUNCTIONS HIDDEN FROM main.c
// ----------------------------------------------
// Fills up with data that write_callback recives 
struct MEMORY_BUFFER_T {
    char *data;
    size_t size;
};

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    printf("Recived data chunk is size: %lu bytes\n", total);

    struct MEMORY_BUFFER_T* MEM_T = userp;
    // Grow buffer with the size of the recived chunk
    char *ptr = realloc(MEM_T->data, MEM_T->size + total + 1);
    if (!ptr) {
	printf("Returning 0 to CURL!\n");
	return 0;
    }
    MEM_T->data = ptr;

    // Copies the new data into the buffer
    // Data points to the start off the buffer
    // Size says how many bytes already exists
    // New contents gets added to the end off buffer
    memcpy(&(MEM_T->data[MEM_T->size]), contents, total);
    MEM_T->size += total;
    MEM_T->data[MEM_T->size] = 0;

    return total; // Tells curl we handled all the bytes
}

static int http_get(const char* url, char** out, size_t* out_len) {
    // Get curl handle
    CURL* curl = curl_easy_init();
    if (!curl) return -1;

    struct MEMORY_BUFFER_T CHUNK_T =  {0};

    // Configuring CURL
    curl_easy_setopt(curl, CURLOPT_URL, url); // Pases URL
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback); // Takes control of stream
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&CHUNK_T); // This becomes userp
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects 
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "weather_app/1.0"); // Apps name 
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // Timeout

    // This is the actual call for work
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
	fprintf(stderr, "curl perform failed %s", curl_easy_strerror(res));
	curl_easy_cleanup(curl);
	free(CHUNK_T.data);
	return -1;
    }

    // Cleanup
    curl_easy_cleanup(curl);

    // Returns to caller
    *out = CHUNK_T.data;
    *out_len = CHUNK_T.size;
    return 0;
}

// === JSON parser: Geoapify ===
// Extract latitude and longitude from the Geoapify response.
// Example response (simplified):
// { "features": [ { "properties": { "lat": 59.3, "lon": 18.0 } } ] }

static int parse_geoapify_json(const char* json_str, double* lat, double* lon) {
    struct json_object* parsed = json_tokener_parse(json_str);
    if (!parsed) return -1;

    struct json_object *features;
    if (!json_object_object_get_ex(parsed, "features", &features)) {
        json_object_put(parsed);
        return -1;
    }

    if (json_object_array_length(features) == 0) {
        json_object_put(parsed);
        return -1; // no results
    }

    struct json_object *first = json_object_array_get_idx(features, 0);

    // --- Try "properties.lat" / "properties.lon" ---
    struct json_object *properties;
    if (json_object_object_get_ex(first, "properties", &properties)) {
        struct json_object *jlat, *jlon;
        if (json_object_object_get_ex(properties, "lat", &jlat) &&
            json_object_object_get_ex(properties, "lon", &jlon)) {
            *lat = json_object_get_double(jlat);
            *lon = json_object_get_double(jlon);
            json_object_put(parsed);
            return 0;
        }
    }

    // --- Fallback: geometry.coordinates [lon, lat] ---
    struct json_object *geometry;
    if (json_object_object_get_ex(first, "geometry", &geometry)) {
        struct json_object *coords;
        if (json_object_object_get_ex(geometry, "coordinates", &coords) &&
            json_object_array_length(coords) >= 2) {
            *lon = json_object_get_double(json_object_array_get_idx(coords, 0));
            *lat = json_object_get_double(json_object_array_get_idx(coords, 1));
            json_object_put(parsed);
            return 0;
        }
    }

    json_object_put(parsed);
    return -1; // neither worked
}


// === JSON parser: Open-Meteo ===
// Extract temperature and humidity.
// Example response (simplified):
// { "current": { "temperature_2m": 17.2, "relative_humidity_2m": 62 } }
static int parse_openmeteo_json(const char* json_str, double* t, int* rh) {
    struct json_object *parsed = json_tokener_parse(json_str);
    if (!parsed) return -1;

    struct json_object *current;
    if (!json_object_object_get_ex(parsed, "current", &current)) {
        json_object_put(parsed);
        return -1;
    }

    struct json_object *jtemp, *jrh;
    if (json_object_object_get_ex(current, "temperature_2m", &jtemp)) {
        *t = json_object_get_double(jtemp);
    }
    if (json_object_object_get_ex(current, "relative_humidity_2m", &jrh)) {
        *rh = json_object_get_int(jrh);
    }

    json_object_put(parsed);
    return 0;
}

WEATHER_APP_T* wa_create(void) {
    return calloc(1, sizeof(struct WEATHER_APP_T));
}

void wa_destroy(WEATHER_APP_T* app) {
    free(app);
}

// === Public API: main workflow ===
// This is where all the pieces come together:
// - we call http_get() (which calls curl)
// - curl calls write_callback()
// - we parse JSON with json-c
// - and we store results in the WeatherApp struct
int wa_run(WEATHER_APP_T* app, const char* city_arg) {
    if (!app || !city_arg) {
        fprintf(stderr, "Invalid arguments to WeatherApp_run\n");
        return 1;
    }

    // Copy city name into the struct
    strncpy(app->city, city_arg, sizeof(app->city) - 1);
    app->city[sizeof(app->city) - 1] = '\0';

    // Build Geoapify URL
    const char* api_key = getenv("GEOAPIFY_API_KEY");
    if (!api_key) {
        fprintf(stderr, "Missing GEOAPIFY_API_KEY environment variable\n");
        return 2;
    }

    // Escape city name for safe URL usage
    char *escaped_city = curl_easy_escape(curl_easy_init(), app->city, 0);
    if (!escaped_city) {
	fprintf(stderr, "Failed to URL-encode city name\n");
    return 2;
    }

    char geo_url[256];
    snprintf(geo_url, sizeof geo_url,
             "https://api.geoapify.com/v1/geocode/search?text=%s&apiKey=%s",
             escaped_city, api_key);

    curl_free(escaped_city);

    // Fetch Geoapify response
    char *geo_json = NULL;
    size_t geo_len = 0;
    if (http_get(geo_url, &geo_json, &geo_len) != 0) {
        fprintf(stderr, "Failed to fetch Geoapify data\n");
        return 3;
    }

    // Parse lat/lon
    if (parse_geoapify_json(geo_json, &app->lat, &app->lon) != 0) {
        fprintf(stderr, "Failed to parse Geoapify JSON\n");
        free(geo_json);
        return 4;
    }
    free(geo_json);

    // Build Open-Meteo URL
    char meteo_url[256];
    snprintf(meteo_url, sizeof meteo_url,
             "https://api.open-meteo.com/v1/forecast?latitude=%f&longitude=%f&current=temperature_2m,relative_humidity_2m",
             app->lat, app->lon);

    // Fetch Open-Meteo response
    char *meteo_json = NULL;
    size_t meteo_len = 0;
    if (http_get(meteo_url, &meteo_json, &meteo_len) != 0) {
        fprintf(stderr, "Failed to fetch Open-Meteo data\n");
        return 5;
    }

    // Parse temperature and humidity
    if (parse_openmeteo_json(meteo_json, &app->temperature, &app->relative_humidity) != 0) {
        fprintf(stderr, "Failed to parse Open-Meteo JSON\n");
        free(meteo_json);
        return 6;
    }
    free(meteo_json);

    // Print final report
    printf("\nWeather report for %s:\n", app->city);
    printf("  Coordinates: (%.4f, %.4f)\n", app->lat, app->lon);
    printf("  Temperature: %.1f °C\n", app->temperature);
    printf("  Humidity:    %d %%\n", app->relative_humidity);

    return 0;
}
