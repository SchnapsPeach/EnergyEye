/*
 * HSRM IoT Project: EnergyEye - a DIY Smart Meter
 *
 * Reading an rotational electricity meter using an optical infrared (IR)
 * reflection sensor.
 *
 * This code is based on the "button" example contained in the esp-open-rtos
 * framework.
 *
 * Dependencies:
 * esp-open-rtos - https://github.com/SuperHouse/esp-open-rtos
 * esp-open-sdk  - https://github.com/pfalcon/esp-open-sdk/
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


// Reference to the wireless network configuration (not checked in to gitHub).
#include "ssid_config.h"


// Thingspeak IoT data push service information (public access).
//#define  WEB_SERVER "api.thingspeak.com"
//#define  WEB_PORT   "80"
//#define  WEB_URL    "https://api.thingspeak.com/update"
// Reference to the thingspeak API key (not checked in to gitHub).
//#include "thingspeak_api_key.h"

//http://demo.volkszaehler.org/middleware.php/data/4f5687c0-bb8a-11e5-88ef-4731a4ea7f5b.json?operation=add&value=2
#define  WEB_SERVER "demo.volkszaehler.org"
#define  WEB_PORT   "80"
#define  WEB_URL    "http://demo.volkszaehler.org/middleware.php/data/"

// Volkszaehler channel UUID variable configurable at runtime
// TODO: Sanity checks length, format, ... ?
char vzChannelUUID[] = "4f5687c0-bb8a-11e5-88ef-4731a4ea7f5b";


static char valueFields[ 128];
static char httpRequest[1024];

// Pin configuration on the ESP8266 Model ESP-12E.

// A RGB LED is used as a status display.
const int gpio_r = 12;
const int gpio_g = 13;
const int gpio_b = 14;
//const int gpioLED = 13;

// GPIO input configuration and interrupt attachment
const int gpioIN = 4;  
const gpio_inttype_t int_type = GPIO_INTTYPE_EDGE_POS;
#define GPIO_HANDLER gpio04_interrupt_handler


// Queue definition to communicate between tasks
static xQueueHandle tsqueue;
static xQueueHandle analogValueQueue;

// Default analog read threshold is half of the ADC range.
int analogThreshold   = 512;

// Electricity meter calibration factor in Revolutions per kWh
//  Socket rotational electricity meter:  600 Revolutions per kWh (default here)
//   House rotational electricity meter:   75 Revolutions per kWh
//            Digital electricity meter: 1000    Impulses per kWh
int calibrationFactor = 600;



// This function executes a HTTP POST request with a numeric value as payload. 

// int http_post_request(int val) {
    
//     // Turn on blue LED to indicate send procedure.
//     gpio_write(gpio_b, 1);
    
//     const struct addrinfo hints = {
//         .ai_family = AF_INET,
//         .ai_socktype = SOCK_STREAM,
//     };
//     struct addrinfo *res;
    
//     //printf("Running DNS lookup for %s...\r\n", WEB_SERVER);
//     int err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);
    
//     if(err != 0 || res == NULL) {
//         printf("DNS lookup failed err=%d res=%p\r\n", err, res);
//         if(res)
//             freeaddrinfo(res);
//         vTaskDelay(1000 / portTICK_RATE_MS);
//         return -1;
//     }
//     /* Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
//     //struct in_addr *addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
//     //printf("DNS lookup succeeded. IP=%s\r\n", inet_ntoa(*addr));
    
//     int s = socket(res->ai_family, res->ai_socktype, 0);
//     if(s < 0) {
//         printf("... Failed to allocate socket.\r\n");
//         freeaddrinfo(res);
//         vTaskDelay(1000 / portTICK_RATE_MS);
//         return -1;
//     }
    
//     //printf("... allocated socket\r\n");
    
//     if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
//         close(s);
//         freeaddrinfo(res);
//         printf("... socket connect failed.\r\n");
//         vTaskDelay(4000 / portTICK_RATE_MS);
//         return -1;
//     }
    
//     //printf("... connected\r\n");
//     freeaddrinfo(res);
    
//     const char *req_format =
// 						"POST /update HTTP/1.1\n"
// 						"Host: api.thingspeak.com\n"
// 						"Connection: close\n"
// 						"X-THINGSPEAKAPIKEY: "API_KEY"\n"
// 						"Content-Type: application/x-www-form-urlencoded\n"
// 						"Content-Length: %d\n"
// 						"\n"
// 						"%s\n"
// 						"\n";

// 	bzero(fields, 128);
// 	bzero(req, 1024);
// 	sprintf(fields, "field1=%d", val);
// 	sprintf(req, req_format, strlen(fields), fields);
	
// 	if (write(s, req, strlen(req)) < 0) {
// 		printf("... socket send failed\r\n");
// 		close(s);
// 		vTaskDelay(4000 / portTICK_RATE_MS);
// 		return -1;
// 	}
//     printf("... socket send success\r\n");


// 	static char recv_buf[128];
// 	int r = 0;
// 	do {
// 		bzero(recv_buf, 128);
// 		r = read(s, recv_buf, 127);
// 		if(r > 0) {
// 			printf("%s", recv_buf);
// 		}
// 	} while(r > 0);
// 	printf("\n");
	
// 	close(s);

// 	return 0;
// }



// Simple HTTP GET string to post something to the volkszaehler TestKanal
//http://demo.volkszaehler.org/middleware.php/data/4f5687c0-bb8a-11e5-88ef-4731a4ea7f5b.json?operation=add&value=2
// based on HTTP GET example from esp-open-rtos toolkit

int http_get_request(int val) {
    
    // Turn blue LED on to indicate send procedure.
    gpio_write(gpio_b, 1);
    
    printf("[HTTP GET] HTTP GET transmission starting...\r\n");
    
    // ???
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    
    printf("[HTTP GET] Running DNS lookup for %s...\r\n", WEB_SERVER);
    int err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);
    
    if(err != 0 || res == NULL) {
        printf("[HTTP GET] DNS lookup failed err=%d res=%p\r\n", err, res);
        if(res)
            freeaddrinfo(res);
        vTaskDelay(1000 / portTICK_RATE_MS);
        return -1;
    }
    /* Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
    struct in_addr *addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    printf("[HTTP GET] DNS lookup succeeded. IP=%s\r\n", inet_ntoa(*addr));
    
    int s = socket(res->ai_family, res->ai_socktype, 0);
    if(s < 0) {
        printf("[HTTP GET] ... Failed to allocate socket.\r\n");
        freeaddrinfo(res);
        vTaskDelay(1000 / portTICK_RATE_MS);
        return -1;
    }
    
    printf("[HTTP GET] ... allocated socket\r\n");
    
    if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        close(s);
        freeaddrinfo(res);
        printf("[HTTP GET] ... socket connect failed.\r\n");
        vTaskDelay(4000 / portTICK_RATE_MS);
        return -1;
    }
    
    printf("[HTTP GET] ... connected\r\n");
    freeaddrinfo(res);
    
    // Building HTTP GET request string
    // const char *req_format =
    //                     "POST /update HTTP/1.1\n"
    //                     "Host: api.thingspeak.com\n"
    //                     "Connection: close\n"
    //                     "X-THINGSPEAKAPIKEY: "API_KEY"\n"
    //                     "Content-Type: application/x-www-form-urlencoded\n"
    //                     "Content-Length: %d\n"
    //                     "\n"
    //                     "%s\n"
    //                     "\n";
    
    // TODO: Error handling: Validate that channel exists (UUID)
    // TODO: send timestamp if there is a problem sending...
    
    const char *requestStructure =
            "GET"
            " "
            "http://demo.volkszaehler.org/middleware.php/data/"
            "%s"
            ".json?operation=add&value="
            "%d"
            "\r\n"
            "User-Agent: esp-open-rtos/0.1 esp8266"
            "\r\n"
            "\r\n"
            ;
    
    // Zero the memory area where the stringbuffers reside.
    memset(valueFields,0 ,  128);
    memset(httpRequest,0 , 1024);
    
    // Write the energy value into the designated stringbuffer.
    sprintf(valueFields, "%d", val);
    
    //printf(valueFields);
    
    // Write the stringbuffer 'requestStructure' into the stringbuffer 'httpRequest'
    // and fill out the placeholder %s with the 'vzChannelUUID' stringbuffer
    // and fill out the placeholder %d with the 'valueFields' integer varaible
    //sprintf(httpRequest, requestStructure, valueFields); // BUG: DOES STRANGE THINGS!
    sprintf(httpRequest, requestStructure, vzChannelUUID, val);
    
    printf("[HTTP GET] REQUEST:\r\n%s\r\n", httpRequest);
    
    if (write(s, httpRequest, strlen(httpRequest)) < 0) {
        printf("[HTTP GET] ... socket send failed\r\n");
        close(s);
        //vTaskDelay(4000 / portTICK_RATE_MS);
        return -1;
    }
    printf("[HTTP GET] ... socket send success\r\n");
    
    static char recv_buf[128];
    int r = 0;
    do {
        bzero(recv_buf, 128);
        r = read(s, recv_buf, 127);
        if(r > 0) {
            printf("[HTTP GET] Received:\r\n%s\r\n", recv_buf);
        }
    } while(r > 0);
    printf("[HTTP GET] ... done reading from socket. Last read return=%d errno=%d\r\n", r, errno);printf("\n");
    
    close(s);
    
    // Turn blue LED off to indicate send procedure.
    gpio_write(gpio_b, 0);
    
    return 0;
}



/* This task configures the GPIO interrupt and uses it to tell
 * when the button is pressed.
 *
 * The interrupt handler communicates the exact button press time to
 * the task via a queue.
 *
 * This is a better example of how to wait for button input!
 */

// This task receives the GPIO button press values from the interrupt
// service routine via the communication queue.

// void gpioIntTask(void *pvParameters)
// {
//     printf("Waiting for button press interrupt on gpio %d...\r\n", gpioIN);
//     xQueueHandle *tsqueue = (xQueueHandle *)pvParameters;

//     while(1) {
//         uint8_t val;
//         xQueueReceive(*tsqueue, &val, portMAX_DELAY);
// 		printf("val: %d\n", val);
// 		if (http_post_request(val) != 0) {
// 			printf("ERROR: HTTP_POST Request failed :(\n");
// 		}
//     }
// }



// This is the interrupt service routine listening for a button press

// void GPIO_HANDLER(void)
// {
//     // Reads from gpio
//     //uint8_t val = gpio_read(gpioIN);
//     //gpio_write(gpio_r, val);
//     //printf("val: %d\n", val);
//     //xQueueSendToBackFromISR(tsqueue, &val, NULL);
//     //gpio_write(gpio_r, !val);
// }



// This task reads an analog value coming from the IR reflection sensor
// Currently, the analog pin is polled every 20 milliseconds for a new value.

void analogReadTask(void *pvParameters) {
    
    bool      visible_now          = false;
    bool      visible_prev         = false;
    // Threshold is defined globally to be configurable from outside.
    //int       analogThreshold      = 512;
    
    int       markerSeenCount       = 0;
    uint32_t  tickCountStart        = 0;
    uint32_t  tickCountStop         = 0;
    uint32_t  markerVisibleTime     = 0;
    uint32_t  markerInvisibleTime   = 0;
    uint32_t  markerRevolutionTime  = 0;
    
    while (1) {
        
        visible_now = false;
		uint16_t ad = sdk_system_adc_read();
        
        // DEBUG:
        //printf("Analog read value: %u ", ad);
        //printf("\n");
        //printf("portTICK_RATE_MS is: %u ", portTICK_RATE_MS);
        //printf("\n");
        // INFO: portTICK_RATE_MS is 10
        //printf("portTICK_PERIOD_MS is: %u ", portTICK_PERIOD_MS);
        //printf("\n");
        
		if (ad <= analogThreshold) {
            
            visible_now = true;
			//printf("====== read analog ======\n");
			//printf("BELOW THRESHOLD ");
            //printf("\n");
            
		}

        // Set RGB to red if the marking on the electricity meter is visible
        gpio_write(gpio_r, visible_now);
        
        // Take action if the visibility state has changed.
        if (visible_prev != visible_now) {
            
            // If the Marker is now visible, save the start time.
            if (visible_now == true) {

                printf("[ANALOGRD] Marker +++++ START +++++ detected!\r\n");
                tickCountStart = xTaskGetTickCount() * portTICK_RATE_MS;
                
                // TODO: Check for correct values after initialization!
                // MarkerInvisibleTime could be very short dirctly after boot-up.
                markerInvisibleTime = tickCountStart - tickCountStop;
                printf("[ANALOGRD] Marker was invisible for %d milliseconds.\r\n", markerInvisibleTime);
                
            }
            
            // If the marker is now not visible anymore, save the stop time
            // and calculate the duration how long the marker was visible.
            // Also ingrement the counter how often the marker was seen.
            if (visible_now == false) {
                
                printf("[ANALOGRD] Marker ----- STOP  ----- detected!\r\n");
                tickCountStop  = xTaskGetTickCount() * portTICK_RATE_MS;
                
                // TODO: Check for tickCount overflow!
                markerVisibleTime = tickCountStop - tickCountStart;
                printf("[ANALOGRD] Marker was visible for %d milliseconds.\r\n", markerVisibleTime);
                                
                markerSeenCount = markerSeenCount + 1;
                printf("[ANALOGRD] Registered the marker %d times.\r\n", markerSeenCount);
                
                markerRevolutionTime = markerInvisibleTime + markerVisibleTime;
                
                if (markerSeenCount <= 1) {
                    // We don't send a value the fist time because it's a bogus
                    // value for sure due to the unknown markerInvisibleTime.
                } else {
                    xQueueSend(analogValueQueue, &markerRevolutionTime, portMAX_DELAY);    
                }
                


            }

            visible_prev = visible_now;

        }

        //xQueueSendToBackFromISR(tsqueue, &val, NULL);
        //fputs(visible_now ? "true" : "false", stdout);
        //printf("\n");

        // Task runs every 20 milliseconds.
        // A value of (1 / portTICK_RATE_MS) here causes errors.
        vTaskDelay(20 / portTICK_RATE_MS);

        // TODO: Should we move the time measurement into the ISR?

	}

}



// This task buffers values received from the analogReadTask and regularly
// sends them off to the server back end.

void valueHandlingTask(void *pvParameters) {
    
    // TODO: Add buffering code for values recieved from analogReadTask.
    // TODO: Add a call to http_post_request.
    
    double energyValue = 0;
    
    printf("[VALHANDL] Task valueHandling started. Waiting for millisecond value.\r\n");
    xQueueHandle *analogValueQueue = (xQueueHandle *)pvParameters;
    
    while(1) {
        
        uint32_t ms_revolution_value;
        xQueueReceive(*analogValueQueue, &ms_revolution_value, portMAX_DELAY);
        printf("[VALHANDL] Received ms_revolution_value: %d\r\n", ms_revolution_value);
        
        // Calibration factor is in Revolutions per kWh
        // Meaning: X Revolutions per 1000 Watts in 3600 Seconds
        energyValue = ( ( 1000 * 3600 ) / (double)calibrationFactor ) * ( 1000 / (double)ms_revolution_value );
        
        // TODO: Why is a floating point value not displayed correctly?
        // Is this FreeRTOS specific?
        printf("[VALHANDL] Calculated energy value: %f\r\n", energyValue);
        // Trying integer typecast...
        printf("[VALHANDL] Typecasted energy value: %d\r\n", (int)energyValue);
        
        http_get_request((int)energyValue);
        
        // TODO: Determine send duration.
        // Empiric result: Ideally less than a second.
        
        // if (http_post_request(val) != 0) {
        //     printf("ERROR: HTTP_POST Request failed :(\n");
        // }
        
    }

}


// This is the main entry task after the freeRTOS initialization

void user_init(void)
{
    uart_set_baud(0, 115200);

    printf("SDK version:%s\n", sdk_system_get_sdk_version());

    // GPIO pin input/output configuration
    gpio_enable(gpioIN, GPIO_INPUT);
    gpio_set_interrupt(gpioIN, int_type);
    //gpio_enable(gpioLED, GPIO_OUTPUT);
    gpio_enable(gpio_r, GPIO_OUTPUT);
    gpio_enable(gpio_g, GPIO_OUTPUT);
    gpio_enable(gpio_b, GPIO_OUTPUT);

    // Load wireless network configuration data
    struct sdk_station_config config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASS,
    };

    /* required to call wifi_set_opmode before station_set_config */
    sdk_wifi_set_opmode(STATION_MODE);
    sdk_wifi_station_set_config(&config);

    // Initialize the queues defined at the top of this file to communicate between tasks.
    // Queue for the GPIO input handler
    tsqueue          = xQueueCreate(5, sizeof(uint8_t));
    // Queue to transfer a value from analogReadTask to analogSendTask
    // The queue can hold up to 10 millisecond values.
    // One day has 86.400.000 milliseconds.
    // The maximum of a 32 bit unsigned int is 4.294.967.295 which corresponds to 49,710269618 days
    // If the marking was not detected for that long, ther must have been a power outage.
    analogValueQueue = xQueueCreate(10, sizeof(uint32_t));

    // Create tasks to be executed during runtime
    //xTaskCreate(gpioIntTask,       (signed char *)"gpioIntTask",       256, &tsqueue,          2, NULL);
    xTaskCreate(analogReadTask,    (signed char *)"analogReadTask",    256, &analogValueQueue, 2, NULL);
    xTaskCreate(valueHandlingTask, (signed char *)"valueHandlingTask", 256, &analogValueQueue, 2, NULL);
}
