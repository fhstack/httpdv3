OBJPATH=bin/obj
EXAMPLEPATH=bin/example

all:http_server
#	g++ CondVar.cpp -lpthread -c -o $(OBJPATH)/CondVar.o
#	g++ Mutex.cpp -lpthread -c -o $(OBJPATH)/Mutex.o
#	g++ Task.cpp -lpthread -c -o $(OBJPATH)/Task.o
#	g++ ThreadPool.cpp -lpthread -c -o $(OBJPATH)/ThreadPool.o
#	g++ $(OBJPATH)/CondVar.o $(OBJPATH)/Mutex.o $(OBJPATH)/Task.o $(OBJPATH)/ThreadPool.o threadpool_test.cpp -lpthread -o $(EXAMPLEPATH)threadpool_test

http_server:http_conn.cpp http_server.cpp ThreadPool.cpp Task.cpp Mutex.cpp CondVar.cpp 
	g++ -o $@ $^ -lpthread

.PHONY:clean

clean:
	rm -f http_server
