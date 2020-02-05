make clean && make -j
# Advection
./hypsys
#./phypsys
#mpirun -np 4 ./phypsys
./hypsys -vs 1000 -m data/inline-4quad.mesh -dt 0.0001 -o 2 -s 1 -r 3 -c 0
#./phypsys -vs 1000 -m data/inline-4quad.mesh -dt 0.0001 -o 2 -s 1 -r 3 -c 0
#mpirun -np 4 ./phypsys -vs 1000 -m data/inline-4quad.mesh -dt 0.0001 -o 2 -s 1 -r 3 -c 0
./hypsys -p 3 -c 0 -tf 0




# ./hypsys -m data/periodic-square.mesh -o 1 -r 4
#
# mpirun -np 2 valgrind --leak-check=yes ./par-hypsys -tf 0.1 -r 0