all: run

run: smoke_test
	./smoke_test


smoke_test: test/smoke_test.cpp
	cmake -Bbuild -DCMAKE_EXPORT_COMPILE_COMMANDS=1
	cd build/; make


install:
	git clone https://github.com/google/googletest

