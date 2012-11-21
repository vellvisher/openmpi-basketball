#include<mpi.h>
#include<stdio.h>
#include<stdlib.h>
#include<time.h>

const int TRAINING_ROUNDS = 900;
const int NUM_PLAYERS = 5;
const int FIELD = 0;
const int FIELD_HEIGHT = 65;
const int FIELD_WIDTH = 129;
#define SIZE_PLAYER_SEND 8
const int INDEX_PASSES = 7;

typedef int bool;
#define true 1
#define false 0

typedef struct {
    int x;
    int y;
    int feet;
    int reaches;
    int passes;
} Player;

void main(int argc, char *argv[]) {
    int rank, numtasks, isPlayer, winnerRank;
    int ballPos[2];
    int withBall;
    int sendbuf[SIZE_PLAYER_SEND];
    int recvbuf[SIZE_PLAYER_SEND*NUM_PLAYERS];
    MPI_Init(&argc, &argv);
    // MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    // 0 rank is always the field process
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    isPlayer = rank;
    int round = 1;
    int reachedRanks[NUM_PLAYERS];
    srand(time(NULL) + rank);
    Player player;

    if (isPlayer) {
       player.x = rank;
       player.y = FIELD_HEIGHT;
       player.feet = 0;
       player.reaches = 0;
       player.passes = 0;
    } else {
       ballPos[0] = rand() % FIELD_WIDTH;
       ballPos[1] = rand() % FIELD_HEIGHT;
    }

    do {
        MPI_Bcast(&ballPos, 2, MPI_INT, FIELD, MPI_COMM_WORLD);

        if (isPlayer) {
            withBall = false;

            sendbuf[0] = player.x;
            sendbuf[1] = player.y;

            int mov = rand() % 10 + 1;
            int diffX = ballPos[0] - player.x;
            int diffY = ballPos[1] - player.y;
            int absDiffX = abs(diffX);
            int absDiffY = abs(diffY);

            if (mov >= absDiffX + absDiffY) {
               player.x = ballPos[0];
               player.y = ballPos[1];
               ++player.reaches;
               mov = absDiffX + absDiffY;
               withBall = true;
            } else if (mov <= absDiffX) {
                player.x += mov*(diffX < 0 ? -1:1);
            } else if (mov <= absDiffY) {
                player.y += mov*(diffY < 0 ? -1:1);
            } else {
                player.x += diffX;
                player.y += (mov - absDiffX)*(diffY < 0 ? -1:1);
            }
            player.feet += mov;
        }

        sendbuf[2] = player.x;
        sendbuf[3] = player.y;
        sendbuf[4] = withBall;
        sendbuf[5] = player.feet;
        sendbuf[6] = player.reaches;
        sendbuf[INDEX_PASSES] = player.passes;

        MPI_Gather(&sendbuf, SIZE_PLAYER_SEND, MPI_INT, &recvbuf, SIZE_PLAYER_SEND, MPI_INT, FIELD, MPI_COMM_WORLD);
        if (!isPlayer) {
            int i, j, numReached = 0;
            for (i = 1; i < NUM_PLAYERS + 1; i++) {
                if (recvbuf[4 + i*SIZE_PLAYER_SEND]) {
                    reachedRanks[numReached] = i;
                    ++numReached;
                }
            }
            if (numReached > 0) {
                winnerRank = reachedRanks[rand() % numReached];
                int newX = ballPos[0];
                int newY = ballPos[1];

                while(newX == ballPos[0] && newY == ballPos[1]) {
                    newX = rand() % FIELD_WIDTH;
                    newY = rand() % FIELD_HEIGHT;
                }
                ballPos[0] = newX;
                ballPos[1] = newY;
                ++recvbuf[INDEX_PASSES + winnerRank*SIZE_PLAYER_SEND];

            } else {
                winnerRank = -1;
            }
            // Printing function
            printf("%d\n", round);
            printf("%d %d\n", ballPos[0], ballPos[1]); //New pos
            for (i = 1; i < NUM_PLAYERS+1; ++i) {
                printf("%d ", i - 1); // Player number
                for (j = 0; j < 5; ++j)
                    printf("%d ", recvbuf[j + i*SIZE_PLAYER_SEND]);
                printf("%d ", i == winnerRank);
                for (j = 5; j < 8; ++j)
                    printf("%d ", recvbuf[j + i*SIZE_PLAYER_SEND]);
                printf("\n");
            }

        }

        MPI_Scatter(&recvbuf, SIZE_PLAYER_SEND, MPI_INT, &sendbuf, SIZE_PLAYER_SEND, MPI_INT, FIELD, MPI_COMM_WORLD);
        if (isPlayer) {
            player.passes = sendbuf[INDEX_PASSES];
        }
        MPI_Barrier(MPI_COMM_WORLD);

        round++;
    } while (round <= TRAINING_ROUNDS);

    MPI_Finalize();
}
