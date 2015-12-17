/* Respond to a button press.
 *
 * This code combines two ways of checking for a button press -
 * busy polling (the bad way) and button interrupt (the good way).
 *
 * This sample code is in the public domain.
 */
#include "espressif/esp_common.h"
#include "espressif/esp_system.h"
#include "esp/uart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "esp8266.h"

#include <string.h>

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "ssid_config.h"

#define WEB_SERVER "api.thingspeak.com"
#define WEB_PORT "80"
#define WEB_URL "https://api.thingspeak.com/update"
#define API_KEY "S9SNYGVX6RV4EBZL"

static char fields[128];
static char req[1024];

/* pin config */
//const int gpioLED = 13;

const int gpio_r = 12;
const int gpio_g = 13;
const int gpio_b = 14;


const int gpioIN = 4;  
const gpio_inttype_t int_type = GPIO_INTTYPE_EDGE_POS;
#define GPIO_HANDLER gpio04_interrupt_handler

int http_post_request(int val) {

    gpio_write(gpio_b, 1);
    gpio_write(gpio_g, 0);

	const struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	struct addrinfo *res;

	//printf("Running DNS lookup for %s...\r\n", WEB_SERVER);
	int err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);

	if(err != 0 || res == NULL) {
		printf("DNS lookup failed err=%d res=%p\r\n", err, res);
		if(res)
			freeaddrinfo(res);
		vTaskDelay(1000 / portTICK_RATE_MS);
		return -1;
	}
	/* Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
	//struct in_addr *addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
	//printf("DNS lookup succeeded. IP=%s\r\n", inet_ntoa(*addr));

	int s = socket(res->ai_family, res->ai_socktype, 0);
	if(s < 0) {
		printf("... Failed to allocate socket.\r\n");
		freeaddrinfo(res);
		vTaskDelay(1000 / portTICK_RATE_MS);
		return -1;
	}

	//printf("... allocated socket\r\n");

	if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
		close(s);
		freeaddrinfo(res);
		printf("... socket connect failed.\r\n");
		vTaskDelay(4000 / portTICK_RATE_MS);
		return -1;
	}

	//printf("... connected\r\n");
	freeaddrinfo(res);

	const char *req_format =
						"POST /update HTTP/1.1\n"
						"Host: api.thingspeak.com\n"
						"Connection: close\n"
						"X-THINGSPEAKAPIKEY: "API_KEY"\n"
						"Content-Type: application/x-www-form-urlencoded\n"
						"Content-Length: %d\n"
						"\n"
						"%s\n"
						"\n";

	bzero(fields, 128);
	bzero(req, 1024);
	sprintf(fields, "field1=%d", val);
	sprintf(req, req_format, strlen(fields), fields);
	
	if (write(s, req, strlen(req)) < 0) {
		printf("... socket send failed\r\n");
		close(s);
		vTaskDelay(4000 / portTICK_RATE_MS);
		return -1;
	}
	//printf("... socket send success\r\n");
		
	//printf("POST_REQUEST:\n%s", req);
	//printf("field1: %d\n", val);

	static char recv_buf[128];
	int r = 0;
	do {
		bzero(recv_buf, 128);
		r = read(s, recv_buf, 127);
		if(r > 0) {
			printf("%s", recv_buf);
		}
	} while(r > 0);
	printf("\n");
	
	close(s);

	return 0;
}

/* This task configures the GPIO interrupt and uses it to tell
   when the button is pressed.

   The interrupt handler communicates the exact button press time to
   the task via a queue.

   This is a better example of how to wait for button input!
*/
void gpioIntTask(void *pvParameters)
{
    printf("Waiting for button press interrupt on gpio %d...\r\n", gpioIN);
    xQueueHandle *tsqueue = (xQueueHandle *)pvParameters;

    while(1) {
        uint8_t val;
        xQueueReceive(*tsqueue, &val, portMAX_DELAY);
		printf("val: %d\n", val);
		if (http_post_request(val) != 0) {
			printf("ERROR: HTTP_POST Request failed :(\n");
		}
    }
}

static xQueueHandle tsqueue;

void GPIO_HANDLER(void)
{
    // Reads from gpio
    //uint8_t val = gpio_read(gpioIN);
	//gpio_write(gpio_r, val);
	//printf("val: %d\n", val);
    //xQueueSendToBackFromISR(tsqueue, &val, NULL);
	//gpio_write(gpio_r, !val);
}

void analogTask(void *pvParameters) {
    bool visible_now  = false;
    bool visible_prev = false;
	int  threshold = 512;
    while (1) {
        visible_now  = false;
		uint16_t ad  = sdk_system_adc_read();
        printf("Analog read value: %u ", ad);
		if (ad <= threshold) {
            visible_now = true;
			//printf("====== read analog ======\n");
			printf("BELOW THRESHOLD ");
		}
        gpio_write(gpio_r, visible_now);
        if (visible_prev != visible_now) {
            if (visible_now == true) {
                printf("Marker +++++ START +++++ detected!");
            }
            if (visible_now == false) {
                printf("Marker ----- STOP  ----- detected!");
            }            
            visible_prev = visible_now;
        }
        //xQueueSendToBackFromISR(tsqueue, &val, NULL);
        fputs(visible_now ? "true" : "false", stdout);
        printf("\n");
        vTaskDelay(200 / portTICK_RATE_MS);		
	}
}

void user_init(void)
{
    uart_set_baud(0, 115200);

    printf("SDK version:%s\n", sdk_system_get_sdk_version());
    
    gpio_enable(gpioIN, GPIO_INPUT);
    gpio_set_interrupt(gpioIN, int_type);

    //gpio_enable(gpioLED, GPIO_OUTPUT);
    gpio_enable(gpio_r, GPIO_OUTPUT);
    gpio_enable(gpio_g, GPIO_OUTPUT);
    gpio_enable(gpio_b, GPIO_OUTPUT);


    struct sdk_station_config config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASS,
    };

    /* required to call wifi_set_opmode before station_set_config */
    sdk_wifi_set_opmode(STATION_MODE);
    sdk_wifi_station_set_config(&config);

    tsqueue = xQueueCreate(5, sizeof(uint8_t));
    xTaskCreate(gpioIntTask, (signed char *)"gpioIntTask", 256, &tsqueue, 2, NULL);
    xTaskCreate(analogTask,  (signed char *)"analogTask",  256, &tsqueue, 2, NULL);
}
