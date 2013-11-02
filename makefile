TODO: servidor cliente multicliente

servidor: servidor.cpp mensajes.h
	g++ --std=c++11 -Wall -Ofast -fpermissive -march=native servidor.cpp -o servidor -lpthread

cliente: cliente.cpp mensajes.h
	g++ --std=c++11 -Wall -Ofast -fpermissive -march=native cliente.cpp -o cliente

multicliente: multicliente.cpp mensajes.h
	g++ --std=c++11 -Wall -Ofast -fpermissive -march=native multicliente.cpp -o multicliente -lpthread
