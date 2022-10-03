# server-demo
some simple server demos implemented with BSD socket



# multithreadServer_demo.c

under linux, use the command below to build

``` SHELL
$ gcc multithreadServer_demo.c -o server -lpthread
```

## usage

server:

``` shell
$ ./server 12345		#12345 is the number of port
```

client: 

```shell
$ nc localhost 12345	#use localhost when you wanna exam on your one machine
```

if you send it to the server you have, use command below. make sure the walls are correctly setted.

``` shell
$ nc <server ip> <port number>
```

# SelectServer.c

under linux, use the command below to build

``` SHELL
$ gcc SelectServer.c -o SelectServer
```

## usage

just like multithreadServer_demo.c

# epollServer.c

under linux, use the command below to build

``` SHELL
$ gcc epollServer.c -o epollServer
```

## usage

just like multithreadServer_demo.c



# epollReactor.c

under linux, use the command below to build

``` SHELL
$ gcc epollReactor.c -o epollReactor
```

## usage

just like multithreadServer_demo.c
