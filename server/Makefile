CC = gcc
CFLAGS = -g -O0 -lpthread

INCLUDE = -I./

LIBS = -L./ 
LIBS = -lpthread 

SRC_LIST = le1w_svr.c \
		   redo.c \
		   avl_tree.c \
		   list.c \
		   ring.c

OBJS = $(subst .c,.o, $(SRC_LIST))

TARGETS = server

all: $(TARGETS)
$(TARGETS): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(INCLUDE)

$(OBJS): %.o : %.c
	$(CC) $(CFLAGS) -o $@ -c $< $(INCLUDE) 

clean:
	rm -fr *.o $(TARGETS)
