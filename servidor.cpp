#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <algorithm>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstring>  // Para memset y std::strlen

#define PORT 7777
#define ROWS 6
#define COLUMNS 7
#define EMPTY 0
#define CLIENT1 1
#define CLIENT2 2

using namespace std;

struct Game {
    int board[ROWS][COLUMNS];
    int currentPlayer;
    bool isRunning;
    int clientSockets[2];
};

vector<Game*> games;
pthread_mutex_t gamesMutex = PTHREAD_MUTEX_INITIALIZER;

void initializeGame(Game &game) {
    for (int i = 0; i < ROWS; ++i) {
        for (int j = 0; j < COLUMNS; ++j) {
            game.board[i][j] = EMPTY;
        }
    }
    game.currentPlayer = CLIENT1;
    game.isRunning = true;
}

void printBoard(int clientSocket, const Game &game) {
    char buffer[512];
    memset(buffer, 0, 512);
    int pos = 0;
    for (int i = 0; i < ROWS; ++i) {
        for (int j = 0; j < COLUMNS; ++j) {
            pos += snprintf(buffer + pos, 512 - pos, "%d ", game.board[i][j]);
        }
        pos += snprintf(buffer + pos, 512 - pos, "\n");
    }
    pos += snprintf(buffer + pos, 512 - pos, "-------------\n");
    write(clientSocket, buffer, std::strlen(buffer));
}

bool placeToken(Game &game, int column) {
    if (column < 0 || column >= COLUMNS || game.board[0][column] != EMPTY) {
        return false;
    }

    for (int i = ROWS - 1; i >= 0; --i) {
        if (game.board[i][column] == EMPTY) {
            game.board[i][column] = game.currentPlayer;
            return true;
        }
    }
    return false;
}

bool checkWinner(const Game &game, int player) {
    // Check horizontal
    for (int i = 0; i < ROWS; ++i) {
        for (int j = 0; j < COLUMNS - 3; ++j) {
            if (game.board[i][j] == player && game.board[i][j + 1] == player &&
                game.board[i][j + 2] == player && game.board[i][j + 3] == player) {
                return true;
            }
        }
    }

    // Check vertical
    for (int i = 0; i < ROWS - 3; ++i) {
        for (int j = 0; j < COLUMNS; ++j) {
            if (game.board[i][j] == player && game.board[i + 1][j] == player &&
                game.board[i + 2][j] == player && game.board[i + 3][j] == player) {
                return true;
            }
        }
    }

    // Check diagonal (bottom left to top right)
    for (int i = 3; i < ROWS; ++i) {
        for (int j = 0; j < COLUMNS - 3; ++j) {
            if (game.board[i][j] == player && game.board[i - 1][j + 1] == player &&
                game.board[i - 2][j + 2] == player && game.board[i - 3][j + 3] == player) {
                return true;
            }
        }
    }

    // Check diagonal (top left to bottom right)
    for (int i = 0; i < ROWS - 3; ++i) {
        for (int j = 0; j < COLUMNS - 3; ++j) {
            if (game.board[i][j] == player && game.board[i + 1][j + 1] == player &&
                game.board[i + 2][j + 2] == player && game.board[i + 3][j + 3] == player) {
                return true;
            }
        }
    }

    return false;
}

void *handleClient(void *arg) {
    int socket = *(int*)arg;
    delete (int*)arg;

    Game *game = nullptr;

    pthread_mutex_lock(&gamesMutex);
    for (auto g : games) {
        if (g->clientSockets[0] == 0) {
            g->clientSockets[0] = socket;
            game = g;
            break;
        } else if (g->clientSockets[1] == 0) {
            g->clientSockets[1] = socket;
            game = g;
            break;
        }
    }

    if (game == nullptr) {
        game = new Game();
        game->clientSockets[0] = socket;
        game->clientSockets[1] = 0;
        initializeGame(*game);
        games.push_back(game);
    }
    pthread_mutex_unlock(&gamesMutex);

    char buffer[256];
    int n;

    if (game->clientSockets[0] != 0 && game->clientSockets[1] != 0) {
        write(game->clientSockets[0], "Game start!\n", 12);
        write(game->clientSockets[1], "Game start!\n", 12);
    } else {
        write(socket, "Waiting for another player...\n", 30);
        while (game->clientSockets[1] == 0) {
            // Busy waiting until the second player connects.
            usleep(100000); // Sleep for 0.1 seconds to avoid busy looping.
        }
        write(game->clientSockets[0], "Second player connected, game starts!\n", 38);
        write(game->clientSockets[1], "Game start!\n", 12);
    }

    while (game->isRunning) {
        if (socket == game->clientSockets[game->currentPlayer - 1]) {
            snprintf(buffer, 255, "Player %d's turn\n", game->currentPlayer);
            write(game->clientSockets[game->currentPlayer - 1], buffer, std::strlen(buffer));
            memset(buffer, 0, 256);
            n = read(socket, buffer, 255);
            if (n < 0) {
                cerr << "ERROR reading from socket" << endl;
                break;
            }

            int column = atoi(buffer);
            if (placeToken(*game, column)) {
                printBoard(game->clientSockets[0], *game);
                printBoard(game->clientSockets[1], *game);

                if (checkWinner(*game, game->currentPlayer)) {
                    snprintf(buffer, 255, "Player %d wins!\n", game->currentPlayer);
                    write(game->clientSockets[0], buffer, std::strlen(buffer));
                    write(game->clientSockets[1], buffer, std::strlen(buffer));
                    game->isRunning = false;
                } else {
                    game->currentPlayer = (game->currentPlayer == CLIENT1) ? CLIENT2 : CLIENT1;
                    snprintf(buffer, 255, "Player %d's turn\n", game->currentPlayer);
                    write(game->clientSockets[0], buffer, std::strlen(buffer));
                    write(game->clientSockets[1], buffer, std::strlen(buffer));
                }
            } else {
                snprintf(buffer, 255, "Invalid move. Try another column.\n");
                write(socket, buffer, std::strlen(buffer));
            }
        } else {
            snprintf(buffer, 255, "Waiting for player %d to play...\n", game->currentPlayer);
            write(socket, buffer, std::strlen(buffer));
        }
    }

    close(socket);

    pthread_mutex_lock(&gamesMutex);
    auto it = std::find(games.begin(), games.end(), game);
    if (it != games.end()) {
        games.erase(it);
    }
    pthread_mutex_unlock(&gamesMutex);

    delete game;
    return nullptr;
}

void printLocalIPs() {
    struct ifaddrs *interfaces, *ifa;
    char ip[NI_MAXHOST];

    if (getifaddrs(&interfaces) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    for (ifa = interfaces; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;

        int family = ifa->ifa_addr->sa_family;

        if (family == AF_INET || family == AF_INET6) {
            int s = getnameinfo(ifa->ifa_addr,
                                (family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6),
                                ip, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
            if (s != 0) {
                printf("getnameinfo() failed: %s\n", gai_strerror(s));
                exit(EXIT_FAILURE);
            }
            printf("%s: %s\n", ifa->ifa_name, ip);
        }
    }

    freeifaddrs(interfaces);
}

int main() {
    int serverSocket, clientSocket;
    socklen_t clientLen;
    struct sockaddr_in serverAddr, clientAddr;

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        cerr << "ERROR opening socket" << endl;
        exit(1);
    }

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "ERROR on binding" << endl;
        close(serverSocket);
        exit(1);
    }

    listen(serverSocket, 5);

    printLocalIPs();

    while (true) {
        clientLen = sizeof(clientAddr);
        clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &clientLen);
        if (clientSocket < 0) {
            cerr << "ERROR on accept" << endl;
            continue;
        }

        cout << "New connection from client" << endl;

        pthread_t thread;
        int *pclient = new int;
        *pclient = clientSocket;
        pthread_create(&thread, nullptr, handleClient, pclient);
    }

    close(serverSocket);
    return 0;
}
