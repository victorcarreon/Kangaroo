#include "freertos/semphr.h"

void http_client_task(void * param);
void get_symbols_task(void * param);

typedef struct Symbol {
    uint32_t id;
    char* symbol;
    char* name;
} symbol_t;