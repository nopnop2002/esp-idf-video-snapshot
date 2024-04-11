#!/usr/bin/python3
#-*- encoding: utf-8 -*-
#
# Requirement library
# python3 -m pip install -U netifaces

from socket import *
import netifaces
import argparse

if __name__=='__main__':
	parser = argparse.ArgumentParser()
	parser.add_argument('--port', type=int, help='udp port', default=9876)
	args = parser.parse_args()
	print("args.port={}".format(args.port))

	# Get my IP address
	for iface_name in netifaces.interfaces():
		iface_data = netifaces.ifaddresses(iface_name)
		ipList=iface_data.get(netifaces.AF_INET)
		#print("ip={}".format(ipList))
		ipDict = ipList[0]
		addr = ipDict["addr"]
		#print("addr={}".format(addr))
		if (addr != "127.0.0.1"):
			myIp = addr

	print("myIp={}".format(myIp))
	myIpList = myIp.split('.')
	print("myIpList={}".format(myIpList))

	# Set Directed broadcast address
	#broadcast = "192.168.10.255" # for Broadcast
	broadcast = "{}.{}.{}.255".format(myIpList[0], myIpList[1], myIpList[2])
	print("broadcast={}".format(broadcast))

	s = socket(AF_INET, SOCK_DGRAM)
	s.setsockopt(SOL_SOCKET, SO_BROADCAST, 1)
	s.bind(('', args.port))
	s.sendto(b'take picture', (broadcast, args.port))
	s.close()
