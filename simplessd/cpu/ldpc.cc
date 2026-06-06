// This code is only used for counting the CPU overhead of LDPC

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#define N 4096   // Number of matrix columns (codeword length)
#define M 2048    // Number of matrix rows
#define WC 3   // Number of 1s per column
#define WR 6   // Number of 1s per row
#define K (N-M) // Number of information bits
#define MAX_ITER 50 // Maximum iteration count

void generateLDPC(int H[M][N]) {
    int row_count[M] = {0};
    int col_count[N] = {0};
    int i, j, rnd;

    srand(time(NULL)); // Initialize random number generator

    // Initialize matrix
    for (i = 0; i < M; i++) {
        for (j = 0; j < N; j++) {
            H[i][j] = 0;
        }
    }

    // Fill matrix
    for (i = 0; i < M; i++) {
        for (j = 0; j < WR; j++) {
            do {
                rnd = rand() % N;
            } while (col_count[rnd] >= WC || H[i][rnd] == 1);
            H[i][rnd] = 1;
            col_count[rnd]++;
        }
    }
}

void encode(int *input, int *output, int H[M][N]) {
    for (int i = 0; i < K; i++) {
        output[i] = input[i];
    }

    for (int j = K; j < N; j++) {
        output[j] = 0;
        for (int i = 0; i < K; i++) {
            if (H[j-K][i] == 1) {
                output[j] ^= input[i];
            }
        }
    }
}

void decode(int *received, int *decoded, int H[M][N]) {
    double p1[N], p0[N], r[M][N];
    int i, j, k, iter, l;

    // Initialize probabilities
    for (j = 0; j < N; j++) {
        p1[j] = received[j] == 1 ? 0.75 : 0.25; // Simplified channel model
        p0[j] = 1 - p1[j];
    }

    // Belief propagation
    for (iter = 0; iter < MAX_ITER; iter++) {
        // Messages from variable nodes to check nodes
        for (i = 0; i < M; i++) {
            for (j = 0; j < N; j++) {
                if (H[i][j]) {
                    double product = 1.0;
                    for (k = 0; k < N; k++) {
                        if (k != j && H[i][k]) {
                            product *= (2 * p1[k] - 1);
                        }
                    }
                    r[i][j] = (1 + product) / 2;
                }
            }
        }

        // Messages from check nodes back to variable nodes
        for (j = 0; j < N; j++) {
            double q1 = p1[j], q0 = p0[j];
            for (i = 0; i < M; i++) {
                if (H[i][j]) {
                    double temp = 2 * r[i][j] - 1;
                    q1 *= temp;
                    q0 *= -temp;
                }
            }
            p1[j] = q1 / (q1 + q0);
            p0[j] = 1 - p1[j];
        }
    }

    // Hard decision
    for (j = 0; j < K; j++) {
        decoded[j] = p1[j] > 0.5 ? 1 : 0;
    }
}

int main() {
    int H[M][N]; // Parity-check matrix
    int input[K] = {1, 0, 1, 0, 1, 0}; // Example information bits
    int encoded[N]; // Encoded codeword
    int decoded[K]; // Decoded information bits

    generateLDPC(H);
    encode(input, encoded, H);
    decode(encoded, decoded, H);

    printf("Encoded data: ");
    for (int i = 0; i < N; i++) {
        printf("%d ", encoded[i]);
    }
    printf("\n");

    printf("Decoded data: ");
    for (int i = 0; i < K; i++) {
        printf("%d ", decoded[i]);
    }
    printf("\n");

    return 0;
}
