/* MQTT (over TCP) Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_mac.h" // esp_base_mac_addr_get
#include "mqtt_client.h"

#include "cmd.h"

#if CONFIG_MQTT_POST

static const char *TAG = "PUB";

EventGroupHandle_t mqtt_status_event_group;
#define MQTT_CONNECTED_BIT BIT2

extern QueueHandle_t xQueueRequest;
extern QueueHandle_t xQueueResponse;

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
	// your_context_t *context = event->context;
	switch (event->event_id) {
		case MQTT_EVENT_CONNECTED:
			ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
			xEventGroupSetBits(mqtt_status_event_group, MQTT_CONNECTED_BIT);
			break;
		case MQTT_EVENT_DISCONNECTED:
			ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
			xEventGroupClearBits(mqtt_status_event_group, MQTT_CONNECTED_BIT);
			break;
		case MQTT_EVENT_SUBSCRIBED:
			ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
			break;
		case MQTT_EVENT_UNSUBSCRIBED:
			ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
			break;
		case MQTT_EVENT_PUBLISHED:
			ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
			break;
		case MQTT_EVENT_DATA:
			ESP_LOGI(TAG, "MQTT_EVENT_DATA");
			break;
		case MQTT_EVENT_ERROR:
			ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
			break;
		default:
			ESP_LOGI(TAG, "Other event id:%d", event->event_id);
			break;
	}
	return ESP_OK;
}

esp_err_t query_mdns_host(const char * host_name, char *ip);
void convert_mdns_host(char * from, char * to);

void mqtt_post_task(void *pvParameters)
{
	ESP_LOGI(TAG, "Start");

	/* Create Eventgroup */
	mqtt_status_event_group = xEventGroupCreate();
	configASSERT( mqtt_status_event_group );
	xEventGroupClearBits(mqtt_status_event_group, MQTT_CONNECTED_BIT);

	// Set client id from mac
	uint8_t mac[8];
	ESP_ERROR_CHECK(esp_base_mac_addr_get(mac));
	for(int i=0;i<8;i++) {
		ESP_LOGI(TAG, "mac[%d]=%x", i, mac[i]);
	}
	char client_id[64];
	sprintf(client_id, "pub-%02x%02x%02x%02x%02x%02x", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
	ESP_LOGI(TAG, "client_id=[%s]", client_id);

	// Resolve mDNS host name
	char ip[128];
	ESP_LOGI(TAG, "CONFIG_PUB_BROKER=[%s]", CONFIG_PUB_BROKER);
	convert_mdns_host(CONFIG_PUB_BROKER, ip);
	ESP_LOGI(TAG, "ip=[%s]", ip);
	char uri[128];
	sprintf(uri, "mqtt://%s", ip);
	ESP_LOGI(TAG, "uri=[%s]", uri);

	esp_mqtt_client_config_t mqtt_cfg = {
		//.uri = CONFIG_BROKER_URL,
		.uri = uri,
		.event_handle = mqtt_event_handler,
		.client_id = client_id
	};

	esp_mqtt_client_handle_t mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
	esp_mqtt_client_start(mqtt_client);
	xEventGroupWaitBits(mqtt_status_event_group, MQTT_CONNECTED_BIT, false, true, portMAX_DELAY);
	ESP_LOGI(TAG, "Connect to MQTT Server");

	REQUEST_t requestBuf;
	RESPONSE_t responseBuf;
	while (1) {
		xQueueReceive(xQueueRequest, &requestBuf, portMAX_DELAY);
		ESP_LOGI(TAG, "localFileName=[%s] localFileSize=%d", requestBuf.localFileName, requestBuf.localFileSize);
		
		EventBits_t EventBits = xEventGroupGetBits(mqtt_status_event_group);
		ESP_LOGI(TAG, "EventBits=%x", EventBits);
		if (EventBits & MQTT_CONNECTED_BIT) {
			char *image_buffer = NULL;
			image_buffer = malloc(requestBuf.localFileSize);
			if (image_buffer == NULL) {
				ESP_LOGE(TAG, "malloc fail. image_buffer %d", requestBuf.localFileSize);
			} else {
				FILE * fp;
				if((fp=fopen(requestBuf.localFileName,"rb"))==NULL){
					ESP_LOGE(TAG, "fopen fail. [%s]", requestBuf.localFileName);
				}else{
					for (int i=0;i<requestBuf.localFileSize;i++) {
						fread(&image_buffer[i],sizeof(char),1,fp);
					}
					fclose(fp);
					int msg_id = esp_mqtt_client_publish(mqtt_client, CONFIG_PUB_TOPIC, image_buffer, requestBuf.localFileSize, 0, 0);
					if (msg_id < 0) {
						ESP_LOGE(TAG, "esp_mqtt_client_publish fail");
					} else {
						ESP_LOGI(TAG, "sent publish successful, topic=[%s] msg_id=%d", CONFIG_PUB_TOPIC, msg_id);
					}
				}
				free(image_buffer);
			}
		} else {
			ESP_LOGW(TAG, "Disconnect to MQTT Server. Skip to send");
		}

		/* send HTTP response */
		if (xQueueSend(xQueueResponse, &responseBuf, 10) != pdPASS) {
			ESP_LOGE(TAG, "xQueueSend fail");
		}

	}

	ESP_LOGI(TAG, "Task Delete");
	esp_mqtt_client_stop(mqtt_client);
	vTaskDelete(NULL);

}
#endif
