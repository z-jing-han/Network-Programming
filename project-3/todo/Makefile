CXX	= g++
CFLAGS	= -Wall -g
MAKE	= make

PROGS	= np_multi_proc

all:
	$(CXX) np_multi_proc.cpp $(CFLAGS) -o np_multi_proc

test: all
	cd ../spec/project-3-demo-sample-main/server && make
	cp -r ../spec/project-3-demo-sample-main/server/work_dir/ ./
	cp np_multi_proc ./work_dir/

clean:
	rm -f *~ $(PROGS)

clean_test: clean
	rm -rf work_dir
