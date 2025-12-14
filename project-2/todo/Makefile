CXX	= g++
CFLAGS	= -Wall -g
MAKE	= make

PROGS	= np_simple np_single_proc

all:
	$(CXX) -c npshell.cpp $(CFLAGS) -o npshell.o
	$(CXX) np_simple.cpp npshell.o $(CFLAGS) -o np_simple
	$(CXX) np_single_proc.cpp npshell.o $(CFLAGS) -o np_single_proc

run_test: all
	cd ../spec/project-2-demo-sample-master/server && make
	cp -r ../spec/project-2-demo-sample-master/server/work_dir ./
	cp ./np_simple ./work_dir/
	cp ./np_single_proc ./work_dir/

clean:
	rm -f *~ $(PROGS)
	rm -f *.o

clean_run_test: clean
	for d in $(DIRS); do $(MAKE) -C $$d clean; done
	rm -rf work_dir

