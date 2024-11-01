/*
	Take a picture from USB camera.

	This code is in the Public Domain (or CC0 licensed, at your option.)

	Unless required by applicable law or agreed to in writing, this
	software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
	CONDITIONS OF ANY KIND, either express or implied.

	I ported from here:
	https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb/host/uvc
*/

#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "libuvc/libuvc.h"
#include "libuvc_helper.h"
#include "libuvc_adapter.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/ringbuf.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_mac.h" // esp_base_mac_addr_get
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_spiffs.h" 
#include "esp_sntp.h"
#include "mdns.h"
#include "netdb.h" // gethostbyname
#include "lwip/dns.h"
#include "mqtt_client.h"

#include "img_converters.h"

#include "cmd.h"

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

/* - are we connected to the MQTT Server? */
#define MQTT_CONNECTED_BIT BIT2

static const char *TAG = "MAIN";

static int s_retry_num = 0;

QueueHandle_t xQueueCmd;
QueueHandle_t xQueueHttp;
RingbufHandle_t xRingbuffer;
QueueHandle_t xQueueRequest;

static void displayBuffer(uint8_t * buf, int buf_len, bool flag) {
	if (flag == false) return;
	ESP_LOGI(TAG, "buf_len=%d", buf_len);
	ESP_LOGI(TAG, "buf[0]=%02x", *buf);
	ESP_LOGI(TAG, "buf[1]=%02x", *(buf+1));
	ESP_LOGI(TAG, "buf[buf_len-3]=%02x", *(buf+buf_len-3));
	ESP_LOGI(TAG, "buf[buf_len-2]=%02x", *(buf+buf_len-2));
	ESP_LOGI(TAG, "buf[buf_len-1]=%02x", *(buf+buf_len-1));
}

static esp_err_t camera_capture(char * FileName, size_t *pictureSize, size_t *pictureWidth, size_t *pictureHeight, TaskHandle_t usb_camera_task_hadle)
{
	ESP_LOGI(TAG, "camera_capture FileName=[%s]", FileName);
	// Clear RingBuffer
	while (1) {
		size_t bytes_received = 0;
		uvc_frame_t *frame = (uvc_frame_t *)xRingbufferReceive( xRingbuffer, &bytes_received, pdMS_TO_TICKS(2500));

		if (frame != NULL) {
			ESP_LOGI(TAG, "clear bytes_received=%d", bytes_received);
			vRingbufferReturnItem(xRingbuffer, (void *)frame);
		} else {
			break;
		}
	}

	// TaskNotify
	xTaskNotifyGive( usb_camera_task_hadle );
 
	// Receive RingBuffer
	while (1) {
		size_t bytes_received = 0;
		uvc_frame_t *frame = (uvc_frame_t *)xRingbufferReceive( xRingbuffer, &bytes_received, pdMS_TO_TICKS(2500));

		if (frame != NULL) {
			ESP_LOGI(TAG, "frame_format=%d width=%ld height=%ld data_bytes=%d", frame->frame_format, frame->width, frame->height, frame->data_bytes);

			size_t _jpg_buf_len = 0;
			uint8_t *_jpg_buf = NULL;
			if (frame->frame_format == UVC_FRAME_FORMAT_YUYV) {
				bool ret = fmt2jpg(frame->data, frame->data_bytes, frame->width, frame->height, PIXFORMAT_YUV422, 60, &_jpg_buf, &_jpg_buf_len);
				ESP_LOGI(TAG, "fmt2jpg ret=%d", ret);
				if (ret != 1) {
					ESP_LOGE(TAG, "JPEG compression failed");
					return ESP_FAIL;
				}
			} else if (frame->frame_format == UVC_COLOR_FORMAT_MJPEG) {
				_jpg_buf = frame->data;
				_jpg_buf_len = frame->data_bytes;
			} else {
				ESP_LOGW(TAG, "Unknown frame format %d", frame->frame_format);
				return ESP_FAIL;
			}

			//displayBuffer(frame->data, frame->data_bytes, true);
			displayBuffer(_jpg_buf, _jpg_buf_len, true);

			FILE* f = fopen(FileName, "wb");
			if (f == NULL) {
				ESP_LOGE(TAG, "Failed to open file for writing");
			} else {
				//fwrite(frame->data, frame->data_bytes, 1, f);
				fwrite(_jpg_buf, _jpg_buf_len, 1, f);
				fclose(f);
				//*pictureSize = frame->data_bytes;
				*pictureSize = _jpg_buf_len;
				*pictureWidth = frame->width;
				*pictureHeight = frame->height;
			}
			if (frame->frame_format == UVC_FRAME_FORMAT_YUYV) free(_jpg_buf);
			vRingbufferReturnItem(xRingbuffer, (void *)frame);
			break;
		}
	}

	return ESP_OK;
}

static void event_handler(void* arg, esp_event_base_t event_base,
								int32_t event_id, void* event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
			esp_wifi_connect();
			s_retry_num++;
			ESP_LOGI(TAG, "retry to connect to the AP");
		} else {
			xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
		}
		ESP_LOGI(TAG,"connect to the AP fail");
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
		s_retry_num = 0;
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
	}
}

void wifi_init_sta()
{
	s_wifi_event_group = xEventGroupCreate();

	ESP_LOGI(TAG,"ESP-IDF esp_netif");
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_t *netif = esp_netif_create_default_wifi_sta();
	assert(netif);

#if CONFIG_STATIC_IP

	ESP_LOGI(TAG, "CONFIG_STATIC_IP_ADDRESS=[%s]",CONFIG_STATIC_IP_ADDRESS);
	ESP_LOGI(TAG, "CONFIG_STATIC_GW_ADDRESS=[%s]",CONFIG_STATIC_GW_ADDRESS);
	ESP_LOGI(TAG, "CONFIG_STATIC_NM_ADDRESS=[%s]",CONFIG_STATIC_NM_ADDRESS);

	/* Stop DHCP client */
	ESP_ERROR_CHECK(esp_netif_dhcpc_stop(netif));
	ESP_LOGI(TAG, "Stop DHCP Services");

	/* Set STATIC IP Address */
	esp_netif_ip_info_t ip_info;
	memset(&ip_info, 0 , sizeof(esp_netif_ip_info_t));
	ip_info.ip.addr = ipaddr_addr(CONFIG_STATIC_IP_ADDRESS);
	ip_info.netmask.addr = ipaddr_addr(CONFIG_STATIC_NM_ADDRESS);
	ip_info.gw.addr = ipaddr_addr(CONFIG_STATIC_GW_ADDRESS);;
	esp_netif_set_ip_info(netif, &ip_info);

	/*
	I referred from here.
	https://www.esp32.com/viewtopic.php?t=5380

	if we should not be using DHCP (for example we are using static IP addresses),
	then we need to instruct the ESP32 of the locations of the DNS servers manually.
	Google publicly makes available two name servers with the addresses of 8.8.8.8 and 8.8.4.4.
	*/

	ip_addr_t d;
	d.type = IPADDR_TYPE_V4;
	d.u_addr.ip4.addr = 0x08080808; //8.8.8.8 dns
	dns_setserver(0, &d);
	d.u_addr.ip4.addr = 0x08080404; //8.8.4.4 dns
	dns_setserver(1, &d);

#endif // CONFIG_STATIC_IP

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

	wifi_config_t wifi_config = {
		.sta = {
			.ssid = CONFIG_ESP_WIFI_SSID,
			.password = CONFIG_ESP_WIFI_PASSWORD
		},
	};
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
	ESP_ERROR_CHECK(esp_wifi_start() );

	ESP_LOGI(TAG, "wifi_init_sta finished.");

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
			WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
			pdFALSE,
			pdFALSE,
			portMAX_DELAY);

	/* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
	 * happened. */
	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
				 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
	} else if (bits & WIFI_FAIL_BIT) {
		ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
				 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
	} else {
		ESP_LOGE(TAG, "UNEXPECTED EVENT");
	}
	vEventGroupDelete(s_wifi_event_group);
}

void initialise_mdns(void)
{
	//initialize mDNS
	ESP_ERROR_CHECK( mdns_init() );
	//set mDNS hostname (required if you want to advertise services)
	ESP_ERROR_CHECK( mdns_hostname_set(CONFIG_MDNS_HOSTNAME) );
	ESP_LOGI(TAG, "mdns hostname set to: [%s]", CONFIG_MDNS_HOSTNAME);

#if 0
	//set default mDNS instance name
	ESP_ERROR_CHECK( mdns_instance_name_set("ESP32 with mDNS") );
#endif
}

esp_err_t mountSPIFFS(char * partition_label, char * base_path) {
	ESP_LOGI(TAG, "Initializing SPIFFS file system");

	esp_vfs_spiffs_conf_t conf = {
		.base_path = base_path,
		.partition_label = partition_label,
		.max_files = 5,
		.format_if_mount_failed = true
	};

	// Use settings defined above to initialize and mount SPIFFS filesystem.
	// Note: esp_vfs_spiffs_register is an all-in-one convenience function.
	esp_err_t ret = esp_vfs_spiffs_register(&conf);

	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE(TAG, "Failed to mount or format filesystem");
		} else if (ret == ESP_ERR_NOT_FOUND) {
			ESP_LOGE(TAG, "Failed to find SPIFFS partition");
		} else {
			ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
		}
		return ret;
	}

	size_t total = 0, used = 0;
	ret = esp_spiffs_info(partition_label, &total, &used);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
	} else {
		ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
	}
	ESP_LOGI(TAG, "Mount SPIFFS filesystem");
	return ret;
}

#if CONFIG_REMOTE_IS_VARIABLE_NAME
void time_sync_notification_cb(struct timeval *tv)
{
	ESP_LOGI(TAG, "Notification of a time synchronization event");
}

static void initialize_sntp(void)
{
	ESP_LOGI(TAG, "Initializing SNTP");
	esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
	//sntp_setservername(0, "pool.ntp.org");
	ESP_LOGI(TAG, "Your NTP Server is %s", CONFIG_NTP_SERVER);
	esp_sntp_setservername(0, CONFIG_NTP_SERVER);
	sntp_set_time_sync_notification_cb(time_sync_notification_cb);
	esp_sntp_init();
}

static esp_err_t obtain_time(void)
{
	initialize_sntp();
	// wait for time to be set
	int retry = 0;
	const int retry_count = 10;
	while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
		ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
		vTaskDelay(2000 / portTICK_PERIOD_MS);
	}

	if (retry == retry_count) return ESP_FAIL;
	return ESP_OK;
}
#endif

esp_err_t query_mdns_host(const char * host_name, char *ip)
{
	ESP_LOGD(__FUNCTION__, "Query A: %s", host_name);

	struct esp_ip4_addr addr;
	addr.addr = 0;

	esp_err_t err = mdns_query_a(host_name, 10000,	&addr);
	if(err){
		if(err == ESP_ERR_NOT_FOUND){
			ESP_LOGW(__FUNCTION__, "%s: Host was not found!", esp_err_to_name(err));
			return ESP_FAIL;
		}
		ESP_LOGE(__FUNCTION__, "Query Failed: %s", esp_err_to_name(err));
		return ESP_FAIL;
	}

	ESP_LOGD(__FUNCTION__, "Query A: %s.local resolved to: " IPSTR, host_name, IP2STR(&addr));
	sprintf(ip, IPSTR, IP2STR(&addr));
	return ESP_OK;
}

void convert_mdns_host(char * from, char * to)
{
	ESP_LOGI(__FUNCTION__, "from=[%s]",from);
	strcpy(to, from);
	char *sp;
	sp = strstr(from, ".local");
	if (sp == NULL) return;

	int _len = sp - from;
	ESP_LOGD(__FUNCTION__, "_len=%d", _len);
	char _from[128];
	strcpy(_from, from);
	_from[_len] = 0;
	ESP_LOGI(__FUNCTION__, "_from=[%s]", _from);

	char _ip[128];
	esp_err_t ret = query_mdns_host(_from, _ip);
	ESP_LOGI(__FUNCTION__, "query_mdns_host=%d _ip=[%s]", ret, _ip);
	if (ret != ESP_OK) return;

	strcpy(to, _ip);
	ESP_LOGI(__FUNCTION__, "to=[%s]", to);
}
#if CONFIG_SHUTTER_ENTER
void keyin(void *pvParameters);
#endif

#if CONFIG_SHUTTER_GPIO
void gpio(void *pvParameters);
#endif

#if CONFIG_SHUTTER_TCP
void tcp_server(void *pvParameters);
#endif

#if CONFIG_SHUTTER_UDP
void udp_server(void *pvParameters);
#endif

#if CONFIG_SHUTTER_MQTT
void mqtt_sub(void *pvParameters);
#endif

#if CONFIG_SHUTTER_HTTP
#define SHUTTER "HTTP Request"
#endif

void http_task(void *pvParameters);

void usb_camera(void *pvParameters);

#if CONFIG_HTTP_POST
void http_post_task(void *pvParameters);
#endif

#if CONFIG_MQTT_POST
void mqtt_post_task(void *pvParameters);
#endif

void app_main()
{
	//Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
	wifi_init_sta();
	initialise_mdns();

#if CONFIG_REMOTE_IS_VARIABLE_NAME
	// obtain time over NTP
	ESP_LOGI(TAG, "Connecting to WiFi and getting time over NTP.");
	ret = obtain_time();
	if(ret != ESP_OK) {
		ESP_LOGE(TAG, "Fail to getting time over NTP.");
		return;
	}

	// update 'now' variable with current time
	time_t now;
	struct tm timeinfo;
	char strftime_buf[64];
	time(&now);
	now = now + (CONFIG_LOCAL_TIMEZONE*60*60);
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);
#endif

	// Initialize SPIFFS
	char *partition_label = "storage";
	char *base_path = "/spiffs"; 
	ret = mountSPIFFS(partition_label, base_path);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "mountSPIFFS fail");
		while(1) { vTaskDelay(1); }
	}

	/* Create RingBuffer */
	xRingbuffer = xRingbufferCreate(100000, RINGBUF_TYPE_BYTEBUF);
	configASSERT( xRingbuffer );

	/* Create Queue */
	xQueueCmd = xQueueCreate( 1, sizeof(CMD_t) );
	configASSERT( xQueueCmd );
	xQueueHttp = xQueueCreate( 10, sizeof(HTTP_t) );
	configASSERT( xQueueHttp );
	xQueueRequest = xQueueCreate( 1, sizeof(REQUEST_t) );
	configASSERT( xQueueRequest );

	/* Create Shutter Task */
#if CONFIG_SHUTTER_ENTER
#define SHUTTER "Keybord Enter"
	xTaskCreate(keyin, "KEYIN", 1024*4, NULL, 2, NULL);
#endif

#if CONFIG_SHUTTER_GPIO
#define SHUTTER "GPIO Input"
	xTaskCreate(gpio, "GPIO", 1024*4, NULL, 2, NULL);
#endif

#if CONFIG_SHUTTER_TCP
#define SHUTTER "TCP Input"
	xTaskCreate(tcp_server, "TCP", 1024*4, NULL, 2, NULL);
#endif

#if CONFIG_SHUTTER_UDP
#define SHUTTER "UDP Input"
	xTaskCreate(udp_server, "UDP", 1024*4, NULL, 2, NULL);
#endif

#if CONFIG_SHUTTER_MQTT
#define SHUTTER "MQTT Publish"
	xTaskCreate(mqtt_sub, "SUB", 1024*4, NULL, 2, NULL);
#endif

#if CONFIG_HTTP_POST
	xTaskCreate(http_post_task, "POST", 1024*6, NULL, 2, NULL);
#endif

#if CONFIG_MQTT_POST
	xTaskCreate(mqtt_post_task, "POST", 1024*6, NULL, 2, NULL);
#endif

	/* Wait for task start */
	vTaskDelay(10);

	/* Create USB CAMERA Task */
	TaskHandle_t usb_camera_task_hadle = NULL;
	xTaskCreate(usb_camera, "USB", 1024*4, NULL, 2, &usb_camera_task_hadle);

	/* Get the local IP address */
	esp_netif_ip_info_t ip_info;
	ESP_ERROR_CHECK(esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info));

	/* Create HTTP Task */
	char cparam0[64];
	sprintf(cparam0, IPSTR, IP2STR(&ip_info.ip));
	ESP_LOGI(TAG, "cparam0=[%s]", cparam0);
	xTaskCreate(http_task, "HTTP", 1024*6, (void *)cparam0, 2, NULL);

	HTTP_t httpBuf;
	httpBuf.taskHandle = xTaskGetCurrentTaskHandle();
	sprintf(httpBuf.localFileName, "%s/picture.jpg", base_path);
	ESP_LOGI(TAG, "localFileName=%s",httpBuf.localFileName);
	
	CMD_t cmdBuf;

#if CONFIG_HTTP_POST
	REQUEST_t requestBuf;
	requestBuf.command = CMD_SEND;
	requestBuf.taskHandle = xTaskGetCurrentTaskHandle();
	strcpy(requestBuf.localFileName, httpBuf.localFileName);
#if CONFIG_REMOTE_IS_FIXED_NAME
	// picture.jpg
	sprintf(requestBuf.remoteFileName, "%s", CONFIG_FIXED_REMOTE_FILE);
	ESP_LOGI(TAG, "remoteFileName=%s",requestBuf.remoteFileName);
#endif
#endif // CONFIG_HTTP_POST

#if CONFIG_MQTT_POST
	REQUEST_t requestBuf;
	requestBuf.command = CMD_SEND;
	requestBuf.taskHandle = xTaskGetCurrentTaskHandle();
	strcpy(requestBuf.localFileName, httpBuf.localFileName);
#endif // CONFIG_MQTT_POST

	while(1) {
		ESP_LOGI(TAG,"Waitting %s ....", SHUTTER);
		xQueueReceive(xQueueCmd, &cmdBuf, portMAX_DELAY);
		ESP_LOGI(TAG,"cmdBuf.command=%d", cmdBuf.command);
		if (cmdBuf.command == CMD_HALT) break;

		// Delete Local file
		struct stat statBuf;
		if (stat(httpBuf.localFileName, &statBuf) == 0) {
			// Delete it if it exists
			unlink(httpBuf.localFileName);
			ESP_LOGI(TAG, "Delete Local file");
		}

		// Save Picture to Local file
		size_t pictureSize;
		size_t pictureWidth;
		size_t pictureHeight;
		ret = camera_capture(httpBuf.localFileName, &pictureSize, &pictureWidth, &pictureHeight, usb_camera_task_hadle);
		ESP_LOGI(TAG, "camera_capture=%d pictureSize=%d pictureWidth=%d pictureHeight=%d",ret, pictureSize, pictureWidth, pictureHeight);
		if (ret != ESP_OK) continue;
		if (stat(httpBuf.localFileName, &statBuf) == 0) {
			ESP_LOGI(TAG, "st_size=%d", (int)statBuf.st_size);
			if (statBuf.st_size != pictureSize) {
				ESP_LOGE(TAG, "Illegal file size %d", (int)statBuf.st_size);
				continue;
			}
		}

#if CONFIG_REMOTE_IS_VARIABLE_NAME
		// 20220927-110742.jpg
		time(&now);
		now = now + (CONFIG_LOCAL_TIMEZONE*60*60);
		localtime_r(&now, &timeinfo);
		strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
		ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);
		sprintf(requestBuf.remoteFileName, "%04d%02d%02d-%02d%02d%02d.jpg",
		(timeinfo.tm_year+1900),(timeinfo.tm_mon+1),timeinfo.tm_mday,
		timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec);
		ESP_LOGI(TAG, "remoteFileName=%s",requestBuf.remoteFileName);
#endif // CONFIG_REMOTE_IS_VARIABLE_NAME

#if CONFIG_HTTP_POST || CONFIG_MQTT_POST
		// Send post request
		requestBuf.localFileSize = pictureSize;
		if (xQueueSend(xQueueRequest, &requestBuf, 10) != pdPASS) {
			ESP_LOGE(TAG, "xQueueSend fail");
		} else {
			uint32_t value = ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
			ESP_LOGI(TAG, "ulTaskNotifyTake value=%"PRIx32, value);
		}
#endif // CONFIG_HTTP_POST || CONFIG_MQTT_POST

		// send Local file name to http task
		if (xQueueSend(xQueueHttp, &httpBuf, 10) != pdPASS) {
			ESP_LOGE(TAG, "xQueueSend xQueueHttp fail");
		}


	} // end while

	esp_vfs_spiffs_unregister(NULL);
	ESP_LOGI(TAG, "SPIFFS unmounted");

}
