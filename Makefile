TARGET = tick_feeder_binance

CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pthread
CXXFLAGS += -Isrc -Itrcache -Itrcache/src/include -Ithird_party

TRCACHE_DIR = trcache
TRCACHE_LIB = $(TRCACHE_DIR)/libtrcache.a

LDFLAGS = -L$(TRCACHE_DIR) -ltrcache -lcurl -lpthread

SRCS = src/main.cpp \
       src/config.cpp \
       src/engine.cpp \
       src/output_writer.cpp \
       src/downloader.cpp \
       src/decompressor.cpp \
       src/csv_parser.cpp \
       src/metadata.cpp \
       src/funding.cpp

OBJS = $(SRCS:.cpp=.o)

.PHONY: all clean trcache

all: trcache $(TARGET)

trcache:
	$(MAKE) -C $(TRCACHE_DIR)

$(TARGET): $(OBJS) $(TRCACHE_LIB)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
	$(MAKE) -C $(TRCACHE_DIR) clean

.PHONY: all clean trcache
