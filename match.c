#include<mpi.h>
#include<stdio.h>
#include<stdlib.h>
#include<time.h>
#include<math.h>
#include<string.h>

#define HALF_ROUNDS 2700
#define NUM_PLAYERS 10
#define NUM_TEAMS 2
#define F0 10
#define F1 11
#define TEAMA 0
#define TEAMB 0
#define FIELD_HEIGHT 65
#define FIELD_WIDTH 129
#define TAG_ROUND_START 10000
#define TAG_PLAYER_SEND 20000
#define TAG_FIELD_STAT 30000
#define TAG_FIELD_SYNC_ADMIN 40000
#define TAG_FIELD_SYNC_PLAYER 50000
#define TAG_FIELD_ROUND_OVER 60000

#define SIZE_PLAYER_RECV 12
#define SIZE_PLAYER_SEND 6
#define SIZE_FIELD_ADMIN 5

#define INDEX_RANK 0
#define INDEX_NEWX 1
#define INDEX_NEWY 2
#define INDEX_CHALLENGE 3
#define INDEX_BALLX 4
#define INDEX_BALLY 5

#define INDEX_WINNER_RANK 0

#define FLAG_UPDATED -1

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

int absDist(int x, int y, int a, int b) {
    return abs(x - a) + abs(y - b);
}

int calcProb(int skill, int d) {
    int maxProb = 100;

    int prob = nearbyint((10+90.0*skill)/(0.5*d*sqrt(d) - 0.5));
    if (prob > maxProb) {
        prob = maxProb;
    }
    return prob;
}

void playerAction(Player player, int rank) {
    int recvbuf[SIZE_PLAYER_RECV];
    int sendbuf[SIZE_PLAYER_SEND];
    int teamId = rank / 5;
    int fieldProvider = teamId + 10;
    int ballPos[2];
    int round = 1;
    int side = 0;
    int scorePost[2] = {0, 32};
    if (teamId == TEAMA) {
        scorePost[0] = 128;
        scorePost[1] = 32;
    }
    do {

    MPI_Recv(recvbuf, SIZE_PLAYER_RECV, MPI_INT, fieldProvider, TAG_ROUND_START + round, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    ballPos[0] = recvbuf[0];
    ballPos[1] = recvbuf[1];

    sendbuf[INDEX_RANK] = rank;
    sendbuf[INDEX_CHALLENGE] = -1;
    sendbuf[INDEX_BALLX] = -1;
    sendbuf[INDEX_BALLY] = -1;

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
        int dist = absDist(scorePost[0], scorePost[1], player.x, player.y);
        int prob = calcProb(player.shoot, dist);
        if (prob > 1) {
            // Chance to hoop the ball
            sendbuf[INDEX_BALLX] = scorePost[0];
            sendbuf[INDEX_BALLY] = scorePost[1];
        } else {
            int minDist = 10000, throwX = -1, throwY = -1;
            int i;
            for (i = 2; i < SIZE_PLAYER_RECV; i+=2) {
                if ((i-2)/2 == rank) continue;
                int pX = recvbuf[i];
                int pY = recvbuf[i + 1];
                int d = absDist(scorePost[0], scorePost[1], pX, pY);
                if (d < minDist) {
                    minDist = d;
                    throwX = pX;
                    throwY = pY;
                }
            }

            sendbuf[INDEX_BALLX] = throwX;
            sendbuf[INDEX_BALLY] = throwY;
        }
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
    
    MPI_Send(sendbuf, SIZE_PLAYER_SEND, MPI_INT, fieldProcess, TAG_PLAYER_SEND + round, MPI_COMM_WORLD);
    if (round == HALF_ROUNDS) {
        //swap scorePost
        if (teamId == TEAMA) {
            scorePost[0] = 0;
            scorePost[1] = 32;
        } else {
            scorePost[0] = 128;
            scorePost[1] = 32;
        }
        side = 1;
    }
    round++;
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
    int teamPoints[NUM_TEAMS] = {0, 0};
    int scorePost[2][2] = {{128, 32}, {0, 32}};
    int side = 0;
    do {
    memset(recvbuf, 0, SIZE_PLAYER_SEND*NUM_PLAYERS*sizeof(int));
    sendbuf[0] = ballPos[0];
    sendbuf[1] = ballPos[1];

    for (i = 2; i < 12; i+=2) {
        // Put pos of all players in the same team
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
    while (true) {
        MPI_Testsome(NUM_PLAYERS, playerReqs, &outcount, outIndices, MPI_STATUS_IGNORE);
        if (outcount == MPI_UNDEFINED || outcount > 0) {

            if (outcount == MPI_UNDEFINED) {
                outcount = 10 - playersReceived[0];
                for (i = 0; i < 10; i++) {
                    outIndices[i] = i;
                }
            }
            for (i = 0; i < outcount; i++) {
                playersReceivedArray[outIndices[i]] = 1;
            }

            playersReceived[0] += outcount;
            if (rank == F1) {
                MPI_Request tempReq;
                MPI_Isend(playersReceived, 1, MPI_INT, F0, TAG_FIELD_STAT + round, MPI_COMM_WORLD, &tempReq);
                MPI_Request_free(&tempReq);
                if (playersReceived[0] == NUM_PLAYERS) {
                    MPI_Wait(&otherField, MPI_STATUS_IGNORE);
                }
            } else if (playersReceived[0] + playersOtherReceived[0] == NUM_PLAYERS) {
                MPI_Request tempReq;
                MPI_Isend(playersReceived, 1, MPI_INT, F1, TAG_FIELD_STAT + round, MPI_COMM_WORLD, &tempReq);
                MPI_Request_free(&tempReq);
                break;
            }
        }
        MPI_Test(&otherField, &flag, MPI_STATUS_IGNORE);
        if (flag) {
            if (rank == F1) {
                break;
            }
            if (playersReceived[0] + playersOtherReceived[0] == NUM_PLAYERS) {
                MPI_Request tempReq;
                MPI_Isend(playersReceived, 1, MPI_INT, F1, TAG_FIELD_STAT + round, MPI_COMM_WORLD, &tempReq);
                MPI_Request_free(&tempReq);
                break;
            }
            MPI_Irecv(playersOtherReceived, 1, MPI_INT, F1, TAG_FIELD_STAT + round, MPI_COMM_WORLD, &otherField);
        }
    }
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
           recvbuf[i*SIZE_PLAYER_SEND + INDEX_RANK] = FLAG_UPDATED;
           if (recvbuf[i*SIZE_PLAYER_SEND + INDEX_NEWX] == ballPos[0]
                && recvbuf[i*SIZE_PLAYER_SEND + INDEX_NEWY] == ballPos[1]) {
               int chall = recvbuf[i*SIZE_PLAYER_SEND + INDEX_CHALLENGE];
               if (chall > maxBallChallenge) {
                   maxBallChallenge = chall;
                   numReached = 0;
               }
               reachedRanks[numReached++] = i;
           }
        }
    }
    int winnerRank = -1;
    if (numReached > 0) {
        winnerRank = reachedRanks[rand() % numReached];
        int newBallPos[2];
        newBallPos[0] = recvbuf[winnerRank*SIZE_PLAYER_SEND + INDEX_BALLX];
        newBallPos[1] = recvbuf[winnerRank*SIZE_PLAYER_SEND + INDEX_BALLY];
        int d = abs(recvbuf[winnerRank*SIZE_PLAYER_SEND + INDEX_NEWX] - newBallPos[0]) +
                abs(recvbuf[winnerRank*SIZE_PLAYER_SEND + INDEX_NEWY] - newBallPos[1]);
        int winnerShootSkill = teamSkill[winnerRank/5][winnerRank%5][2];
        int prob = calcProb(winnerShootSkill, d);

        int hit = -1;
        if (prob > 0) {
            hit = rand() % (100 / prob);
        }

        ballPos[0] = newBallPos[0];
        ballPos[1] = newBallPos[1];
        if (hit != 0) {
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
        } else {
            if (ballPos[0] == scorePost[winnerRank/5][0] && ballPos[1] == scorePost[winnerRank/5][1]) {
                ballPos[0] = 64;
                ballPos[1] = 32;
                teamPoints[winnerRank/5] += (d > 24) ? 3:2;
            }
        }
    }
    int adminDetails[SIZE_FIELD_ADMIN] = {winnerRank, ballPos[0], ballPos[1], teamPoints[0], teamPoints[1]};
    int recvbuf2[NUM_PLAYERS*SIZE_PLAYER_SEND];
    int adminDetails2[SIZE_FIELD_ADMIN];
    memset(recvbuf2, 0, NUM_PLAYERS*SIZE_PLAYER_SEND*sizeof(int));

    MPI_Request temp, temp2, roundOverRequest;
    MPI_Isend(adminDetails, SIZE_FIELD_ADMIN, MPI_INT, otherFieldRank, TAG_FIELD_SYNC_ADMIN + round, MPI_COMM_WORLD, &temp);
    MPI_Request_free(&temp);
    MPI_Isend(recvbuf, NUM_PLAYERS*SIZE_PLAYER_SEND, MPI_INT, otherFieldRank, TAG_FIELD_SYNC_PLAYER + round, MPI_COMM_WORLD, &temp2);
    MPI_Request_free(&temp2);

    MPI_Recv(adminDetails2, SIZE_FIELD_ADMIN, MPI_INT, otherFieldRank, TAG_FIELD_SYNC_ADMIN + round, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    MPI_Recv(recvbuf2, NUM_PLAYERS*SIZE_PLAYER_SEND, MPI_INT, otherFieldRank, TAG_FIELD_SYNC_PLAYER + round, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    if (adminDetails2[INDEX_WINNER_RANK] != -1) {
       winnerRank = adminDetails2[INDEX_WINNER_RANK];
       ballPos[0] = adminDetails2[1];
       ballPos[1] = adminDetails2[2];
       teamPoints[0] = adminDetails2[3];
       teamPoints[1] = adminDetails2[4];
    }

    for (i = 0; i < NUM_PLAYERS; i++) {
        if (recvbuf2[i*SIZE_PLAYER_SEND + INDEX_RANK] == FLAG_UPDATED) {
            for (j = 1; j < SIZE_PLAYER_SEND; j++) {
                recvbuf[i*SIZE_PLAYER_SEND + j] = recvbuf2[i*SIZE_PLAYER_SEND + j];
            }
        }
    }
    if (rank == F1) {
        int tempVal[1];
        int iTeamId, iPlayerId;
        for (i = 0; i < NUM_PLAYERS; ++i) {
            iTeamId = i/5;
            iPlayerId = i%5;
            teamPos[iTeamId][iPlayerId][0] = recvbuf[i*SIZE_PLAYER_SEND + INDEX_NEWX];
            teamPos[iTeamId][iPlayerId][1] = recvbuf[i*SIZE_PLAYER_SEND + INDEX_NEWY];
        }
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
    } while(round <= 2*HALF_ROUNDS);
}

void main(int argc, char *argv[]) {
    //time_t start, end;
    //time(&start);
    int teamPos[2][5][2] = {{{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}},
                            {{128, 64}, {128, 64}, {128, 64}, {128, 64}, {128, 64}}};

    int teamSkill[2][5][3] = {{{2, 6, 7}, {4, 3, 8}, {2, 3, 10}, {3, 6, 6}, {5, 5, 5}},
    			      {{2, 6, 7}, {4, 3, 8}, {2, 3, 10}, {3, 6, 6}, {5, 5, 5}}};

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
    //time(&end);
    //printf("%lf", difftime(end, start));
}
