CC = gcc
CFLAGS = -g -O0 -lpthread

INCLUDE = -I./

LIBS = -L./ 
LIBS = -lpthread 

SRC_LIST = client.c \
		   redo.c \
		   ring.c 

OBJS = $(subst .c,.o, $(SRC_LIST))

TARGETS = client

all: $(TARGETS)
$(TARGETS): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(INCLUDE)

$(OBJS): %.o : %.c
	$(CC) $(CFLAGS) -o $@ -c $< $(INCLUDE) 

clean:
	rm -fr *.o $(TARGETS)
