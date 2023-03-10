

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "http_client.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "secret.h"

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048

static const char *TAG = "http_client";

SemaphoreHandle_t xHttpClientSemaphore = NULL;
static bool symbols_ready = false;

static esp_err_t _http_event_handler(esp_http_client_event_t *evt);
static void ParseResponse(void);
static char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};
static symbol_t* symbols = NULL;
static uint32_t symbol_count = 0;
/**
* NOTE: All the configuration parameters for http_client must be spefied either in URL or as host and path parameters.
* If host and path parameters are not set, query parameter will be ignored. In such cases,
* query parameter should be specified in URL.
*
* If URL as well as host and path parameters are specified, values of host and path will be considered.
*/
static esp_http_client_config_t kangaroo_config = {
    .host = "192.168.1.77",
    .path = "/symbols",
    .event_handler = _http_event_handler,
    .user_data = local_response_buffer,        // Pass address of local buffer to get response
    .disable_auto_redirect = true,
    .port = 8000,
};

static esp_http_client_config_t finhub_config = {
    .host = "https://finhub.io",
    .path = "/api/v1",
    .event_handler = _http_event_handler,
    .user_data = local_response_buffer,        // Pass address of local buffer to get response
    .disable_auto_redirect = true,
    .crt_bundle_attach = esp_crt_bundle_attach,
};

void http_client_task(void * param)
{
    char api_buffer[256] = "";
    while (1) 
    {
        
        if (xSemaphoreTake( xHttpClientSemaphore, pdMS_TO_TICKS(1000) ) == pdTRUE )
        {
            if(symbols_ready)
            {
                //Iterate through symbols
                //for each symbol
                for (uint32_t i = 0; i < symbol_count; i++) 
                {
                    snprintf(api_buffer, 256, "https://finnhub.io/api/v1/quote?symbol=%s&token=%s", symbols[i].symbol, finhub_token);
                    
                    esp_http_client_handle_t client = esp_http_client_init(&finhub_config);
                    esp_http_client_set_url(client, &api_buffer[0]);
                    esp_err_t err = esp_http_client_perform(client);
                    if (err == ESP_OK) 
                    {
                        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %lld",
                                esp_http_client_get_status_code(client),
                                esp_http_client_get_content_length(client));

                        ESP_LOG_BUFFER_CHAR(TAG, local_response_buffer, strlen(local_response_buffer));

                        cJSON *root = cJSON_Parse(local_response_buffer);
                        ESP_LOGI(TAG, "*******%s Data*******", symbols[i].name );
                        ESP_LOGI(TAG, "Current price %lf", cJSON_GetObjectItem(root, "c")->valuedouble);
                        ESP_LOGI(TAG, "High of day %lf", cJSON_GetObjectItem(root, "h")->valuedouble);
                        ESP_LOGI(TAG, "low of day %lf", cJSON_GetObjectItem(root, "l")->valuedouble);
                        ESP_LOGI(TAG, "open price %lf", cJSON_GetObjectItem(root, "o")->valuedouble);
                        ESP_LOGI(TAG, "previous close %lf", cJSON_GetObjectItem(root, "pc")->valuedouble);
                        ESP_LOGI(TAG, "timestamp %d", cJSON_GetObjectItem(root, "t")->valueint);

                        esp_http_client_cleanup(client);

                    } 
                    else 
                    {
                        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
                    }
                        memset(api_buffer, 0, 256);
                }
            }

            if (xSemaphoreGive( xHttpClientSemaphore ) != pdTRUE)
                ESP_LOGI(TAG, "Error giving xHttpClientSemaphore");
        }
        else
        {
            //not ready to send http requests
            ESP_LOGI(TAG, "Not Ready to send http requests");
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void get_symbols_task(void * param)
{
    

    while (1) 
    {
        //IF REQUEST TO SERVER WAS
        //SUCCESSFUL symbols_ready , SLEEP THE THREAD FOR 24 hrs.
        if (!symbols_ready)
        {
            if (xSemaphoreTake( xHttpClientSemaphore, pdMS_TO_TICKS(1000) ) == pdTRUE )
            {
                //Make HTTP CALL TO GET SYMBOLS FROM SERVER
                
                free(symbols);

                esp_http_client_handle_t client = esp_http_client_init(&kangaroo_config);
                esp_http_client_set_header(client, "Content-Type", "application/json");
                // GET
                esp_err_t err = esp_http_client_perform(client);
                if (err == ESP_OK) 
                {
                    ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %lld",
                            esp_http_client_get_status_code(client),
                            esp_http_client_get_content_length(client));

                    ESP_LOG_BUFFER_CHAR(TAG, local_response_buffer, strlen(local_response_buffer));

                    ParseResponse();
                    symbols_ready = true;
                } 
                else 
                {
                    ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
                }
                
                esp_http_client_cleanup(client);
                memset(local_response_buffer, 0, MAX_HTTP_OUTPUT_BUFFER);

                if (xSemaphoreGive( xHttpClientSemaphore ) != pdTRUE)
                    ESP_LOGI(TAG, "Error giving xHttpClientSemaphore");


            }
            else
            {
                //not ready to send http requests
                ESP_LOGI(TAG, "Not Ready to send http requests");
            }

        }
        
    }
}

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else {
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                //ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;

        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            esp_http_client_set_header(evt->client, "From", "user@example.com");
            esp_http_client_set_header(evt->client, "Accept", "text/html");
            esp_http_client_set_redirection(evt->client);
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void ParseResponse()
{
    cJSON *root = cJSON_Parse(local_response_buffer);
    symbol_count = cJSON_GetArraySize(root);

    cJSON *elem;


    symbols = (symbol_t*)malloc(symbol_count * sizeof(symbol_t));
    if (symbols == NULL)
        ESP_LOGE(TAG, "Unable to allocate memory for symbols");

    for (uint32_t i = 0; i < symbol_count; i++) 
    {
        elem = cJSON_GetArrayItem(root, i);
        symbols[i].id = cJSON_GetObjectItem(elem, "id")->valueint;
        symbols[i].symbol = cJSON_GetObjectItem(elem, "symbol")->valuestring;
        symbols[i].name = cJSON_GetObjectItem(elem, "name")->valuestring;
    }

    
}