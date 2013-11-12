TODO: servidor cliente multicliente network 

servidor: servidor.cpp network mensajes.h
	g++ --std=c++11 -g -Wall -O0 -fpermissive servidor.cpp -o servidor -lpthread ./network.o
cliente: cliente.cpp mensajes.h
	g++ --std=c++11 -Wall -Ofast -fpermissive -march=native cliente.cpp -o cliente

multicliente: multicliente.cpp mensajes.h
	g++ --std=c++11 -Wall -Ofast -fpermissive -march=native multicliente.cpp -o multicliente -lpthread

network: network.cpp network.h mensajes.h
	g++ -c network.cpp -g -o network.o

test: test-conexiones.cpp mensajes.h
	g++ test-conexiones.cpp -o test-conexiones
