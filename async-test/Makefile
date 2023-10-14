all: async-test async-test-mpip async-test-scorep

async-test: main.cc
	CC main.cc -o async-test

async-test-mpip: main.cc
	CC main.cc -o async-test-mpip -L/global/homes/p/prlavin/spack/opt/spack/linux-sles15-zen3/gcc-11.2.0/binutils-2.40-fwse67sdbg6xstibz45xvhjfqe4gyh4t/lib/ -L/global/homes/p/prlavin/agile/A-mpiP -lmpiP

async-test-scorep: main.cc
	scorep CC main.cc -o async-test-scorep

clean:
	rm -f aync-test async-test-mpip async-test-scorep

.PHONY: all clean
