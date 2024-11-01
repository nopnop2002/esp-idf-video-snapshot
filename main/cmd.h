typedef enum {CMD_TAKE, CMD_SEND, CMD_HALT} COMMAND;

typedef struct {
	uint16_t command;
	TaskHandle_t taskHandle;
} CMD_t;

// Message to MQTT/HTTP Client
typedef struct {
	uint16_t command;
	char localFileName[64];
	size_t localFileSize;
	char remoteFileName[64];
	TaskHandle_t taskHandle;
} REQUEST_t;

// Message to HTTP Server
typedef struct {
	char localFileName[64];
	TaskHandle_t taskHandle;
} HTTP_t;
