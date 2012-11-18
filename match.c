#include<mpi.h>
#include<stdio.h>
#include<stdlib.h>
#include<time.h>
#include<math.h>
#include<string.h>

const int HALF_ROUNDS = 2700;
const int NUM_PLAYERS = 10;
const int F0 = 10;
const int F1 = 11;
const int FIELD_HEIGHT = 65;
const int FIELD_WIDTH = 129;
const int TAG_ROUND_START = 10000;
const int TAG_PLAYER_SEND = 20000;
const int TAG_FIELD_STAT = 30000;
const int TAG_FIELD_SYNC_ADMIN = 40000;
const int TAG_FIELD_SYNC_PLAYER = 50000;
const int TAG_FIELD_ROUND_OVER = 60000;

const int SIZE_PLAYER_RECV = 12;
const int SIZE_PLAYER_SEND = 6;
const int SIZE_FIELD_SEND = 3;

const int INDEX_RANK = 0;
const int INDEX_NEWX = 1;
const int INDEX_NEWY = 2;
const int INDEX_CHALLENGE = 3;
const int INDEX_BALLX = 4;
const int INDEX_BALLY = 5;

const int INDEX_WINNER_RANK = 0;

const int FLAG_UPDATED = -1;

typedef int bool;
#define true 1
#define false 0

typedef struct {
    int x;
    int y;
    int speed;
    int dribble;
    int shoot;
} Player;

//TODO implement side switch
void playerAction(Player player, int rank) {
    int recvbuf[SIZE_PLAYER_RECV];
    int sendbuf[SIZE_PLAYER_SEND];
    int teamId = rank / 5;
    int fieldProvider = teamId + 10;
    int ballPos[2];
    int round = 1;
    int side = 0;
    do {

    MPI_Recv(recvbuf, SIZE_PLAYER_RECV, MPI_INT, fieldProvider, TAG_ROUND_START + round, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    ballPos[0] = recvbuf[0];
    ballPos[1] = recvbuf[1];

    sendbuf[INDEX_RANK] = rank;
    sendbuf[INDEX_CHALLENGE] = -1;
    sendbuf[INDEX_BALLX] = -1;
    sendbuf[INDEX_BALLY] = -1;
    //sendbuf[INDEX_SHOOT] = player.shoot;

    int mov = player.speed;
    int diffX = ballPos[0] - player.x;
    int diffY = ballPos[1] - player.y;
    int absDiffX = abs(diffX);
    int absDiffY = abs(diffY);

    // Player movement strategy
    if (mov >= absDiffX + absDiffY) {
        player.x = ballPos[0];
        player.y = ballPos[1];
        mov = absDiffX + absDiffY;
        // generate ball challenge to shoot
        sendbuf[INDEX_CHALLENGE] = (rand()%10 + 1)*player.dribble;
        sendbuf[INDEX_BALLX] = rand()%FIELD_WIDTH;
        sendbuf[INDEX_BALLY] = rand()%FIELD_HEIGHT;
        /*
            TODO
        sendbuf[6] = newballposx
        sendbuf[7] = newballposx
        */
    } else if (mov <= absDiffX) {
        player.x += mov*(diffX < 0 ? -1:1);
    } else if (mov <= absDiffY) {
        player.y += mov*(diffY < 0 ? -1:1);
    } else {
        player.x += diffX;
        player.y += (mov - absDiffX)*(diffY < 0 ? -1:1);
    }

    sendbuf[INDEX_NEWX] = player.x;
    sendbuf[INDEX_NEWY] = player.y;

    int fieldProcess = 10 + (player.x > 64);
    // Send field process data
    int j;
    /*
    printf("In player process before sending to field %d r%d ", fieldProcess, round);
    for (j = 0; j < SIZE_PLAYER_SEND; j++) {
        printf(" %d", sendbuf[j]);
    }
    printf("\n");
    */
    MPI_Send(sendbuf, SIZE_PLAYER_SEND, MPI_INT, fieldProcess, TAG_PLAYER_SEND + round, MPI_COMM_WORLD);
    //printf("field process %d accepted my send round%d rank %d\n", fieldProcess, round, rank);
    if (round == HALF_ROUNDS) {
        //swap scorePost
        side = 1;
    }
    round++;
    //printf("ball is at %d %d from %d\n", ballPos[0], ballPos[1], rank);
    } while(round <= 2*HALF_ROUNDS);
}

void fieldAction(int rank, int teamPos[2][5][2], int teamSkill[2][5][3]) {
    int ballPos[2] = {64, 32};
    int sendbuf[SIZE_PLAYER_RECV];
    int recvbuf[SIZE_PLAYER_SEND*NUM_PLAYERS];
    int teamId = rank - 10;
    int otherFieldRank = 21 - rank;
    int i, j;
    int round = 1;
    int teamPoints[2] = {0, 0};
    int scorePost[2][2] = {{128, 32}, {0, 32}};
    int side = 0;
    do {
    memset(recvbuf, 0, SIZE_PLAYER_SEND*NUM_PLAYERS*sizeof(int));
    sendbuf[0] = ballPos[0];
    sendbuf[1] = ballPos[1];

    for (i = 2; i < 12; i+=2) {
        // Put pos of all players in the same team
        // TODO Fix for F1
        sendbuf[i] = teamPos[teamId][i/2 - 1][0];
        sendbuf[i+1] = teamPos[teamId][i/2 - 1][1];
    }
    // Send to one team
    for (i = 0; i < 5; i++) {
        MPI_Request tempReq;
        MPI_Isend(sendbuf, SIZE_PLAYER_RECV, MPI_INT, 5*teamId + i, TAG_ROUND_START + round, MPI_COMM_WORLD, &tempReq);
        MPI_Request_free(&tempReq);
    }
    MPI_Request playerReqs[NUM_PLAYERS];
    MPI_Request otherField;

    for (i = 0; i < NUM_PLAYERS; i++) {
        MPI_Irecv(recvbuf + i*SIZE_PLAYER_SEND, SIZE_PLAYER_SEND, MPI_INT, i, TAG_PLAYER_SEND + round,
            MPI_COMM_WORLD, playerReqs + i);
    }

    int playersReceived[1] = {0};
    int playersOtherReceived[1] = {0};
    int flag = 0;
    MPI_Irecv(playersOtherReceived, 1, MPI_INT, 11 - teamId, TAG_FIELD_STAT + round, MPI_COMM_WORLD, &otherField);

    int outcount = 0;
    int outIndices[NUM_PLAYERS];
    int playersReceivedArray[NUM_PLAYERS];
    for (i = 0; i < NUM_PLAYERS; i++) {
        playersReceivedArray[i] = 0;
    }
    //printf("I am field before%d %d\n", rank, round);
    while (true) {
        MPI_Testsome(NUM_PLAYERS, playerReqs, &outcount, outIndices, MPI_STATUS_IGNORE);
        if (outcount == MPI_UNDEFINED || outcount > 0) {
            //printf("I am field inside %d with playersReceived %d and %d\n", rank, playersReceived[0], outcount);

            if (outcount == MPI_UNDEFINED) {
                printf("rank %d has a undef", rank);
                outcount = 10 - playersReceived[0];
                for (i = 0; i < 10; i++) {
                    outIndices[i] = i;
                }
            }
            for (i = 0; i < outcount; i++) {
                playersReceivedArray[outIndices[i]] = 1;
                int j = 0;
                
                printf("Array of received till now in field %d ", rank);
                for (j = 0; j < NUM_PLAYERS; j++) {
                    printf("%d ", playersReceivedArray[j]);
                }
                printf("\n");
                
            }

            playersReceived[0] += outcount;
            if (rank == F1) {
                MPI_Request tempReq;
                //printf("field %d with playersReceived %d and %d\n", rank, playersReceived[0], outcount);
                //int tempArray[1] = {playersReceived};
                MPI_Isend(playersReceived, 1, MPI_INT, F0, TAG_FIELD_STAT + round, MPI_COMM_WORLD, &tempReq);
                MPI_Request_free(&tempReq);
                if (playersReceived[0] == NUM_PLAYERS) {
                    MPI_Wait(&otherField, MPI_STATUS_IGNORE);
                }
            } else if (playersReceived[0] + playersOtherReceived[0] == NUM_PLAYERS) {
                MPI_Request tempReq;
                //printf("Field 0 says its done\n");
                MPI_Isend(playersReceived, 1, MPI_INT, F1, TAG_FIELD_STAT + round, MPI_COMM_WORLD, &tempReq);
                MPI_Request_free(&tempReq);
                break;
            }
        }
        MPI_Test(&otherField, &flag, MPI_STATUS_IGNORE);
        if (flag) {
            if (rank == F1) {
                //printf("Field 0 told me its done\n");
                break;
            }
            //playersReceived[0] += playersOtherReceived[0];
            //playersOtherReceived[0] = 0;
            if (playersReceived[0] + playersOtherReceived[0] == NUM_PLAYERS) {
                MPI_Request tempReq;
                MPI_Isend(playersReceived, 1, MPI_INT, F1, TAG_FIELD_STAT + round, MPI_COMM_WORLD, &tempReq);
                MPI_Request_free(&tempReq);
                break;
            }
            MPI_Irecv(playersOtherReceived, 1, MPI_INT, F1, TAG_FIELD_STAT + round, MPI_COMM_WORLD, &otherField);
        }
    }
    //printf("I am field %d %d\n", rank, round);
    // Cancel all extra team recv requests
    for (i = 0; i < NUM_PLAYERS; i++) {
        if (playerReqs[i] != MPI_REQUEST_NULL) {
            MPI_Request_free(&playerReqs[i]);
        }
    }
    int numReached = 0;
    int reachedRanks[NUM_PLAYERS];
    int maxBallChallenge = 1;
    for (i = 0; i < NUM_PLAYERS; i++) {
        if (playersReceivedArray[i]) {
            /*
            printf("In field %d ", rank);
            for (j = 0; j < SIZE_PLAYER_SEND; j++) {
                printf("%d ", recvbuf[i*SIZE_PLAYER_SEND + j]);
            }
            printf("\n");
            */
           recvbuf[i*SIZE_PLAYER_SEND + INDEX_RANK] = FLAG_UPDATED;
           if (recvbuf[i*SIZE_PLAYER_SEND + INDEX_NEWX] == ballPos[0]
                && recvbuf[i*SIZE_PLAYER_SEND + INDEX_NEWY] == ballPos[1]) {
               int chall = recvbuf[i*SIZE_PLAYER_SEND + INDEX_CHALLENGE];
               printf("challenge %d %d\n", chall, i);
               if (chall > maxBallChallenge) {
                   maxBallChallenge = chall;
                   numReached = 0;
               }
               reachedRanks[numReached++] = i;
           }
        }
    }
    printf("Number reached:%d", numReached);
    int winnerRank = -1;
    if (numReached > 0) {
        winnerRank = reachedRanks[rand() % numReached];
        int newBallPos[2];
        newBallPos[0] = recvbuf[winnerRank*SIZE_PLAYER_SEND + INDEX_BALLX];
        newBallPos[1] = recvbuf[winnerRank*SIZE_PLAYER_SEND + INDEX_BALLY];
        printf("winner rank for round %d is %d", round, winnerRank);
        for (i = 0; i < SIZE_PLAYER_SEND; i++) {
            printf(" %d", recvbuf[winnerRank*SIZE_PLAYER_SEND + i]);
        }
        printf("\n");
        int d = abs(recvbuf[winnerRank*SIZE_PLAYER_SEND + INDEX_NEWX] - newBallPos[0]) +
                abs(recvbuf[winnerRank*SIZE_PLAYER_SEND + INDEX_NEWY] - newBallPos[1]);
        int prob;
        int maxProb = 100;
        int winnerShootSkill = teamSkill[winnerRank/5][winnerRank%5][2];
            printf("d=%d\n", d);
            float valp= (0.5*d*sqrt(d) - 0.5);
            printf("p=%f\n", valp);
            float numer= (10+90.0*winnerShootSkill);
            printf("numer=%f\n", numer);
            printf("prob=%f\n", roundf(numer/valp));

        prob = roundf((10+90.0*winnerShootSkill)/(0.5*d*sqrt(d) - 0.5));
        if (prob > maxProb) {
            prob = maxProb;
        }
        printf("prob od=%d\n", prob);
        int val = -1;
        if (prob > 0) {
            val = rand() % (100 / prob);
        }
        ballPos[0] = newBallPos[0];
        ballPos[1] = newBallPos[1];
        if (val != 0) {
            // Random 8
            int x8 = rand() % 8 + 1;
            int y8 = rand() % 8 + 1;
            if (rand() % 2) x8 = -x8;
            if (rand() % 2) y8 = -y8;
            ballPos[0] = recvbuf[winnerRank*SIZE_PLAYER_SEND + INDEX_BALLX] + x8;
            ballPos[1] = recvbuf[winnerRank*SIZE_PLAYER_SEND + INDEX_BALLY] + y8;
            if (ballPos[0] < 0 || ballPos[1] < 0) {
                ballPos[0] = 0;
                ballPos[1] = 0;
            } else if (ballPos[0] > 128 || ballPos[1] > 64) {
                ballPos[0] = 128;
                ballPos[1] = 64;
            }
        } else if (ballPos[0] == scorePost[winnerRank/5][0] && ballPos[1] == scorePost[winnerRank/5][1]) {
            teamPoints[winnerRank/5] += (d > 24) ? 3:2;
        }
    }
    int adminDetails[3] = {winnerRank, ballPos[0], ballPos[1]};
    int recvbuf2[NUM_PLAYERS*SIZE_PLAYER_SEND];
    memset(recvbuf2, 0, NUM_PLAYERS*SIZE_PLAYER_SEND*sizeof(int));

    MPI_Request temp, temp2, roundOverRequest;
    MPI_Isend(adminDetails, 3, MPI_INT, otherFieldRank, TAG_FIELD_SYNC_ADMIN + round, MPI_COMM_WORLD, &temp);
    MPI_Request_free(&temp);
    MPI_Isend(recvbuf, NUM_PLAYERS*SIZE_PLAYER_SEND, MPI_INT, otherFieldRank, TAG_FIELD_SYNC_PLAYER + round, MPI_COMM_WORLD, &temp2);
    MPI_Request_free(&temp2);

    MPI_Recv(adminDetails, 3, MPI_INT, otherFieldRank, TAG_FIELD_SYNC_ADMIN + round, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    MPI_Recv(recvbuf2, NUM_PLAYERS*SIZE_PLAYER_SEND, MPI_INT, otherFieldRank, TAG_FIELD_SYNC_PLAYER + round, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    if (adminDetails[INDEX_WINNER_RANK] != -1) {
       winnerRank = adminDetails[INDEX_WINNER_RANK];
       ballPos[0] = adminDetails[1];
       ballPos[1] = adminDetails[2];
    }

    for (i = 0; i < NUM_PLAYERS; i++) {
        if (recvbuf2[i*SIZE_PLAYER_SEND + INDEX_RANK] == FLAG_UPDATED) {
            printf("Got updated values for rank %d", i);
            for (j = 1; j < SIZE_PLAYER_SEND; j++) {
                recvbuf[i*SIZE_PLAYER_SEND + j] = recvbuf2[i*SIZE_PLAYER_SEND + j];
                printf("%d ", recvbuf[i*SIZE_PLAYER_SEND + j]);
            }
            printf("\n");
        }
    }
    if (rank == F1) {
        int tempVal[1];
        MPI_Irecv(tempVal, 1, MPI_INT, F0, TAG_FIELD_ROUND_OVER + round, MPI_COMM_WORLD, &roundOverRequest);
        MPI_Wait(&roundOverRequest, MPI_STATUS_IGNORE);
        round++;
        continue;
    }
    // Printing function
    printf("%d\n", round);
    printf("%d %d\n", teamPoints[0], teamPoints[1]);
    printf("%d %d\n", ballPos[0], ballPos[1]); //New pos
    int newX, newY, iTeamId, iPlayerId;
    for (i = 0; i < NUM_PLAYERS; ++i) {
        iTeamId = i/5;
        iPlayerId = i%5;
        printf("%d ", i); // Player number
        printf("%d %d ", teamPos[iTeamId][iPlayerId][0], teamPos[iTeamId][iPlayerId][1]);
        teamPos[iTeamId][iPlayerId][0] = recvbuf[i*SIZE_PLAYER_SEND + INDEX_NEWX];
        teamPos[iTeamId][iPlayerId][1] = recvbuf[i*SIZE_PLAYER_SEND + INDEX_NEWY];
        printf("%d %d ", teamPos[iTeamId][iPlayerId][0], teamPos[iTeamId][iPlayerId][1]);
        printf("%d ", recvbuf[i*SIZE_PLAYER_SEND + INDEX_CHALLENGE] > -1);
        printf("%d ", winnerRank == i);
        printf("%d ", recvbuf[i*SIZE_PLAYER_SEND + INDEX_CHALLENGE]);
        if (winnerRank == i) {
            printf("%d ", recvbuf[i*SIZE_PLAYER_SEND + INDEX_BALLX]);
            printf("%d", recvbuf[i*SIZE_PLAYER_SEND + INDEX_BALLY]);
        } else {
            printf("-1 -1");
        }
        printf("\n");
     }
    if (round == HALF_ROUNDS) {
        scorePost[0][0] = 0;
        scorePost[0][1] = 32;
        scorePost[1][0] = 128;
        scorePost[1][1] = 32;
    }
        int tempVal[1] = {1};
        MPI_Send(tempVal, 1, MPI_INT, F1, TAG_FIELD_ROUND_OVER + round, MPI_COMM_WORLD);
        round++;
        //printf("Sending printing over %d\n", round);
    } while(round <= 2*HALF_ROUNDS);
}

void main(int argc, char *argv[]) {
    int teamPos[2][5][2] = {{{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}},
                            {{128, 64}, {128, 64}, {128, 64}, {128, 64}, {128, 64}}};

    int teamSkill[2][5][3] = {{{5, 5, 5}, {5, 5, 5}, {5, 5, 5}, {5, 5, 5}, {5, 5, 5}},
                              {{5, 5, 5}, {5, 5, 5}, {5, 5, 5}, {5, 5, 5}, {5, 5, 5}}};

    int rank, numtasks, isPlayer, winnerRank;
    int teamA[5] = {0, 1, 2, 3, 4}, teamB[5] = {5, 6, 7, 8, 9};

    MPI_Init(&argc, &argv);

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    isPlayer = rank < 10;
    int round = 1;
    srand(time(NULL) + rank);
    Player player;

    if (isPlayer) {
       int teamId = rank/5;
       int playerNum = rank % 5;
       player.x = teamPos[teamId][playerNum][0];
       player.y = teamPos[teamId][playerNum][1];
       player.speed = teamSkill[teamId][playerNum][0];
       player.dribble = teamSkill[teamId][playerNum][1];
       player.shoot = teamSkill[teamId][playerNum][2];
       playerAction(player, rank);
    } else {
       fieldAction(rank, teamPos, teamSkill);
    }

    MPI_Finalize();
}
