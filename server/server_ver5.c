#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <mysql/mysql.h>

typedef struct RoomNode {
    char room_number[10];  // πÊ π¯»£
    int esp_sock;          // ESP32 º“ƒœ
    int fr_sock;           // FR º“ƒœ
    struct RoomNode* next; // ¥Ÿ¿Ω ≥ÎµÂ ∆˜¿Œ≈Õ
} RoomNode;

//µ•¿Ã≈Õ∫£¿ÃΩ∫ ø¨∞·
char* server = "localhost";
char* user = "admin";
char* password = "1234";
char* database = "SmartBuilding";

RoomNode* room_table = NULL; // πÊ ∏Ò∑œ¿« Ω√¿€ ∆˜¿Œ≈Õ
pthread_mutex_t room_table_mutex; // πÊ ∏Ò∑œø° ¥Î«— π¬≈ÿΩ∫
pthread_mutex_t capture_mutex; // ƒ∏√≥ ø‰√ªø° ¥Î«— π¬≈ÿΩ∫

#define BUF_SIZE 256
#define PORT 9000

// ≈¨∂Û¿Ãæ∆Æ ≈∏¿‘ ¡§¿«
#define CLIENT_TYPE_ESP 1
#define CLIENT_TYPE_FR 2
#define CLIENT_TYPE_WEB 3

//«‘ºˆ º±æ∫Œ
RoomNode* create_room_node(const char* room_number);
RoomNode* find_room_node(const char* room_number);
void add_room_node(RoomNode* new_node);
void delete_room_node(const char* room_number);
void handle_message(const char* room_number, int client_sock, char* message, MYSQL* conn, int client_type);
void handle_web_message(int client_sock, char* message, MYSQL* conn);
void* client_handler(void* arg);
void save_image_path(MYSQL* conn, const char* image_path, const char* room_number);
void change_password(MYSQL* conn, const char* pw, const char* room_number);

int main() {
    room_table = NULL; // πÊ ∏Ò∑œ √ ±‚»≠
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
        perror("bind fail");
        close(server_sock);
        return 1;
    }

    if (listen(server_sock, 5) == -1) {
        perror("listen fail");
        close(server_sock);
        return 1;
    }

    printf("server start. client wait...\n");

    while (1) {
        int* client_sock = malloc(sizeof(int));
        *client_sock = accept(server_sock, NULL, NULL);
        if (*client_sock < 0) {
            perror("accept fail");
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

// ªı∑ŒøÓ πÊ ≥ÎµÂ∏¶ ª˝º∫«œ¥¬ «‘ºˆ
RoomNode* create_room_node(const char* room_number) {
    RoomNode* new_node = (RoomNode*)malloc(sizeof(RoomNode));
    strcpy(new_node->room_number, room_number);
    new_node->esp_sock = -1;
    new_node->fr_sock = -1;
    new_node->next = NULL;
    return new_node;
}

// πÊ π¯»£∑Œ πÊ ≥ÎµÂ∏¶ √£¥¬ «‘ºˆ
RoomNode* find_room_node(const char* room_number) {
    pthread_mutex_lock(&room_table_mutex); // πÊ ∏Ò∑œ π¬≈ÿΩ∫ ¿·±›
    RoomNode* current = room_table;
    while (current != NULL) {
        if (strcmp(current->room_number, room_number) == 0) {
            pthread_mutex_unlock(&room_table_mutex); // ¿·±› «ÿ¡¶
            return current;
        }
        current = current->next;
    }
    pthread_mutex_unlock(&room_table_mutex); // ¿·±› «ÿ¡¶
    return NULL;
}

// πÊ ≥ÎµÂ∏¶ πÊ ∏Ò∑œø° √ﬂ∞°«œ¥¬ «‘ºˆ
void add_room_node(RoomNode* new_node) {
    pthread_mutex_lock(&room_table_mutex); // πÊ ∏Ò∑œ π¬≈ÿΩ∫ ¿·±›
    new_node->next = room_table;
    room_table = new_node;
    pthread_mutex_unlock(&room_table_mutex); // ¿·±› «ÿ¡¶
}

// πÊ ≥ÎµÂ∏¶ ªË¡¶«œ∞Ì ∏ﬁ∏∏Æ «ÿ¡¶
void delete_room_node(const char* room_number) {
    pthread_mutex_lock(&room_table_mutex); // πÊ ∏Ò∑œ π¬≈ÿΩ∫ ¿·±›
    RoomNode* current = room_table;
    RoomNode* prev = NULL;

    while (current != NULL) {
        if (strcmp(current->room_number, room_number) == 0) {
            if (prev != NULL) {
                prev->next = current->next;
            }
            else {
                room_table = current->next; // √π π¯¬∞ ≥ÎµÂ ªË¡¶
            }
            free(current); // ∏ﬁ∏∏Æ «ÿ¡¶
            break;
        }
        prev = current;
        current = current->next;
    }
    pthread_mutex_unlock(&room_table_mutex); // ¿·±› «ÿ¡¶
}

void handle_web_message(int client_sock, char* message, MYSQL* conn) {
    // ¿•º≠πˆ √≥∏Æ
    char room_number[BUF_SIZE] = { 0 };
    char status[BUF_SIZE] = { 0 };
    char pw[BUF_SIZE] = { 0 };
    sscanf(message, "WEB:room_%[^:]:%[^:]:%[^:]", room_number, status, pw);
    RoomNode* room_node = find_room_node(room_number);

    if (!room_node) {
        printf("Not found room %s.\n", room_number);
        return;
    }
    // ¿• º≠πˆø°º≠ ø¬ Ω≈»£ √≥∏Æ
    if (strcmp(status, "open") == 0) {
        printf("WEB : room %s opened sign.\n", room_number);
        int esp_sock = room_node->esp_sock;
        if (esp_sock > 0) {
            char open_msg[BUF_SIZE];
            snprintf(open_msg, sizeof(open_msg), "open\n");
            write(esp_sock, open_msg, strlen(open_msg));
        }
        else {
            printf("Not found room %s.\n", room_number);
        }
    }
    else if (strcmp(status, "change_PW") == 0) {
        change_password(conn, pw, room_number);
    }
}

// ∏ﬁΩ√¡ˆ∏¶ √≥∏Æ«œ¥¬ «‘ºˆ
void handle_message(const char* room_number, int client_sock, char* message, MYSQL* conn, int client_type) {
    RoomNode* room_node = find_room_node(room_number);
    if (!room_node) {
        printf("Not found room %s.\n", room_number);
        return;
    }

    if (client_type == CLIENT_TYPE_ESP) {  // ESP32 √≥∏Æ
        char status[BUF_SIZE] = { 0 };
        sscanf(message, "ESP32:room_%*[^:]:%[^:]", status);

        if (strcmp(status, "wrong_password") == 0) {
            printf("ESP32: room %s fail password. FR capture request...\n", room_number);
            int fr_sock = room_node->fr_sock;
            if (fr_sock > 0) {
                pthread_mutex_lock(&capture_mutex); // µøΩ√ ø‰√ª πÊ¡ˆ
                char capture_request_msg[BUF_SIZE];
                snprintf(capture_request_msg, sizeof(capture_request_msg), "FR:room_%s:request_capture", room_number);
                write(fr_sock, capture_request_msg, strlen(capture_request_msg));
                pthread_mutex_unlock(&capture_mutex);
            }
            else {
                printf("Not found room %s.\n", room_number);
            }
        }
    }
    else if (client_type == CLIENT_TYPE_FR) {  // FR √≥∏Æ
        char status[BUF_SIZE] = { 0 };
        char image_path[BUF_SIZE] = { 0 };
        sscanf(message, "FR:room_%*[^:]:%[^:]:%s", status, image_path);

        if (strcmp(status, "failure") == 0) {
            printf("FR: room %s fail face recognition. to ESP32 send signal...\n", room_number);
            // ESP32∑Œ Ω«∆– Ω≈»£ ¿¸º€
            int esp_sock = room_node->esp_sock;
            if (esp_sock > 0) {
                char failure_msg[BUF_SIZE];
                snprintf(failure_msg, sizeof(failure_msg), "failure\n");
                write(esp_sock, failure_msg, strlen(failure_msg));
                save_image_path(conn, image_path, room_number);
            }
            else {
                printf("Not found room %s.\n", room_number);
            }
        }
        else if (strcmp(status, "success") == 0) {
            int esp_sock = room_node->esp_sock;
            if (esp_sock > 0) {
                char activate_keypad_msg[BUF_SIZE];
                snprintf(activate_keypad_msg, sizeof(activate_keypad_msg), "activate_keypad\n");
                write(esp_sock, activate_keypad_msg, strlen(activate_keypad_msg));
            }
            else {
                printf("Not found room %s.\n", room_number);
            }
        }
        else if (strcmp(status, "capture") == 0) {
            save_image_path(conn, image_path, room_number);
        }
    }

}

// ≈¨∂Û¿Ãæ∆Æ «⁄µÈ∑Ø «‘ºˆ
void* client_handler(void* arg) {
    int client_sock = *(int*)arg;
    free(arg);

    MYSQL* conn = mysql_init(NULL);
    if (!mysql_real_connect(conn, server, user, password, database, 0, NULL, 0)) {
        fprintf(stderr, "DB connect error : %s\n", mysql_error(conn));
        close(client_sock);
        return NULL;
    }

    char buffer[BUF_SIZE];
    char room_number[10] = { 0 };
    int client_type = 0;

    // ≈¨∂Û¿Ãæ∆Æ ¡æ∑˘øÕ πÊ π¯»£ ∆ƒæ«
    recv(client_sock, buffer, sizeof(buffer), 0);
    if (sscanf(buffer, "ESP32:room_%s", room_number) == 1) {
        client_type = CLIENT_TYPE_ESP;
        RoomNode* room_node = find_room_node(room_number);
        if (!room_node) {
            room_node = create_room_node(room_number);
            add_room_node(room_node);
        }
        else if (room_node->esp_sock != -1) {
            close(room_node->esp_sock); // ±‚¡∏ ø¨∞·µ» º“ƒœ¿ª ¥›¿Ω
        }
        room_node->esp_sock = client_sock;
        printf("ESP32 room %s connect.\n", room_number);
    }
    else if (sscanf(buffer, "FR:room_%s", room_number) == 1) {
        client_type = CLIENT_TYPE_FR;
        RoomNode* room_node = find_room_node(room_number);
        if (!room_node) {
            room_node = create_room_node(room_number);
            add_room_node(room_node);
        }
        else if (room_node->fr_sock != -1) {
            close(room_node->fr_sock); // ±‚¡∏ ø¨∞·µ» º“ƒœ¿ª ¥›¿Ω
        }
        room_node->fr_sock = client_sock;
        printf("FR room %s connect.\n", room_number);
    }
    else if (strncmp(buffer,"WEB",3) == 0) {
        client_type = CLIENT_TYPE_WEB;
        printf("WEB connect.\n");
    }

    // ∏ﬁΩ√¡ˆ √≥∏Æ ∑Á«¡
    while (1) {
        memset(buffer, 0, BUF_SIZE);
        int bytes_received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            printf("client close.\n");
            break; // ≈¨∂Û¿Ãæ∆Æ∞° ø¨∞·¿ª ≤˜æ˙¿ª ∂ß ∑Á«¡ ¡æ∑·
        }
        buffer[bytes_received] = '\0'; // πÆ¿⁄ø≠ ¡æ∑·
        if (client_type == CLIENT_TYPE_WEB) {
            handle_web_message(client_sock, buffer, conn);
        }
        else {
            handle_message(room_number, client_sock, buffer, conn, client_type);
        }

    }

    // ≈¨∂Û¿Ãæ∆Æ º“ƒœ ¥›±‚ π◊ πÊ ≥ÎµÂ ªË¡¶
    RoomNode* room_node = find_room_node(room_number);
    if (client_type == CLIENT_TYPE_ESP) {
        if (room_node) room_node->esp_sock = -1; // ESP32 º“ƒœ √ ±‚»
	printf("ESP close\n");
    }
    else if (client_type == CLIENT_TYPE_FR) {
        if (room_node) room_node->fr_sock = -1; // FR º“ƒœ √ ±‚»≠
	printf("FR close\n");
    }
	else if (client_type == CLIENT_TYPE_WEB){
		close(client_sock);
		return NULL;
	}
    close(client_sock);
    if ((room_node->fr_sock == -1) && (room_node->esp_sock == -1))
        delete_room_node(room_number); // πÊ ≥ÎµÂ ªË¡¶
    mysql_close(conn); // DB ø¨∞· ¡æ∑·
    return NULL;
}

void save_image_path(MYSQL* conn, const char* image_path, const char* room_number) {
    char query[BUF_SIZE] = { 0 };
    snprintf(query, sizeof(query), "INSERT INTO Stranger (RoomNO, Img_path) VALUES ('%s', '%s')", room_number, image_path);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "Failed to insert image path into DB: %s\n", mysql_error(conn));
    }
    else {
        printf("Image path saved to DB for room %s: %s\n", room_number, image_path);
    }
}

void change_password(MYSQL* conn, const char* pw, const char* room_number) {
    char query[BUF_SIZE] = { 0 };
    snprintf(query, sizeof(query), "UPDATE Owner SET LoginPW ='%s' WHERE RoomNO = %s", pw, room_number);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "Failed to update Password DB: %s\n", mysql_error(conn));
    }
    else {
        printf("Change Password DB for room %s\n", room_number);
    }
}
