/* Capture video from a USB camera using the libuvc library

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.

   Based on this.
   https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb/host/uvc
*/
#include <stdio.h>
#include <unistd.h>
#include "libuvc/libuvc.h"
#include "libuvc_helper.h"
#include "libuvc_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "freertos/event_groups.h"
#include "usb/usb_host.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h" // for esp_timer_get_time()

static const char *TAG = "USB_CAMERA";

extern RingbufHandle_t xRingbuffer;

//#define FPS 15
#define FPS 10
#define WIDTH 320
#define HEIGHT 240
#define FORMAT UVC_COLOR_FORMAT_MJPEG

// Attached camera can be filtered out based on (non-zero value of) PID, VID, SERIAL_NUMBER
#define PID 0
#define VID 0
#define SERIAL_NUMBER NULL

#define UVC_CHECK(exp) do {					\
	uvc_error_t _err_ = (exp);				\
	if(_err_ < 0) {							\
		ESP_LOGE(TAG, "UVC error: %s",		\
				 uvc_error_string(_err_));	\
		assert(0);							\
	}										\
} while(0)

static SemaphoreHandle_t ready_to_uninstall_usb;
static EventGroupHandle_t app_flags;

// Handles common USB host library events
static void usb_lib_handler_task(void *args)
{
	ESP_LOGI(pcTaskGetName(0), "start");
	while (1) {
		uint32_t event_flags;
		usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
		// Release devices once all clients has deregistered
		if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
			ESP_LOGI(pcTaskGetName(0), "USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS");
			usb_host_device_free_all();
		}
		// Give ready_to_uninstall_usb semaphore to indicate that USB Host library
		// can be deinitialized, and terminate this task.
		if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
			ESP_LOGI(pcTaskGetName(0), "USB_HOST_LIB_EVENT_FLAGS_ALL_FREE");
			xSemaphoreGive(ready_to_uninstall_usb);
		}
	}

	ESP_LOGI(pcTaskGetName(0), "end");
	vTaskDelete(NULL);
}

static esp_err_t initialize_usb_host_lib(void)
{
	TaskHandle_t task_handle = NULL;

	const usb_host_config_t host_config = {
		.intr_flags = ESP_INTR_FLAG_LEVEL1
	};

	esp_err_t err = usb_host_install(&host_config);
	if (err != ESP_OK) {
		return err;
	}

	ready_to_uninstall_usb = xSemaphoreCreateBinary();
	if (ready_to_uninstall_usb == NULL) {
		usb_host_uninstall();
		return ESP_ERR_NO_MEM;
	}

	if (xTaskCreate(usb_lib_handler_task, "usb_events", 4096, NULL, 2, &task_handle) != pdPASS) {
		vSemaphoreDelete(ready_to_uninstall_usb);
		usb_host_uninstall();
		return ESP_ERR_NO_MEM;
	}

	return ESP_OK;
}

static void uninitialize_usb_host_lib(void)
{
	xSemaphoreTake(ready_to_uninstall_usb, portMAX_DELAY);
	ESP_LOGI(__FUNCTION__, "wake up");
	vSemaphoreDelete(ready_to_uninstall_usb);

	if ( usb_host_uninstall() != ESP_OK) {
		ESP_LOGE(TAG, "Failed to uninstall usb_host");
	}
}

void button_callback(int button, int state, void *user_ptr)
{
	printf("button %d state %d\n", button, state);
}

static void libuvc_adapter_cb(libuvc_adapter_event_t event)
{
	ESP_LOGI(TAG, "libuvc_adapter_cb event=%x", event);
	xEventGroupSetBits(app_flags, event);
}

static EventBits_t wait_for_event(EventBits_t event)
{
	return xEventGroupWaitBits(app_flags, event, pdTRUE, pdFALSE, portMAX_DELAY) & event;
}

static EventBits_t check_for_event(EventBits_t event)
{
	return xEventGroupWaitBits(app_flags, event, pdTRUE, pdFALSE, 0) & event;
}

static void displayBuffer(uint8_t * buf, int buf_len, bool flag) {
	if (flag == false) return;
	ESP_LOGI(TAG, "buf_len=%d", buf_len);
	ESP_LOGI(TAG, "buf[0]=%02x", *buf);
	ESP_LOGI(TAG, "buf[1]=%02x", *(buf+1));
	ESP_LOGI(TAG, "buf[buf_len-3]=%02x", *(buf+buf_len-3));
	ESP_LOGI(TAG, "buf[buf_len-2]=%02x", *(buf+buf_len-2));
	ESP_LOGI(TAG, "buf[buf_len-1]=%02x", *(buf+buf_len-1));
}

static void camera_not_available(char *message) {
	ESP_LOGE(TAG, "%s", message);
	ESP_LOGE(TAG, "Change other camera and re-start");
	while(1) { vTaskDelay(1); }
}

void usb_camera(void *pvParameters) {
	uvc_context_t *ctx;
	uvc_device_t *dev;
	uvc_device_handle_t *devh;
	uvc_stream_ctrl_t ctrl;
	uvc_error_t res;

	app_flags = xEventGroupCreate();
	assert(app_flags);

	ESP_ERROR_CHECK( initialize_usb_host_lib() );

	libuvc_adapter_config_t config = {
		.create_background_task = true,
		.task_priority = 5,
		.stack_size = 4096,
		.callback = libuvc_adapter_cb
	};

	libuvc_adapter_set_config(&config);

	UVC_CHECK( uvc_init(&ctx, NULL) );

	while (1) {

		printf("Waiting for device\n");
		wait_for_event(UVC_DEVICE_CONNECTED);

		UVC_CHECK( uvc_find_device(ctx, &dev, PID, VID, SERIAL_NUMBER) );
		puts("Device found");

		UVC_CHECK( uvc_open(dev, &devh) );

		// Uncomment to print configuration descriptor
		// libuvc_adapter_print_descriptors(devh);

		// I don't know how to use this
		//uvc_set_button_callback(devh, button_callback, NULL);

		// Print known device information
		uvc_print_diag(devh, stderr);

		int fps = FPS;
		int width = WIDTH;
		int height = HEIGHT;
		int format = FORMAT;
		const uvc_format_desc_t *format_desc = uvc_get_format_descs(devh);
		const uvc_frame_desc_t *frame_desc = format_desc->frame_descs;
		if (frame_desc) {
			ESP_LOGI(TAG, "frame_desc->wWidth=%d", frame_desc->wWidth);
			ESP_LOGI(TAG, "frame_desc->wHeight=%d", frame_desc->wHeight);
			ESP_LOGI(TAG, "format_desc->bDescriptorSubtype=%d", format_desc->bDescriptorSubtype);
			ESP_LOGI(TAG, "frame_desc->dwDefaultFrameInterval=%d", frame_desc->dwDefaultFrameInterval);
			if (format_desc->bDescriptorSubtype == UVC_VS_FORMAT_UNCOMPRESSED) { // Logitech C270/C270N
				ESP_LOGI(TAG, "format_desc->bDescriptorSubtype == UVC_VS_FORMAT_UNCOMPRESSED");
				width = frame_desc->wWidth;
				height = frame_desc->wHeight;
				format = UVC_COLOR_FORMAT_UNCOMPRESSED;
			} else if (format_desc->bDescriptorSubtype == UVC_VS_FORMAT_MJPEG) { // 
				ESP_LOGI(TAG, "format_desc->bDescriptorSubtype == UVC_VS_FORMAT_MJPEG");
				width = frame_desc->wWidth;
				height = frame_desc->wHeight;
				format = UVC_COLOR_FORMAT_MJPEG;
			} else {
				camera_not_available("Unknown UVC_VS_FORMAT");
			}
		}

#if CONFIG_SIZE_640x480
        ESP_LOGW(TAG, "FRAME SIZE=640x480");
        width = 640;
        height = 480;
#elif CONFIG_SIZE_352x288
        ESP_LOGW(TAG, "FRAME SIZE=352x288");
        width = 352;
        height = 288;
#elif CONFIG_SIZE_320x240
        ESP_LOGW(TAG, "FRAME SIZE=320x240");
        width = 320;
        height = 240;
#elif CONFIG_SIZE_160x120
        ESP_LOGW(TAG, "FRAME SIZE=160x120");
        width = 160;
        height = 120;
#endif

#if CONFIG_FORMAT_YUY2
        ESP_LOGW(TAG, "FRAME FORMAT=YUYV");
        format = UVC_FRAME_FORMAT_YUYV;
#elif CONFIG_FORMAT_MJPG
        ESP_LOGW(TAG, "FRAME FORMAT=MJPEG");
        format = UVC_FRAME_FORMAT_MJPEG;
#endif

		// Negotiate stream profile
		int retry = 0;
		ESP_LOGI(TAG, "format=%d width=%d height=%d fps=%d", format, width, height, fps);
		//res = uvc_get_stream_ctrl_format_size(devh, &ctrl, FORMAT, WIDTH, HEIGHT, FPS );
		res = uvc_get_stream_ctrl_format_size(devh, &ctrl, format, width, height, fps );
		while (res != UVC_SUCCESS) {
			printf("Negotiating streaming format failed, trying again...\n");
			//res = uvc_get_stream_ctrl_format_size(devh, &ctrl, FORMAT, WIDTH, HEIGHT, FPS );
			res = uvc_get_stream_ctrl_format_size(devh, &ctrl, format, width, height, fps );
			sleep(1);
			retry++;
			if (retry == 10) {
				camera_not_available("uvc_get_stream_ctrl_format_size fail");
			}
		}

		// dwMaxPayloadTransferSize has to be overwritten to MPS (maximum packet size)
		// supported by ESP32-S2(S3), as libuvc selects the highest possible MPS by default.
		ctrl.dwMaxPayloadTransferSize = 512;

		//uvc_print_stream_ctrl(&ctrl, stderr);

		uvc_stream_handle_t *strmhp;
		res = uvc_stream_open_ctrl(devh, &strmhp, &ctrl);
		ESP_LOGI(TAG, "uvc_stream_open_ctrl=%d", res);
		if (res != 0) {
			camera_not_available("uvc_stream_open_ctrl fail");
		}
		res = uvc_stream_start(strmhp, NULL, NULL, 0);
		ESP_LOGI(TAG, "uvc_stream_start=%d", res);
		if (res != 0) {
			camera_not_available("uvc_stream_start fail");
			break;
		}

		uvc_frame_t *frame;
		while (1) {
			// Wait for task notify
			uint32_t notify = ulTaskNotifyTake( pdTRUE, pdMS_TO_TICKS(1000));
			ESP_LOGD(TAG, "Wake Up. notify=%d", notify);
			if (notify == 0) {
				if (check_for_event(UVC_DEVICE_DISCONNECTED)) break;
				continue;
			}

			// Poll for a frame
			res = uvc_stream_get_frame(strmhp, &frame, 0);
			ESP_LOGI(TAG, "uvc_stream_get_frame=%d", res);
			if (res != 0) {
				camera_not_available("uvc_stream_get_frame fail");
			}
			ESP_LOGI(TAG, "frame_format=%d width=%ld height=%ld data_bytes=%d metadata_bytes=%d", frame->frame_format, frame->width, frame->height, frame->data_bytes, frame->metadata_bytes);

			// Send frame to other
			displayBuffer(frame->data, frame->data_bytes, true);
			ESP_LOGI(TAG, "sizeof(uvc_frame_t)=%d", sizeof(uvc_frame_t));
			if ( xRingbufferSend(xRingbuffer, frame, sizeof(uvc_frame_t), pdMS_TO_TICKS(1)) != pdTRUE ) {
				ESP_LOGE(TAG, "Failed to send frame to ring buffer.");
			}
		}
		ESP_LOGI(TAG, "uvc_close");
		uvc_close(devh);

	} // end while

	// Never reach here
	uvc_exit(ctx);
	puts("UVC exited");

	uninitialize_usb_host_lib();
	vTaskDelete(NULL);
}
