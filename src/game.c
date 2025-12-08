#include "board.h"
#include "display.h"
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>

#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define LOAD_BACKUP 3
#define CREATE_BACKUP 4

void screen_refresh(board_t * game_board, int mode) {
    debug("REFRESH\n");
    draw_board(game_board, mode);
    refresh_screen();
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
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

    int result = move_pacman(game_board, 0, play);
    if (result == REACHED_PORTAL) {
        // Next level
        return NEXT_LEVEL;
    }

    if(result == DEAD_PACMAN) {
        return QUIT_GAME;
    }
    for (int i = 0; i < game_board->n_ghosts; i++) {
        ghost_t* ghost = &game_board->ghosts[i];
        // avoid buffer overflow wrapping around with modulo of n_moves
        // this ensures that we always access a valid move for the ghost
        move_ghost(game_board, i, &ghost->moves[ghost->current_move%ghost->n_moves]);
    }

    if (!game_board->pacmans[0].alive) {
        return QUIT_GAME;
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
    debug("Opening file: %s\n", file);
    if (f < 0) {
        exit(EXIT_FAILURE);
    }
    debug("File %s opened successfully\n", file);
    ssize_t bytes_read;
    char buffer[1024];
    size_t fileSize = 0;
    char *fileContent = NULL;
    while ((bytes_read = read(f, buffer, sizeof(buffer)-1)) > 0) {
        buffer[bytes_read] = '\0'; // Garante que o buffer seja uma string válida

        fileContent = realloc(fileContent, fileSize + bytes_read + 1);
        debug("Reallocating fileContent to size: %zu\n", fileSize + bytes_read + 1);
        if (fileContent == NULL) {
            close(f);
            exit(EXIT_FAILURE);
        }
        debug("Reading %zd bytes from file\n", bytes_read);

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
    debug("Reading PAC/GHOST info from file: %s\n", file);
    char *fileInfo = readFile(file);
    debug("PAC/GHOST FILE: %s\n", file);
    strncpy(info.file_name, getFileName(file), MAX_FILENAME - 1);
    debug("PAC/GHOST FILE NAME: %s\n", info.file_name);
    char *saveptr_line; // Estado para strtok_r
    char *line = strtok_r(fileInfo, "\n", &saveptr_line);
    while (line != NULL) {
        if (strncmp(line, "#", 1) == 0) {
            line = strtok_r(NULL, "\n", &saveptr_line);
            continue;
        } else if (strncmp(line, "PASSO", 5) == 0) {
            sscanf(line, "PASSO %d", &info.passo);
            debug("PASSO: %d\n", info.passo);
        } else if (strncmp(line, "POS", 3) == 0) {
            sscanf(line, "POS %d %d", &info.pos_x, &info.pos_y);
            debug("POS: %d %d\n", info.pos_x, info.pos_y);
        } else {
            int n_moves = 0;
            while (line != NULL) {
                debug("MOVE LINE: %s\n", line);
                build_command(&info.moves[n_moves], line);
                debug("MOVE[%d]: %c %d\n", n_moves, info.moves[n_moves].command, info.moves[n_moves].turns);
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
            info.pacman_info = getPacGhostInfo(getPath(level_file, info.pacman_file));
            debug("PACMAN POS: %d %d\n", info.pacman_info.pos_x, info.pacman_info.pos_y);
            info.has_pacman = 1;
        } else if (strncmp(line, "MON", 3) == 0) {
            int ghost_index = 0;
            debug("GHOST FILES LINE: %s\n", line);
            char *saveptr_token; // Estado para strtok_r dentro da linha
            char *token = strtok_r(line + 4, " ", &saveptr_token);
            while (token != NULL) {
                if (ghost_index >= MAX_GHOSTS) { 
                    break;
                }
                strncpy(info.ghost_files[ghost_index], token, MAX_FILENAME - 1);
                info.ghost_files[ghost_index][MAX_FILENAME - 1] = '\0'; 
                debug("GHOST FILE[%d]: %s\n", ghost_index, info.ghost_files[ghost_index]);
                info.ghosts_info[ghost_index] = getPacGhostInfo(getPath(level_file, info.ghost_files[ghost_index]));
                debug("GHOST POS[%d]: %d %d\n", ghost_index, info.ghosts_info[ghost_index].pos_x, info.ghosts_info[ghost_index].pos_y);
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
            break;
            
        }
        line = strtok_r(NULL, "\n", &saveptr_line);
        
    }
    free(fileInfo);
    return info;
}

int read_dir(char *argv, level_info *level_info) {
    DIR *dir = opendir(argv);
    if (dir == NULL) {
        return 1;
    }
    struct dirent *entry;
    int i = 0;
    int x = 0;
    while ((entry = readdir(dir)) != NULL) { // Lê cada ficheiro na diretoria
        if (i++ < 2) continue;
        char* path = malloc(strlen(argv) + strlen(entry->d_name) + 2);
        sprintf(path, "%s/%s", argv, entry->d_name);
        const char *dot = strrchr(entry->d_name, '.');
        char extension[4] = "";
        if (dot != NULL && *(dot + 1) != '\0') {
            strncpy(extension, dot + 1, sizeof(extension) - 1); 
            extension[sizeof(extension) - 1] = '\0';
        }
        switch (extension[0]) {
            case 'l':
                level_info[x] = getLevelInfo(path);
                x++;
                break;
            default:
                break;
        }
        free(path);
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
    //pac_ghost_info pacman_info[MAX_LEVELS];
    //pac_ghost_info ghosts_info[MAX_GHOSTS];
    int n_levels = read_dir(argv[1], level_info);

    // Random seed for any random movements
    srand((unsigned int)time(NULL));

    

    terminal_init();
    
    int accumulated_points = 0;
    bool end_game = false;
    board_t game_board;
    int lvl = 0;
    while (!end_game) {
        load_level(&game_board, accumulated_points, &level_info[lvl]);
        draw_board(&game_board, DRAW_MENU);
        refresh_screen();
        while(true) {
            int result = play_board(&game_board); 
            if(result == NEXT_LEVEL) {
                screen_refresh(&game_board, DRAW_WIN);
                sleep_ms(game_board.tempo);
                lvl++;
                if (lvl >= n_levels) {
                    end_game = true;
                }
                break;
            }

            if(result == QUIT_GAME) {
                screen_refresh(&game_board, DRAW_GAME_OVER); 
                sleep_ms(game_board.tempo);
                end_game = true;
                break;
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
