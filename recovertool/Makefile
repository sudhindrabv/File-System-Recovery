CXX := clang
CXXFLAGS := -fPIC -Wall -Wno-format -Wno-implicit-int
INCLUDES := -I

OBJECTS	:= myrecover.o

rfstool	: $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o rfstool 

myrecover.o	: myrecover.c
	$(CXX) $(CXXFLAGS) -c myrecover.c -o myrecover.o

clean	:
	rm -rf rfstool *.o
