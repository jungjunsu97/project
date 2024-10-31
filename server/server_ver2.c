#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <mysql/mysql.h>

typedef struct RoomNode {
    char room_number[10];  // room number
    int esp_sock;          // ESP32 
    int fr_sock;           // FR 
    struct RoomNode* next; // next node pointer
} RoomNode;

//database
char* server = "localhost";
char* user = "admin";
char* password = "1234";
char* database = "SmartBuilding";

RoomNode* room_table = NULL; // room list start
pthread_mutex_t room_table_mutex; // room list mutex
pthread_mutex_t capture_mutex; // capture mutex

#define BUF_SIZE 256
#define PORT 9000

//client type
#define CLIENT_TYPE_ESP 1
#define CLIENT_TYPE_FR 2
#define CLIENT_TYPE_WEB 3

//func
RoomNode* create_room_node(const char* room_number);
RoomNode* find_room_node(const char* room_number);
void add_room_node(RoomNode* new_node);
void delete_room_node(const char* room_number);
void handle_message(const char* room_number, int client_sock, char* message, MYSQL* conn, int client_type);
void* client_handler(void* arg);
void save_image_path(MYSQL* conn, const char* image_path, const char* room_number);

int main() {
    room_table = NULL; // 방 목록 초기화
    pthread_mutex_init(&room_table_mutex, NULL);
    pthread_mutex_init(&capture_mutex, NULL);
    struct sockaddr_in server_addr;

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1) {
        perror("server socket fail");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind error");
        close(server_sock);
        return 1;
    }

    if (listen(server_sock, 5) == -1) {
        perror("listen fail");
        close(server_sock);
        return 1;
    }

    printf("start server wait client...\n");

    while (1) {
        int* client_sock = malloc(sizeof(int));
        *client_sock = accept(server_sock, NULL, NULL);
        if (*client_sock == -1) {
            perror("client accept fail");
            free(client_sock);
            continue;
        }

        pthread_t tid;
        pthread_create(&tid, NULL, client_handler, client_sock);
    }

    close(server_sock);
    pthread_mutex_destroy(&capture_mutex);
    pthread_mutex_destroy(&room_table_mutex);
    return 0;
}

// 새로운 방 노드를 생성하는 함수
RoomNode* create_room_node(const char* room_number) {
    RoomNode* new_node = (RoomNode*)malloc(sizeof(RoomNode));
    strcpy(new_node->room_number, room_number);
    new_node->esp_sock = -1;
    new_node->fr_sock = -1;
    new_node->next = NULL;
    return new_node;
}

// 방 번호로 방 노드를 찾는 함수
RoomNode* find_room_node(const char* room_number) {
    pthread_mutex_lock(&room_table_mutex); // 방 목록 뮤텍스 잠금
    RoomNode* current = room_table;
    while (current != NULL) {
        if (strcmp(current->room_number, room_number) == 0) {
            pthread_mutex_unlock(&room_table_mutex); // 잠금 해제
            return current;
        }
        current = current->next;
    }
    pthread_mutex_unlock(&room_table_mutex); // 잠금 해제
    return NULL;
}

// 방 노드를 방 목록에 추가하는 함수
void add_room_node(RoomNode* new_node) {
    pthread_mutex_lock(&room_table_mutex); // 방 목록 뮤텍스 잠금
    new_node->next = room_table;
    room_table = new_node;
    pthread_mutex_unlock(&room_table_mutex); // 잠금 해제
}

// 방 노드를 삭제하고 메모리 해제
void delete_room_node(const char* room_number) {
    pthread_mutex_lock(&room_table_mutex); // 방 목록 뮤텍스 잠금
    RoomNode* current = room_table;
    RoomNode* prev = NULL;

    while (current != NULL) {
        if (strcmp(current->room_number, room_number) == 0) {
            if (prev != NULL) {
                prev->next = current->next;
            }
            else {
                room_table = current->next; // 첫 번째 노드 삭제
            }
            free(current); // 메모리 해제
            break;
        }
        prev = current;
        current = current->next;
    }
    pthread_mutex_unlock(&room_table_mutex); // 잠금 해제
}

// 메시지를 처리하는 함수
void handle_message(const char* room_number, int client_sock, char* message, MYSQL* conn, int client_type) {
    RoomNode* room_node = find_room_node(room_number);
    if (!room_node) {
        printf("room %s no serch.\n", room_number);
        return;
    }

    if (client_type == CLIENT_TYPE_ESP) {  // ESP32 처리
        if (strstr(message, "wrong_password") != NULL) {
            printf("ESP32: room%s password fail...\n", room_number);
            int fr_sock = room_node->fr_sock;
            if (fr_sock > 0) {
                pthread_mutex_lock(&capture_mutex); // 동시 요청 방지
                char capture_request_msg[BUF_SIZE];
                snprintf(capture_request_msg, sizeof(capture_request_msg), "FR:room_%s:request_capture", room_number);
                write(fr_sock, capture_request_msg, strlen(capture_request_msg));
                pthread_mutex_unlock(&capture_mutex);
            }
            else {
                printf("room %s FR no serch.\n", room_number);
            }
        }
    }
    else if (client_type == CLIENT_TYPE_FR) {  // FR 처리
        char status[BUF_SIZE] = { 0 };
        char image_path[BUF_SIZE] = { 0 };
        sscanf(message, "FR:room_%*[^:]:%[^:]:%s", status, image_path);

        if (strcmp(status, "failure") == 0) {
            printf("FR: room%s FR fail...\n", room_number);
            // ESP32로 실패 신호 전송
            int esp_sock = room_node->esp_sock;
            if (esp_sock > 0) {
                char failure_msg[BUF_SIZE];
                snprintf(failure_msg, sizeof(failure_msg), "ROOM_%s:failure", room_number);
                write(esp_sock, failure_msg, strlen(failure_msg));
            }
            else {
                printf("room %s no search ESP32 \n", room_number);
            }
        }
        else if (strcmp(status, "success") == 0) {
            int esp_sock = room_node->esp_sock;
            if (esp_sock > 0) {
                char activate_keypad_msg[BUF_SIZE];
                snprintf(activate_keypad_msg, sizeof(activate_keypad_msg), "ROOM_%s:activate_keypad", room_number);
                write(esp_sock, activate_keypad_msg, strlen(activate_keypad_msg));
            }
            else {
                printf("room %s no search ESP32\n", room_number);
            }
        }
        else if (strcmp(status, "capture") == 0) {
            save_image_path(conn, image_path, room_number);
        }
    }
    else if (client_type == CLIENT_TYPE_WEB) {  // 웹서버 처리
        // 웹 서버에서 온 신호 처리
        if (strstr(message, "open") != NULL) {
            printf("WEB: room%s open.\n", room_number);
            int esp_sock = room_node->esp_sock;
            if (esp_sock > 0) {
                char open_msg[BUF_SIZE];
                snprintf(open_msg, sizeof(open_msg), "ROOM_%s:open", room_number);
                write(esp_sock, open_msg, strlen(open_msg));
            }
            else {
                printf("room %s no search ESP32.\n", room_number);
            }
        }
    }
}

// 클라이언트 핸들러 함수
void* client_handler(void* arg) {
    int client_sock = *(int*)arg;
    free(arg);

    MYSQL* conn = mysql_init(NULL);
    if (!mysql_real_connect(conn, server, user, password, database, 0, NULL, 0)) {
        fprintf(stderr, "DB error : %s\n", mysql_error(conn));
        close(client_sock);
        return NULL;
    }

    char buffer[BUF_SIZE];
    char room_number[10] = { 0 };
    int client_type = 0;

    // 클라이언트 종류와 방 번호 파악
    recv(client_sock, buffer, sizeof(buffer), 0);
    if (sscanf(buffer, "ESP32:room_%s", room_number) == 1) {
        client_type = CLIENT_TYPE_ESP;
        RoomNode* room_node = find_room_node(room_number);
        if (!room_node) {
            room_node = create_room_node(room_number);
            add_room_node(room_node);
        }
        else if (room_node->esp_sock != -1) {
            close(room_node->esp_sock); // 기존 연결된 소켓을 닫음
        }
        room_node->esp_sock = client_sock;
        printf("ESP32 room %sconnect.\n", room_number);
    }
    else if (sscanf(buffer, "FR:room_%s", room_number) == 1) {
        client_type = CLIENT_TYPE_FR;
        RoomNode* room_node = find_room_node(room_number);
        if (!room_node) {
            room_node = create_room_node(room_number);
            add_room_node(room_node);
        }
        else if (room_node->fr_sock != -1) {
            close(room_node->fr_sock); // 기존 연결된 소켓을 닫음
        }
        room_node->fr_sock = client_sock;
        printf("FR room %sconnect.\n", room_number);
    }
    else if (sscanf(buffer, "WEB:room_%s", room_number) == 1) {
        client_type = CLIENT_TYPE_WEB;
        RoomNode* room_node = find_room_node(room_number);
        if (!room_node) {
            room_node = create_room_node(room_number);
            add_room_node(room_node);
        } // 웹 서버는 이미 연결된 상태를 유지하므로 소켓 저장은 필요 없음
        printf("web cunnect.\n");
    }

    // 메시지 처리 루프
    while (1) {
        memset(buffer, 0, BUF_SIZE);
        int bytes_received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            printf("client disconnect\n");
            break; // 클라이언트가 연결을 끊었을 때 루프 종료
        }
        buffer[bytes_received] = '\0'; // 문자열 종료

        handle_message(room_number, client_sock, buffer, conn, client_type);
    }

    // 클라이언트 소켓 닫기 및 방 노드 삭제
    RoomNode* room_node = find_room_node(room_number);
    if (client_type == CLIENT_TYPE_ESP) {
        if (room_node) room_node->esp_sock = -1; // ESP32 소켓 초기화
    }
    else if (client_type == CLIENT_TYPE_FR) {
        if (room_node) room_node->fr_sock = -1; // FR 소켓 초기화
    }

    close(client_sock);
    delete_room_node(room_number); // 방 노드 삭제
    mysql_close(conn); // DB 연결 종료
    return NULL;
}

void save_image_path(MYSQL* conn, const char* image_path, const char* room_number) {
    char query[BUF_SIZE];
    snprintf(query, sizeof(query), "INSERT INTO Stranger (RoomNO, Img_path) VALUES ('%s', '%s')", room_number, image_path);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "Failed to insert image path into DB: %s\n", mysql_error(conn));
    }
    else {
        printf("Image path saved to DB for room %s: %s\n", room_number, image_path);
    }
}
