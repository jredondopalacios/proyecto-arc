TODO: servidor cliente

servidor: servidor.cpp mensajes.h
	g++ --std=c++11 -Wall -Ofast -march=native servidor.cpp -o servidor -lpthread

cliente: cliente.cpp mensajes.h
	g++ --std=c++11 -Wall -Ofast -march=native cliente.cpp -o cliente