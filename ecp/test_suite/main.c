/* Test application for GEOPM Caliper service.
 * The application tests the following GEOPM annotations and mappings:
 * (GEOPM) geopm_prof_region   -- (Caliper) first occurrence of 
 *                              phase start, loop begin, function start
 * (GEOPM) geopm_prof_enter    -- (Caliper) phase start, loop begin, 
 *                              function start
 * (GEOPM) geopm_prof_exit     -- (Caliper) phase end, loop end, 
 *                              function end
 * (GEOPM) geopm_prof_progress -- (Caliper) end of iteration#xxx attribute
 * (GEOPM) geopm_prof_epoch    -- (Caliper) iteration#mainloop end
 * (GEOPM) geopm_tprof_create  -- Deprecated (Caliper) update to xxx.loopcount
 * (GEOPM) geopm_tprof_destroy -- Deprecated (Caliper) loop end
 * (GEOPM) geopm_tprof_increment -- Deprecated (Caliper) end of iteration#xxx attribute
 * (GEOPM) geopm_tprof_init    -- (Caliper) declare a parallel region
 * (GEOPM) geopm_tprof_post    -- (Caliper) end of iteration#xxx
 *
 * The application builds five binaries: 
 *  main.orig:          original application (without OpenMP),
 *  main.geo:           with GEOPM markup,
 *  main.geo.omp:       with GEOPM markup and OpenMP computation phase,
 *  main.caligeo:       with Caliper markup, and
 *  main.caligeo.omp:   with Caliper markup and OpenMP computation phase
 *  
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <mpi.h>

#ifdef __D_CALI__
#include <cali.h>  
#include <cali_macros.h>  
#endif

#ifdef __D_GEO__
#include <geopm.h>
#endif

#ifdef __OPENMP__
#include <omp.h>
#endif

#ifdef __D_GEO__
uint64_t func_send_recv_rid;
#endif

void do_send_recv(int rank, char *buff_ptr, char **lbuff, long buffsize, int proto, long u_sleep_time) {
#ifdef __D_CALI__
    CALI_MARK_FUNCTION_BEGIN;
#endif
    #ifdef __D_GEO__
        geopm_prof_enter(func_send_recv_rid);
    #endif
    int tag=1;
    MPI_Request req;
    MPI_Status stat;

    if(rank%2 != 0) {
        if(proto==1) {
            MPI_Send(buff_ptr, buffsize, MPI_CHAR, rank-1, tag, MPI_COMM_WORLD);
        } else {
            memcpy(*lbuff, buff_ptr, buffsize);
            MPI_Isend(*lbuff, buffsize, MPI_CHAR, rank-1, tag, MPI_COMM_WORLD, &req);
        }
    }
    else {
        usleep(u_sleep_time);
        MPI_Recv(buff_ptr, buffsize, MPI_CHAR, rank+1, tag, MPI_COMM_WORLD, &stat);
    }

    #ifdef __D_GEO__
        geopm_prof_exit(func_send_recv_rid);
    #endif
#ifdef __D_CALI__
    CALI_MARK_FUNCTION_END;
#endif

}

#ifdef __D_GEO__
void do_compute_p1(long liter, long biter, uint64_t mainloop_rid, char *buff_ptr, long buffsize, long u_sleep_time) {
#else
void do_compute_p1(long liter, long biter, char *buff_ptr, long buffsize, long u_sleep_time) {
#endif    
        #ifdef __D_CALI__           
            CALI_MARK_LOOP_BEGIN(mainloop, "mainloop");
        #endif
            #ifdef __D_GEO__
                geopm_prof_enter(mainloop_rid);
            #endif
            for(liter = 0; liter < u_sleep_time * 10; liter++) {
            #ifdef __D_CALI__           
//                CALI_MARK_ITERATION_BEGIN_DOUBLE(mainloop, liter/(u_sleep_time * 10));
                CALI_MARK_ITERATION_BEGIN(mainloop, liter);
            #endif
                for(biter = 0; biter < buffsize; biter++) {
                    buff_ptr[biter] += (u_sleep_time + liter) * biter;
                }
                #ifdef __D_GEO__
                    geopm_prof_progress(mainloop_rid, liter);
                #endif
                #ifdef __D_GEO__
                    geopm_prof_epoch();
                #endif
            #ifdef __D_CALI__           
                CALI_MARK_ITERATION_END(mainloop);
            #endif
            }
            #ifdef __D_GEO__
                geopm_prof_exit(mainloop_rid);
            #endif
        #ifdef __D_CALI__           
            CALI_MARK_LOOP_END(mainloop);
        #endif
}

#ifdef __OPENMP__
  #ifdef __D_GEO__
    static int do_compute_p2(uint64_t mainloop_rid, size_t num_stream, double scalar, double *a, double *b, double *c)
  #else
    static int do_compute_p2(size_t num_stream, double scalar, double *a, double *b, double *c)
  #endif
{
    const size_t block = 256;
    const size_t num_block = num_stream / block;
    const size_t num_remain = num_stream % block;
    int err = 0;
    int num_thread = 1;
    struct geopm_tprof_c *tprof = NULL;

    #ifdef __D_CALI__           
        cali_set_int_byname("mainloop.loopcount", num_block);
        CALI_MARK_LOOP_BEGIN(mainloop, "mainloop");
    #endif
        #ifdef __D_GEO__
            geopm_prof_enter(mainloop_rid);
        #endif
    #pragma omp parallel
    {
        num_thread = omp_get_num_threads();
    }
#ifdef __D_GEO__
    err = geopm_tprof_init(num_block);
#endif
    if (!err) {
        #pragma omp parallel
        {
                int thread_idx = omp_get_thread_num();
                size_t i;
        #pragma omp for
                for (i = 0; i < num_block; ++i) {
            #ifdef __D_CALI__           
//                CALI_MARK_ITERATION_BEGIN_DOUBLE(mainloop, liter/(u_sleep_time * 10));
                CALI_MARK_ITERATION_BEGIN(mainloop, i);
            #endif
                    size_t j;
                    for (j = 0; j < block; ++j) {
                        a[i * block + j] = b[i * block + j] + scalar * c[i * block + j];
                    }
                #ifdef __D_GEO__
                    geopm_tprof_post();
                #endif
            #ifdef __D_CALI__           
                CALI_MARK_ITERATION_END(mainloop);
            #endif
                }
                size_t j;
        #pragma omp for
                for (j = 0; j < num_remain; ++j) {
                    a[num_block * block + j] = b[num_block * block + j] + scalar * c[num_block * block + j];
                }
        }
    }

        #ifdef __D_GEO__
            geopm_prof_exit(mainloop_rid);
        #endif
    #ifdef __D_CALI__           
        CALI_MARK_LOOP_END(mainloop);
    #endif
    return err;
}
#endif

int main(int argc, char *argv[]) {
    
    int numtasks, source=0, dest, tag=1, i;
    int provided;

    int val = 1;
    int iter;
    char *buff_ptr, *local_buff;
    long buffsize=200000;
    int total_iter=2;
    int proto=1;
    long u_sleep_time=1;

    double t1, t2;
    MPI_Status stat;
    MPI_Request req;

    int rank;
    MPI_Init( &argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);

    buff_ptr = (char *)malloc(buffsize);
    local_buff=(char *)malloc(buffsize);

    double w1 = MPI_Wtime();
    long liter, biter;

#ifdef __D_GEO__
    uint64_t comm_rid;
    uint64_t comp_rid;
    uint64_t mainloop_rid;
    geopm_prof_region("comm-phase", GEOPM_REGION_HINT_COMPUTE, &comm_rid);
    geopm_prof_region("comp-phase", GEOPM_REGION_HINT_COMPUTE, &comp_rid);
    geopm_prof_region("mainloop", GEOPM_REGION_HINT_COMPUTE, &mainloop_rid);
    geopm_prof_region("f_send_recv", GEOPM_REGION_HINT_COMPUTE, &func_send_recv_rid);
#endif
    
#ifdef __OPENMP__
    //size_t num_stream = 0.2 * 500000000;
    size_t num_stream = 0.2 * 5000;
    double *a = NULL;
    double *b = NULL;
    double *c = NULL;

    size_t cline_size = 64;
    size_t mem_size = sizeof(double) * num_stream;
    int err = posix_memalign((void *)&a, cline_size, mem_size);
    if (!err) {
        err = posix_memalign((void *)&b, cline_size, mem_size);
    }
    if (!err) {
        err = posix_memalign((void *)&c, cline_size, mem_size);
    }
    if (!err) {
        int i;
  #pragma omp parallel for
        for (i = 0; i < num_stream; i++) {
            a[i] = 0.0;
            b[i] = 1.0;
            c[i] = 2.0;
        }
    }
#endif

    for(proto = 1; proto < 3; proto++) {
        for(iter = 0;iter<total_iter;iter++) {


        #ifdef __D_CALI__
            CALI_MARK_BEGIN("comm-phase");
        #endif            
            #ifdef __D_GEO__
                geopm_prof_enter(comm_rid);
            #endif
            do_send_recv(rank, buff_ptr, &local_buff, buffsize, proto, u_sleep_time);
            #ifdef __D_GEO__
                geopm_prof_exit(comm_rid);
            #endif
        #ifdef __D_CALI__
            CALI_MARK_END("comm-phase");
        #endif

        #ifdef __D_CALI__
            CALI_MARK_BEGIN("compute-phase");
        #endif            
            #ifdef __D_GEO__
                geopm_prof_enter(comp_rid);
            #endif
                #ifdef __OPENMP__
                    #ifdef __D_GEO__
                    do_compute_p2(mainloop_rid, num_stream,3,a,b,c);
                    #else
                    do_compute_p2(num_stream,3,a,b,c);
                    #endif
                #else
                    #ifdef __D_GEO__
                    do_compute_p1(liter, biter, mainloop_rid, buff_ptr, buffsize, u_sleep_time);
                    #else
                    do_compute_p1(liter, biter, buff_ptr, buffsize, u_sleep_time);
                    #endif
                #endif
            #ifdef __D_GEO__
                geopm_prof_exit(comp_rid);
            #endif
        #ifdef __D_CALI__
            CALI_MARK_END("compute-phase");
        #endif            

            MPI_Barrier(MPI_COMM_WORLD);
        }
    }

    printf("calling Finalize\n");
    MPI_Finalize();
}

