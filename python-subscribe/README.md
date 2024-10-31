# MQTT Subscriber using python

You can use MQTT Subscriber using python.   
View the image after you subscribe.   
![screen_2024-04-12_06-47-06](https://github.com/nopnop2002/esp-idf-video-snapshot/assets/6020549/5ff266bf-ab54-46f8-8626-e00fdeab2c24)

# Installation   
```
python3 -m pip install -U wheel
python3 -m pip install paho-mqtt
python3 -m pip install opencv-python
python3 -m pip install numpy

git clone https://github.com/nopnop2002/esp-idf-video-snapshot
cd esp-idf-video-snapshot/python-subscribe

python3 subscribe.py --help
usage: subscribe.py [-h] [--host HOST] [--port PORT] [--topic TOPIC]
                    [--output OUTPUT]

options:
  -h, --help       show this help message and exit
  --host HOST      mqtt broker
  --port PORT      mqtt port
  --topic TOPIC    mqtt topic
  --output OUTPUT  output file name
```

# Default parameters   
- host:esp32-broker.local   
- port:1883   
- topic:/image/esp32cam   
- output:./output.jpg
