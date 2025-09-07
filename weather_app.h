#ifndef WEATHER_APP_H
#define WEATHER_APP_H

typedef struct WEATHER_APP_T WEATHER_APP_T;

// Public API
WEATHER_APP_T* wa_create(void);
void wa_destroy(WEATHER_APP_T*);
int wa_run(WEATHER_APP_T*, const char* city_arg);

#endif
