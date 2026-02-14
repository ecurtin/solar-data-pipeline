all: battery_target_setter.cpp
	g++ -g -std=c++17 -o battery_target_setter -O0 battery_target_setter.cpp -I../../mlpack/src/ -I../../ensmallen/include/ -larmadillo -lmosquitto -lInfluxDB -lcpr

clean:
	rm -f battery_target_setter
