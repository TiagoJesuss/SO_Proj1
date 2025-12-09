#include "board.h"
#include "display.h"
#include "threads.h"
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h> //adicionado
#include <sys/wait.h>   //adicionado
#include <pthread.h>

#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define LOAD_BACKUP 3
#define CREATE_BACKUP 4

pthread_mutex_t game_state_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t ncurses_mutex = PTHREAD_MUTEX_INITIALIZER;

void screen_refresh(board_t * game_board, int mode) {
    debug("REFRESH\n");
    draw_board(game_board, mode);
    refresh_screen();
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
}

void *ghost_thread(void *arg) {
    ghost_thread_args_t *args = (ghost_thread_args_t *)arg;
    board_t *game_board = args->game_board;
    int ghost_index = args->ghost_index;
    bool *leave_thread = args->leave_thread;

    ghost_t* ghost = &game_board->ghosts[ghost_index];
    debug("STARTING GHOST THREAD %d, leave thread %d\n", ghost_index, *leave_thread);
    while (*leave_thread == 0) {
        debug("GHOST THREAD %d MOVE %d/%d\n", ghost_index, ghost->current_move, ghost->n_moves);
        pthread_mutex_lock(&game_state_mutex);
        move_ghost(game_board, ghost_index, &ghost->moves[ghost->current_move % ghost->n_moves]);
        pthread_mutex_unlock(&game_state_mutex);

        pthread_mutex_lock(&game_state_mutex);
        screen_refresh(game_board, DRAW_MENU);
        pthread_mutex_unlock(&game_state_mutex);

        sleep_ms(game_board->tempo);
    }
    return NULL; 
}

void *pacman_thread(void *arg) {
    pacman_thread_args_t *pacman_thread_args = (pacman_thread_args_t *)arg;
    board_t *game_board = pacman_thread_args->game_board;
    pacman_t *pacman = &game_board->pacmans[0];
    int *result = pacman_thread_args->result;
    bool *leave_thread = pacman_thread_args->leave_thread;

    while (pacman->alive) {
        command_t *play;

        if (pacman->n_moves == 0) { // Se for entrada do usuário
            command_t c;
            c.command = get_input();

            if (c.command == '\0') {
                continue; // Sem entrada, continua
            }

            c.turns = 1;
            play = &c;
        } else { // Movimentos predefinidos
            play = &pacman->moves[pacman->current_move % pacman->n_moves];
        }

        debug("KEY %c\n", play->command);

        if (play->command == 'Q') {
            pthread_mutex_lock(&game_state_mutex);
            *result = QUIT_GAME;
            *leave_thread = true;
            pthread_mutex_unlock(&game_state_mutex);
            break;
        }
        if (play->command == 'G'){
            pthread_mutex_lock(&game_state_mutex);
            *result = CREATE_BACKUP;
            *leave_thread = true;
            pthread_mutex_unlock(&game_state_mutex);
            break;
        }

        pthread_mutex_lock(&game_state_mutex);
        int move = move_pacman(game_board, 0, play);
        pthread_mutex_unlock(&game_state_mutex);

        if (move == REACHED_PORTAL) {
            pthread_mutex_lock(&ncurses_mutex);
            screen_refresh(game_board, DRAW_WIN);
            *result = NEXT_LEVEL;
            *leave_thread = true;
            pthread_mutex_unlock(&ncurses_mutex);
            break; // Pacman venceu
        }

        if (move == DEAD_PACMAN) {
            pthread_mutex_lock(&ncurses_mutex);
            screen_refresh(game_board, DRAW_GAME_OVER);
            *result = LOAD_BACKUP;
            *leave_thread = true;
            pthread_mutex_unlock(&ncurses_mutex);
            break; // Pacman morreu
        }

        pthread_mutex_lock(&ncurses_mutex);
        screen_refresh(game_board, DRAW_MENU);
        pthread_mutex_unlock(&ncurses_mutex);

        sleep_ms(game_board->tempo); // Aguarda o tempo definido
    }

    return NULL;
}

int play_board(board_t * game_board) {
    pacman_t* pacman = &game_board->pacmans[0];
    command_t* play;
    if (pacman->n_moves == 0) { // if is user input
        command_t c; 
        c.command = get_input();

        if(c.command == '\0')
            return CONTINUE_PLAY;

        c.turns = 1;
        play = &c;
    }
    else { // else if the moves are pre-defined in the file
        // avoid buffer overflow wrapping around with modulo of n_moves
        // this ensures that we always access a valid move for the pacman
        play = &pacman->moves[pacman->current_move%pacman->n_moves];
    }

    debug("KEY %c\n", play->command);

    if (play->command == 'Q') {
        return QUIT_GAME;
    }
    if (play->command == 'G'){
        return CREATE_BACKUP;
    }

    int result = move_pacman(game_board, 0, play);
    if (result == REACHED_PORTAL) {
        // Next level
        return NEXT_LEVEL;
    }

    if(result == DEAD_PACMAN) {
        return LOAD_BACKUP;
    }

    //pthread_t pacman_tid;
    //pthread_create(&pacman_tid, NULL, pacman_thread, game_board);
    //pthread_t ghost_threads[game_board->n_ghosts];
    for (int i = 0; i < game_board->n_ghosts; i++) {
        ghost_t* ghost = &game_board->ghosts[i];
        // avoid buffer overflow wrapping around with modulo of n_moves
        // this ensures that we always access a valid move for the ghost
        move_ghost(game_board, i, &ghost->moves[ghost->current_move%ghost->n_moves]);
        //pthread_create(&ghost_threads[i], NULL, ghost_thread, &(ghost_thread_args_t){game_board, i});
    }
    /*
    pthread_join(pacman_tid, NULL);
    for (int i = 0; i < game_board->n_ghosts; i++) {
        pthread_join(ghost_threads[i], NULL);
    }
    */

    if (!game_board->pacmans[0].alive) {
        return LOAD_BACKUP;
    }      

    return CONTINUE_PLAY;  
}

void process_board(board_pos_t *board, char *board_str, int height, int width) {
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            int index = i * width + j;
            char ch = board_str[index];
            switch (ch) {
                case 'X': // Wall
                    board[index].content = 'W';
                    board[index].has_dot = 0;
                    board[index].has_portal = 0;
                    break;
                case 'o': // Free space
                    board[index].content = ' ';
                    board[index].has_dot = 1;
                    board[index].has_portal = 0;
                    break;
                case '@': // Portal
                    board[index].content = ' ';
                    board[index].has_dot = 0;
                    board[index].has_portal = 1;
                    break;
            }

        }
    }
}

char* readFile (char *file) {
    int f = open(file, O_RDONLY);
    if (f < 0) {
        exit(EXIT_FAILURE);
    }
    ssize_t bytes_read;
    char buffer[1024];
    size_t fileSize = 0;
    char *fileContent = NULL;
    while ((bytes_read = read(f, buffer, sizeof(buffer)-1)) > 0) {
        buffer[bytes_read] = '\0'; // Garante que o buffer seja uma string válida

        fileContent = realloc(fileContent, fileSize + bytes_read + 1);
        if (fileContent == NULL) {
            close(f);
            exit(EXIT_FAILURE);
        }

        memcpy(fileContent + fileSize, buffer, bytes_read + 1);
        fileSize += bytes_read;
    }
    close(f);
    return fileContent;
}

void build_command(command_t *command, char *line) {
    sscanf(line, "%c", &command->command);
    if (command->command == 'T') {
        sscanf(line, "T %d", &command->turns);
    } else {
        command->turns = 1;
    }
}

char* getFileName(char *file) {
    char *filename = strrchr(file, '/');
    if (filename != NULL) {
        return filename + 1;
    }
    return file;
}

pac_ghost_info getPacGhostInfo(char *file) {
    pac_ghost_info info;
    char *fileInfo = readFile(file);
    strncpy(info.file_name, getFileName(file), MAX_FILENAME - 1);
    char *saveptr_line; // Estado para strtok_r
    char *line = strtok_r(fileInfo, "\n", &saveptr_line);
    while (line != NULL) {
        if (strncmp(line, "#", 1) == 0) {
            line = strtok_r(NULL, "\n", &saveptr_line);
            continue;
        } else if (strncmp(line, "PASSO", 5) == 0) {
            sscanf(line, "PASSO %d", &info.passo);
        } else if (strncmp(line, "POS", 3) == 0) {
            sscanf(line, "POS %d %d", &info.pos_x, &info.pos_y);
        } else {
            int n_moves = 0;
            while (line != NULL) {
                build_command(&info.moves[n_moves], line);
                n_moves++;
                line = strtok_r(NULL, "\n", &saveptr_line);
            }
            break;
        }
        line = strtok_r(NULL, "\n", &saveptr_line);
    }
    free(fileInfo);
    return info;
}

char* getPath(char *base_path, char *file_name) {
    char *last_slash = strrchr(base_path, '/');
    char *path;
    if (last_slash != NULL) {
        size_t dir_length = last_slash - base_path + 1; // +1 para incluir a barra
        path = malloc(dir_length + strlen(file_name) + 1); // +1 para o terminador nulo
        strncpy(path, base_path, dir_length);
        path[dir_length] = '\0'; // Adiciona o terminador nulo
        strcat(path, file_name);
    } else {
        path = malloc(strlen(file_name) + 1);
        strcpy(path, file_name);
    }
    return path;
}

level_info getLevelInfo(char *level_file) {
    level_info info;
    info.has_pacman = 0;
    char* fileInfo = readFile(level_file);
    strncpy(info.file_name, getFileName(level_file), MAX_FILENAME - 1);
    char *board = NULL;
    char *saveptr_line; // Estado para strtok_r
    char *line = strtok_r(fileInfo, "\n", &saveptr_line);
    while (line != NULL) {
        if (strncmp(line, "DIM", 3) == 0) {
            sscanf(line, "DIM %d %d", &info.width, &info.height);
            info.board = malloc(sizeof(board_pos_t) * (info.width * info.height + 1));
            board = malloc(info.width * info.height + 1);
            board[0] = '\0';
        } else if (strncmp(line, "TEMPO", 5) == 0) {
            sscanf(line, "TEMPO %d", &info.tempo);
        } else if (strncmp(line, "PAC", 3) == 0) {
            sscanf(line, "PAC %s", info.pacman_file);
            char *path = getPath(level_file, info.pacman_file);
            info.pacman_info = getPacGhostInfo(path);
            free(path);
            info.has_pacman = 1;
        } else if (strncmp(line, "MON", 3) == 0) {
            int ghost_index = 0;
            char *saveptr_token; // Estado para strtok_r dentro da linha
            char *token = strtok_r(line + 4, " ", &saveptr_token);
            while (token != NULL) {
                if (ghost_index >= MAX_GHOSTS) { 
                    break;
                }
                strncpy(info.ghost_files[ghost_index], token, MAX_FILENAME - 1);
                info.ghost_files[ghost_index][MAX_FILENAME - 1] = '\0'; 
                char *path = getPath(level_file, info.ghost_files[ghost_index]);
                info.ghosts_info[ghost_index] = getPacGhostInfo(path);
                free(path);
                ghost_index++;
                token = strtok_r(NULL, " ", &saveptr_token);
            }
            info.n_ghosts = ghost_index;
        } else if (strncmp(line, "#", 1) == 0) {
            line = strtok_r(NULL, "\n", &saveptr_line);
            continue;
        } else {
            strcat(board, line);
            line = strtok_r(NULL, "\n", &saveptr_line);
            while (line != NULL) {
                strcat(board, line);
                line = strtok_r(NULL, "\n", &saveptr_line);
            }
            process_board(info.board, board, info.height, info.width);
            free(board);
            break;
            
        }
        line = strtok_r(NULL, "\n", &saveptr_line);
        
    }
    free(fileInfo);
    return info;
}

void *read_file_thread(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;

    *(args->level_info) = getLevelInfo(args->path);

    free(args);
    return NULL;
}

int read_dir(char *argv, level_info *level_info) {
    DIR *dir = opendir(argv);
    if (dir == NULL) {
        return 1;
    }
    struct dirent *entry;
    int i = 0;
    int x = 0;

    pthread_t threads[MAX_LEVELS];
    int thread_count = 0;

    while ((entry = readdir(dir)) != NULL) { // Lê cada ficheiro na diretoria
        if (i++ < 2) continue;
        const char *dot = strrchr(entry->d_name, '.');
        char extension[4] = "";
        if (dot != NULL && *(dot + 1) != '\0') {
            strncpy(extension, dot + 1, sizeof(extension) - 1); 
            extension[sizeof(extension) - 1] = '\0';
        }

        if (extension[0] == 'l') {
            //char *path = malloc(strlen(argv) + strlen(entry->d_name) + 2);
            //sprintf(path, "%s/%s", argv, entry->d_name);

            thread_args_t *args = malloc(sizeof(thread_args_t));
            sprintf(args->path, "%s/%s", argv, entry->d_name);
            args->level_info = &level_info[x];
            
            if (pthread_create(&threads[thread_count], NULL, read_file_thread, args) != 0) {
                perror("pthread_create");
                free(args);
                continue;
            }
            
            //level_info[x] = getLevelInfo(path);
            thread_count++;
            x++;
            //free(path);
        }
        
    }
    
    for (int j = 0; j < thread_count; j++) {
        pthread_join(threads[j], NULL);
    }
    
    closedir(dir);
    return x;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("Usage: %s <level_directory>\n", argv[0]);
        // TODO receive inputs
    }
    open_debug_file("debug.log");
    level_info level_info[MAX_LEVELS];
    int n_levels = read_dir(argv[1], level_info);

    // Random seed for any random movements
    srand((unsigned int)time(NULL));

    

    terminal_init();
    
    int accumulated_points = 0;
    bool end_game = false;
    board_t game_board;
    int lvl = 0;
    bool hasBackup = false;
    int result;
    bool leave_thread = false;
    /*
    Ter a logica do while true dentro do thread do pacman e ter threads para os fantasmas
    ter logica do play_board dentro do thread do pacman e ghosts
    */

    pacman_thread_args_t pacman_args;
    pacman_args.result = &result;
    pacman_args.leave_thread = &leave_thread;
    pthread_t pacman_tid;

    ghost_thread_args_t ghost_args[MAX_GHOSTS];
    pthread_t ghost_tids[MAX_GHOSTS];
    while (!end_game) {
        load_level(&game_board, accumulated_points, &level_info[lvl]);
        draw_board(&game_board, DRAW_MENU);
        refresh_screen();
        for (int i = 0; i < game_board.n_ghosts; i++) {
            ghost_args[i].game_board = &game_board;
            ghost_args[i].ghost_index = i;
            ghost_args[i].leave_thread = &leave_thread;
        }
        pacman_args.game_board = &game_board;
        while(true) {
            //int result = play_board(&game_board);
            if (pthread_create(&pacman_tid, NULL, pacman_thread, &pacman_args) != 0) {
                perror("pthread_create");
                exit(EXIT_FAILURE);
            }
            for (int i = 0; i < game_board.n_ghosts; i++) {
                if (pthread_create(&ghost_tids[i], NULL, ghost_thread, &ghost_args[i]) != 0) {
                    perror("pthread_create");
                    exit(EXIT_FAILURE);
                }
            }
            pthread_join(pacman_tid, NULL);
            debug("RESULT: %d\n", result);
            for (int i = 0; i < game_board.n_ghosts; i++) {
                pthread_join(ghost_tids[i], NULL);
            }
            if(result == NEXT_LEVEL) {
                lvl++;
                if (lvl >= n_levels) {
                    screen_refresh(&game_board, DRAW_WIN);
                    if (hasBackup)
                        _exit(1);
                    end_game = true;
                } else {
                    screen_refresh(&game_board, DRAW_MENU);
                }
                sleep_ms(game_board.tempo);
                break;
            }
            if(result == QUIT_GAME && hasBackup) {
                _exit(1);
            }
            if(result == QUIT_GAME) {
                screen_refresh(&game_board, DRAW_GAME_OVER); 
                sleep_ms(game_board.tempo);
                end_game = true;
                break;
            }
            if (result == LOAD_BACKUP && !hasBackup){
                screen_refresh(&game_board, DRAW_GAME_OVER); 
                sleep_ms(game_board.tempo);
                end_game = true;
                break;
            } else if (result == LOAD_BACKUP){
                _exit(0);
            }

            if (result == CREATE_BACKUP && !hasBackup){
                pid_t pid, w;
                int status;
                hasBackup = true;
                pid = fork(); //nao sei se é correto nao ter caos para o filho mas visto q executa o mm codigo
                if (pid!= 0 && pid != -1){ // caso do pai
                    w = waitpid(pid, &status, 0); // 0 representa esperar por todas as childs, pode ser mudado visto q so ha uma
                    if (w == -1) {
                        perror("waitpid");
                        exit(EXIT_FAILURE);
                    }
                    hasBackup = false;
                    if (WIFEXITED(status)){ //se child terminar de forma correta(return true)
                        if (WEXITSTATUS(status)){ //neste caso exit = 1 entra ca dentro
                            screen_refresh(&game_board, DRAW_GAME_OVER); 
                            sleep_ms(game_board.tempo);
                            end_game = true;
                            break;
                        }
                    }
                }
            }
    
            screen_refresh(&game_board, DRAW_MENU); 

            accumulated_points = game_board.pacmans[0].points;      
        }
        print_board(&game_board);
        unload_level(&game_board);
    }
    
    terminal_cleanup();

    close_debug_file();

    return 0;
}
