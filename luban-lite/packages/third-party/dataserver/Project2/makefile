OB = lib/server.o lib/client.o lib/aux.o lib/fpath.o lib/q.o lib/q_node.o 
OBSER = lib/server.o lib/aux.o lib/fpath.o lib/q.o lib/q_node.o 
OBCLI = lib/client.o lib/aux.o lib/fpath.o lib/q.o lib/q_node.o 
HEADERS = header/aux.h header/fpath.h header/q.h header/q_node.h
EXECSER = dataServer
EXECCLI = remoteClient
COMP = g++
FLAGS = -std=c++11 -g -Wall -c -ggdb3 -lpthread
#executable
all: server client

lib/server.o: source/server.cpp $(HEADERS)
	$(COMP) $(FLAGS) source/server.cpp
	mv server.o lib/server.o

lib/client.o: source/client.cpp $(HEADERS)
	$(COMP) $(FLAGS) source/client.cpp
	mv client.o lib/client.o

lib/aux.o: source/aux.cpp $(HEADERS)
	$(COMP) $(FLAGS) source/aux.cpp
	mv aux.o lib/aux.o

lib/fpath.o: source/fpath.cpp $(HEADERS)
	$(COMP) $(FLAGS) source/fpath.cpp
	mv fpath.o lib/fpath.o

lib/q.o: source/q.cpp $(HEADERS)
	$(COMP) $(FLAGS) source/q.cpp
	mv q.o lib/q.o

lib/q_node.o: source/q_node.cpp $(HEADERS)
	$(COMP) $(FLAGS) source/q_node.cpp
	mv q_node.o lib/q_node.o

server: $(OBSER)
		$(COMP) -g $(OBSER) -o $(EXECSER) -lpthread
	
client: $(OBCLI)
		$(COMP) -g $(OBCLI) -o $(EXECCLI)

#cleaning command
clean :
	rm -f $(OB) $(EXECSER) $(EXECCLI)
