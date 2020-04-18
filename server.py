#!/usr/bin/python
# -*- coding: UTF-8 -*-

import select
import socket
import time


sendstr = 'sjadfoiefjqeifsldghe;jrfgioerhfgejfoiwehfeihldsjfdi;sod'

def main_func():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setblocking(False)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1) #keepalive
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1) #端口复用
 
    inputs = [s]
    outputs = []
    client_info = {}

    host = '127.0.0.1'
    port = 10087
    s.bind((host, port))
    s.listen(5)
    
    while True:
        readable , writable , exceptional = select.select(inputs, outputs, inputs, 60)
        if not (readable or writable or exceptional) :
            continue

        for sock in readable :
            if sock is s:#是客户端连接
                connection, client_address = s.accept()
                #print "connection", connection
                print "%s connect." % str(client_address)
                connection.setblocking(0) #非阻塞
                inputs.append(connection) #客户端添加到inputs
                client_info[connection] = str(client_address)

            else: #是client, 数据发送过来
                data = None
                try:
                    data = sock.recv(1024)
                except Exception as e:
                    err_msg = "Recv Client Error! {}".format(str(e))
                    print(err_msg)

                if data:
                    #print data
                    data = "%s %s say: |%s|" % (time.strftime("%Y-%m-%d %H:%M:%S"), client_info[sock], data)
                    print(data)
                    try:
                        sock.send(data)
                    except Exception as e:
                        err_msg = "Send Client Error! {}".format(str(e))
                        print(err_msg)

                else: #客户端断开
                    #Interpret empty result as closed connection
                    print "Client:%s Close." % str(client_info[sock])
                    inputs.remove(sock)
                    sock.close()
                    del client_info[sock]

#            for s in writable: #outputs 有消息就要发出去了
#                try:
#                    next_msg = self.message_queues[s].get_nowait()  #非阻塞获取
#                except Queue.Empty:
#                    err_msg = "Output Queue is Empty!"
#                    #g_logFd.writeFormatMsg(g_logFd.LEVEL_INFO, err_msg)
#                    self.outputs.remove(s)
#                except Exception, e:  #发送的时候客户端关闭了则会出现writable和readable同时有数据，会出现message_queues的keyerror
#                    err_msg = "Send Data Error! ErrMsg:%s" % str(e)
#                    logging.error(err_msg)
#                    if s in self.outputs:
#                        self.outputs.remove(s)
#                else:
#                    for cli in self.client_info: #发送给其他客户端
#                        if cli is not s:
#                            try:
#                                cli.sendall(next_msg)
#                            except Exception, e: #发送失败就关掉
#                                err_msg = "Send Data to %s  Error! ErrMsg:%s" % (str(self.client_info[cli]), str(e))
#                                logging.error(err_msg)
#                                print "Client: %s Close Error." % str(self.client_info[cli])
#                                if cli in self.inputs:
#                                    self.inputs.remove(cli)
#                                    cli.close()
#                                if cli in self.outputs:
#                                    self.outputs.remove(s)
#                                if cli in self.message_queues:
#                                    del self.message_queues[s]
#                                del self.client_info[cli]
#
#
#        clisock, cliaddr = s.accept()
#        print("cli addr: {0}".format(str(cliaddr)))
        
    
    s.close()

if __name__ == '__main__':
    main_func()
