set(component_srcs
	yuv.c
	to_jpg.cpp
	to_bmp.c
	jpge.cpp
	esp_jpg_decode.c
)

set(component_include
	include
	../../managed_components/espressif__esp_jpeg/tjpgd
	)

idf_component_register(SRCS "${component_srcs}"
	INCLUDE_DIRS "${component_include}" "private_include")
