#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <unistd.h>

#ifdef SCOREP_USER_ENABLE
#include <scorep/SCOREP_User.h>
#endif //SCOREP_USER_ENABLE

void check_error(int mpi_error) {
    if (mpi_error != MPI_SUCCESS) {
        printf("Error on line: %d\n", __LINE__);
        exit(1);
    }
}

void do_work(int rank) {
#ifdef SCOREP_USER_ENABLE
    SCOREP_USER_REGION( "do_work_region", SCOREP_USER_REGION_TYPE_FUNCTION )
#endif //SCOREP_USER_ENABLE

    int nelements      = 1;
    MPI_Datatype dtype = MPI_INT;

    if (rank == 0) {
        printf("Rank 0\n");
        // Send 2 messages. Wait 5 seconds. Send 2 more messages. Exit.
        int message        = 1313;
        int dest           = 1;
        MPI_Request req[4];
        MPI_Status stat[4];
        int flag [4] = {0};

        check_error( MPI_Isend(&message, nelements, dtype, dest, 1, MPI_COMM_WORLD, &req[0]) );
        check_error( MPI_Isend(&message, nelements, dtype, dest, 2, MPI_COMM_WORLD, &req[1]) );

        sleep(5);

        check_error( MPI_Isend(&message, nelements, dtype, dest, 3, MPI_COMM_WORLD, &req[2]) );
        check_error( MPI_Isend(&message, nelements, dtype, dest, 4, MPI_COMM_WORLD, &req[3]) );

        while (!(flag[0] && flag[1] && flag[2] && flag[3])) {
            for (int i = 0; i < 4; i++) {
                check_error( MPI_Test(&req[i], &flag[i], &stat[i]) );
            }
            sleep(1);
        }

        printf("Rank 0 done sending\n");
    } else if (rank == 1){
        printf("Rank 1\n");
        int source = 0;
        int message[4];

        MPI_Request req[4];
        MPI_Status stat[4];
        int flag[4];

        check_error( MPI_Irecv(&message[0], nelements, dtype, source, 1, MPI_COMM_WORLD, &req[0]) );
        check_error( MPI_Irecv(&message[1], nelements, dtype, source, 2, MPI_COMM_WORLD, &req[1]) );
        check_error( MPI_Irecv(&message[2], nelements, dtype, source, 3, MPI_COMM_WORLD, &req[2]) );
        check_error( MPI_Irecv(&message[3], nelements, dtype, source, 4, MPI_COMM_WORLD, &req[3]) );

        while (!(flag[0] && flag[1] && flag[2] && flag[3])) {
            for (int i = 0; i < 4; i++) {
                check_error( MPI_Test(&req[i], &flag[i], &stat[i]) );
            }
            sleep(1);
        }

        printf("Rank 1 done receiving\n");
        printf("Rank 1 recieved:");
        for (int i = 0; i < 4; i++) {
            printf(" %d", message[i]);
        }
        printf("\n");
    }

}

int main(int argc, char **argv) {

    int rank;
    int mpi_error;

    MPI_Init(&argc, &argv);

    mpi_error = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    check_error(mpi_error);

    int size;
    mpi_error = MPI_Comm_size(MPI_COMM_WORLD, &size);
    check_error(mpi_error);

    if (size < 2) {
        printf("At least 2 ranks are needed.\n");
        exit(1);
    }

    do_work(rank);


    MPI_Finalize();

}
