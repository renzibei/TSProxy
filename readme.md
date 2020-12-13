# TSProxy

TSProxy is a pair of socks5 proxy server and client with encrypted tunnel.

## Compile

This program has been tested on macOS 10.15 and ubuntu 16.04

On ubuntu, first install `libuuid` by run

```shell
sudo apt install uuid-dev
```

Just run the below command to compile

```shell
make -j8
```

## Options

You can adjust most of the options in the `constants.h` header file, including server/client address, port, key...

The key should be in uuid form, e.g.  `879df66f-e758-4a32-af60-dce399530703`.

You can get a random uuid in unix machine using `uuidgen` command

## Run

You have to run a server and a client.

```
./server
```



```
./client
```

## Techniques

We use AES encryption and simply disguise network packets as TLS datagrams.

## TODO

- [ ] Use a config file like yaml or json
- [ ] Totaly diguise the behavior as TLS

