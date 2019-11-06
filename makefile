all:httpd client
LIBS = -lpthread
httpd:httpd.c
	gcc -g -W -Wall $(LIBS) -o $@ $<

client:miniclient.c
	gcc -W -Wall -o $@ $<

clean:
	rm httpd client
