/* Respond to a button press.
 *
 * This code combines two ways of checking for a button press -
 * busy polling (the bad way) and button interrupt (the good way).
 *
 * This sample code is in the public domain.
 */
#include "espressif/esp_common.h"
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
const int gpioLED = 13;
const int gpio = 4;   /* gpio 0 usually has "PROGRAM" button attached */
const int active = 0; /* active == 0 for active low */
const gpio_inttype_t int_type = GPIO_INTTYPE_EDGE_ANY;
int ledON = 1;
#define GPIO_HANDLER gpio04_interrupt_handler

int http_post_request(int val) {
	const struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	struct addrinfo *res;

	printf("Running DNS lookup for %s...\r\n", WEB_SERVER);
	int err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);

	if(err != 0 || res == NULL) {
		printf("DNS lookup failed err=%d res=%p\r\n", err, res);
		if(res)
			freeaddrinfo(res);
		vTaskDelay(1000 / portTICK_RATE_MS);
		return -1;
	}
	/* Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
	struct in_addr *addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
	printf("DNS lookup succeeded. IP=%s\r\n", inet_ntoa(*addr));

	int s = socket(res->ai_family, res->ai_socktype, 0);
	if(s < 0) {
		printf("... Failed to allocate socket.\r\n");
		freeaddrinfo(res);
		vTaskDelay(1000 / portTICK_RATE_MS);
		return -1;
	}

	printf("... allocated socket\r\n");

	if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
		close(s);
		freeaddrinfo(res);
		printf("... socket connect failed.\r\n");
		vTaskDelay(4000 / portTICK_RATE_MS);
		return -1;
	}

	printf("... connected\r\n");
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
	printf("... socket send success\r\n");
		
	printf("POST_REQUEST:\n%s", req);

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
    printf("Waiting for button press interrupt on gpio %d...\r\n", gpio);
    xQueueHandle *tsqueue = (xQueueHandle *)pvParameters;
    gpio_set_interrupt(gpio, int_type);

    while(1) {
        uint8_t val;
        xQueueReceive(*tsqueue, &val, portMAX_DELAY);
		if (http_post_request(val) != 0) {
			printf("ERROR: HTTP_POST Request failed :(\n");
		}
    }
}

static xQueueHandle tsqueue;

void GPIO_HANDLER(void)
{
    uint8_t val = ledON;
	gpio_write(gpioLED, ledON);
	ledON = !ledON;
    xQueueSendToBackFromISR(tsqueue, &val, NULL);
}

void user_init(void)
{
    uart_set_baud(0, 115200);
    printf("SDK version:%s\n", sdk_system_get_sdk_version());
    gpio_enable(gpio, GPIO_INPUT);
    gpio_enable(gpioLED, GPIO_OUTPUT);

    struct sdk_station_config config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASS,
    };

    /* required to call wifi_set_opmode before station_set_config */
    sdk_wifi_set_opmode(STATION_MODE);
    sdk_wifi_station_set_config(&config);

    tsqueue = xQueueCreate(2, sizeof(uint8_t));
    xTaskCreate(gpioIntTask, (signed char *)"gpioIntTask", 256, &tsqueue, 2, NULL);
}