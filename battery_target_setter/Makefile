all: battery_target_setter.cpp
	g++ -std=c++17 -o battery_target_setter -O3 battery_target_setter.cpp -I../../mlpack/src/ -I../../ensmallen/include/ -larmadillo -lmosquitto

clean:
	rm -f battery_target_setter
