SRCS = \
	main.c \
	parson.c \
	mongoose.c \
	detect.cc

OBJS = $(subst .c,.o,$(subst .cc,.o,$(SRCS)))

TARGET = mongoose-tflite
TENSORFLOW_ROOT = $(shell go env GOPATH)/src/github.com/tensorflow/tensorflow

ifeq ($(OS),Windows_NT)
TARGET_ARCH = windows_x86_64
TARGET := $(TARGET).exe
CFLAGS = -O3
CXXFLAGS = $(CFLAGS) $(shell pkg-config --cflags opencv4)
LDFLAGS = -lws2_32 $(shell pkg-config --libs opencv4)
else
ifeq ($(shell uname -m),armv6l)
TARGET_ARCH = linux_armv6l
else
TARGET_ARCH = rpi_armv7l
endif
CFLAGS = -O3 -g
CXXFLAGS = $(CFLAGS) $(shell pkg-config --cflags opencv)
LDFLAGS = -lrt $(shell pkg-config --libs opencv)
endif

CFLAGS += -DMG_ENABLE_HTTP_STREAMING_MULTIPART=1
CXXFLAGS += -DMG_ENABLE_HTTP_STREAMING_MULTIPART=1 \
	-I$(TENSORFLOW_ROOT) \
	-I$(TENSORFLOW_ROOT)/tensorflow/lite/tools/make/downloads/flatbuffers/include

LDFLAGS += -L$(TENSORFLOW_ROOT)/tensorflow/lite/tools/make/gen/$(TARGET_ARCH)/lib \
	-ltensorflow-lite \
	-lstdc++ \
	-lpthread \
	-ldl \
	-lm

.SUFFIXES: .c .o .cc .o

all : $(TARGET)

$(TARGET) : $(OBJS)
	g++ -o $@ $(OBJS) $(LDFLAGS)

.c.o :
	gcc -c $(CFLAGS) -I. $< -o $@

.cc.o :
	g++ -c $(CXXFLAGS) -I. $< -o $@

detect : detect.cc
	g++ -DMAIN $(CXXFLAGS) -I. $< -o $@ $(LDFLAGS)

clean :
	rm -f *.o $(TARGET)
