CXX := clang
CXXFLAGS := -fPIC -Wall -Wno-format -Wno-implicit-int
INCLUDES := -I

OBJECTS	:= myrecover.o

recoverFileSystemTool	: $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o recoverFileSystemTool

myrecover.o	: myrecover.c
	$(CXX) $(CXXFLAGS) -c myrecover.c -o myrecover.o

clean	:
	rm -rf recoverFileSystemTool *.o
