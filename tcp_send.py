#!/usr/bin/python3
#-*- encoding: utf-8 -*-
import socket
import argparse

if __name__=='__main__':
	parser = argparse.ArgumentParser()
	parser.add_argument('--host', help='tcp server', default='esp32-camera.local')
	parser.add_argument('--port', type=int, help='tcp port', default=9876)
	args = parser.parse_args()
	#host = "esp32-camera.local" # mDNS hostname
	#port = 9876
	print("args.host={}".format(args.host))
	print("args.port={}".format(args.port))

	client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	client.connect((args.host, args.port))
	client.send(b'take picture')
	response = client.recv(1024)
	client.close()
	print(response)
