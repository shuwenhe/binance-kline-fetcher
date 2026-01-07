CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall
# Added -llz4 and -lcityhash
LIBS = -lcurl -lclickhouse-cpp-lib -lpthread -lzstd -llz4 -lcityhash

TARGET = binance-kline-fetcher
OBJS = binance-kline-fetcher.o

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

binance-kline-fetcher.o: binance-kline-fetcher.cpp
	$(CXX) $(CXXFLAGS) -c binance-kline-fetcher.cpp -o binance-kline-fetcher.o

clean:
	rm -f $(TARGET) $(OBJS)
