CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall
# The order of libraries matters for the linker
LIBS = -lclickhouse-cpp-lib -lcurl -lzstd -llz4 -lcityhash -lpthread

TARGET = binance-kline-fetcher
SRC = binance-kline-fetcher.cpp
OBJ = binance-kline-fetcher.o

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJ) $(LIBS)

$(OBJ): $(SRC)
	$(CXX) $(CXXFLAGS) -c $(SRC) -o $(OBJ)

clean:
	rm -f $(TARGET) $(OBJ)
