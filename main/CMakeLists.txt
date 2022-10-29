set(COMPONENT_SRCS "main.c" 
	"gpio.c" 
	"keyboard.c" 
	"tcp_server.c" 
	"udp_server.c" 
	"http_server.c" 
	"http_post.c" 
	"mqtt_sub.c"
	"mqtt_pub.c"
	"usb_camera.c")
set(COMPONENT_ADD_INCLUDEDIRS ".")

register_component()
