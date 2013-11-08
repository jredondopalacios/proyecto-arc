TODO: network servidor cliente multicliente 

servidor: servidor.cpp network.o mensajes.h
	g++ --std=c++11 -Wall -Ofast -fpermissive -march=native servidor.cpp -o servidor -lpthread ./network.o

cliente: cliente.cpp mensajes.h
	g++ --std=c++11 -Wall -Ofast -fpermissive -march=native cliente.cpp -o cliente

multicliente: multicliente.cpp mensajes.h
	g++ --std=c++11 -Wall -Ofast -fpermissive -march=native multicliente.cpp -o multicliente -lpthread

network: network.cpp network.h mensajes.h
	g++ -c network.cpp -o network.o
