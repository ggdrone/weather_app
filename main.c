#include "weather_app.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
	fprintf(stderr, "Usage: %s <city>\n", argv[0]);
	return 1;
    }

    char city[64] = {0};
    int i;
    for (i = 1; i < argc; i++) {
	strcat(city, argv[i]);
	if (i < argc - 1) {
	    strcat(city, " ");
	}
    }

    // create wa object
    WEATHER_APP_T* app = wa_create();
    if (!app) {
	fprintf(stderr, "Failed to create weather app object.\n");
	return 2;
    }

    // Run workflow for city
    int rc = wa_run(app, city);

    wa_destroy(app);

    return rc;
}
