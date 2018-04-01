GEO_INSTALL_PATH=$(HOME)/$(shell hostname | sed -e "s/[0-9]*$$//")/local
LIBMSR_INSTALL_PATH=$(HOME)/$(shell hostname | sed -e "s/[0-9]*$$//")/local
CALIPER_DIR=$(HOME)/$(shell hostname | sed -e "s/[0-9]*$$//")/local

all: main.c
	mpicc -o main.orig main.c
	mpicc -o main.geo main.c -D__D_GEO__ -I$(GEO_INSTALL_PATH)/include \
				-L$(GEO_INSTALL_PATH)/lib -L$(LIBMSR_INSTALL_PATH)/lib \
	            -Xlinker -rpath $(GEO_INSTALL_PATH)/lib \
				-Xlinker -rpath $(LIBMSR_INSTALL_PATH)/lib -lgeopm -lmsr -fopenmp
	mpicc -o main.geo.omp main.c -D__OPENMP__ -D__D_GEO__ -I$(GEO_INSTALL_PATH)/include \
				-L$(GEO_INSTALL_PATH)/lib -L$(LIBMSR_INSTALL_PATH)/lib \
	            -Xlinker -rpath $(GEO_INSTALL_PATH)/lib \
				-Xlinker -rpath $(LIBMSR_INSTALL_PATH)/lib -lgeopm -lmsr -fopenmp
#	mpicc -o main.caliapp main.c -D__D_CALI__ -I$(CALIPER_DIR)/include/caliper \
#				-L$(CALIPER_DIR)/lib -L$(CALIPER_DIR)/lib64 -lcaliper -lcaliper-common \
#				-lcaliper-mpiwrap -lcaliper-reader -Xlinker \
#				-rpath $(CALIPER_DIR)/lib -Xlinker -rpath $(CALIPER_DIR)/lib64
	mpicc -o main.caligeo main.c -D__D_CALI__ -I$(CALIPER_DIR)/include/caliper \
				-L$(CALIPER_DIR)/lib -L$(CALIPER_DIR)/lib64 -lcaliper -lcaliper-common \
				-lcaliper-mpiwrap -lcaliper-reader -Xlinker \
				-rpath $(CALIPER_DIR)/lib -Xlinker -rpath $(CALIPER_DIR)/lib64 \
				-L$(GEO_INSTALL_PATH)/lib -L$(LIBMSR_INSTALL_PATH)/lib \
				-Xlinker -rpath $(GEO_INSTALL_PATH)/lib \
				-Xlinker -rpath $(LIBMSR_INSTALL_PATH)/lib -lgeopm -lmsr -fopenmp
	mpicc -o main.caligeo.omp main.c -D__OPENMP__ -D__D_CALI__ -I$(CALIPER_DIR)/include/caliper \
				-L$(CALIPER_DIR)/lib -L$(CALIPER_DIR)/lib64 -lcaliper -lcaliper-common \
				-lcaliper-mpiwrap -lcaliper-reader -Xlinker \
				-rpath $(CALIPER_DIR)/lib -Xlinker -rpath $(CALIPER_DIR)/lib64 \
				-L$(GEO_INSTALL_PATH)/lib -L$(LIBMSR_INSTALL_PATH)/lib \
				-Xlinker -rpath $(GEO_INSTALL_PATH)/lib \
				-Xlinker -rpath $(LIBMSR_INSTALL_PATH)/lib -lgeopm -lmsr -fopenmp
clean:
	/bin/rm -f main.geo main.orig main.caligeo.omp main.caligeo main.geo.omp
