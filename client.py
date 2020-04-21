#!/usr/bin/python
# -*- coding: UTF-8 -*-
 
import socket
import time


sendstr = 'sjadfoiefjqeifsldghe;jrfgioerhfgejfoiwehfeihldsjfdi;sod'

def main_func():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    host = '127.0.0.1'
    port = 10010
    #host = '3.115.197.234'
    #host = '3.114.120.5'
    #port = 1085
    s.connect((host, port))
    s.send(sendstr)
    time.sleep(60)
    print(s.recv(1024))
    s.close()

if __name__ == '__main__':
    main_func()
