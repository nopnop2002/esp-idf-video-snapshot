# MQTT Subscriber using python

You can use MQTT Subscriber using python.   
![screen_2024-04-12_06-47-06](https://github.com/nopnop2002/esp-idf-video-snapshot/assets/6020549/5ff266bf-ab54-46f8-8626-e00fdeab2c24)

# Installation   
```
git clone https://github.com/nopnop2002/esp-idf-video-snapshot
cd esp-idf-video-snapshot/subscribe.py
python3 -m pip install -U wheel
python3 -m pip install paho-mqtt
python3 -m pip install opencv-python
python3 -m pip install numpy

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

