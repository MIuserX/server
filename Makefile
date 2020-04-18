
server:
	gcc -o server server.c service.c server_other.c pipe.c common.c buffer.c line.c tunnel.c packet.c

client: client.c service.c server_other.c pipe.c common.c buffer.c tunnel.c packet.c
	gcc -o client client.c service.c server_other.c pipe.c common.c buffer.c line.c tunnel.c packet.c -lpthread

clean:
	rm server
