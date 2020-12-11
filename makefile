CXX = g++
CXX_FLAGS = -O3 --std=c++11
CXX_LIBS = 
# CLIENT_SRCS = clientlaunch.cpp loghelper.cpp proxyclient.cpp SocketPlugin.cpp utils.cpp
CLIENT_INCS = loghelper.h proxyclient.h SocketPlugin.h utils.h constants.h aes.h
CLIENT_OBJS = clientlaunch.o loghelper.o proxyclient.o SocketPlugin.o utils.o constants.o aes.o
# SERVER_SRCS = loghelper.cpp proxyserver.cpp serverlaunch.cpp utils.cpp
SERVER_INCS = loghelper.h proxyserver.h utils.h constants.h aes.h
SERVER_OBJS = loghelper.o proxyserver.o serverlaunch.o utils.o constants.o aes.o
# INCS = loghelper.h proxyserver.h proxyclient.h SocketPlugin.h utils.h constants.h
# OBJECTS = clientlaunch.o loghelper.o proxyclient.o proxyserver.o serverlaunch.o SocketPlugin.o utils.o

UNAME := $(shell uname)

ifeq ($(UNAME), Linux)
	CXX_LIBS += -lpthread -luuid
endif

main: client server
	echo "Make End"

client: $(CLIENT_OBJS)
	$(CXX) -o $@ $^ $(CXX_FLAGS) $(CXX_LIBS)

server: $(SERVER_OBJS)
	$(CXX) -o $@ $^ $(CXX_FLAGS) $(CXX_LIBS)

%.o: %.cpp $(SERVER_INCS) $(CLIENT_INCS)
	$(CXX) -o $@ -c $< $(CXX_FLAGS) $(CXX_LIBS)

clean:
	rm server client *.o
