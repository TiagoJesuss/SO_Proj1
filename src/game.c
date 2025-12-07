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
                    board[index].has_dot = 0;
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

level_info getLevelInfo(char *level_file) {
    level_info info;
    int f = open(level_file, O_RDONLY);
    if (f < 0) {
        exit(EXIT_FAILURE);
    }
    ssize_t bytes_read;
    char buffer[1024];
    size_t fileInfoSize = 0;
    char *fileInfo = NULL;
    while ((bytes_read = read(f, buffer, sizeof(buffer)-1)) > 0) {
        buffer[bytes_read] = '\0'; // Garante que o buffer seja uma string válida

        fileInfo = realloc(fileInfo, fileInfoSize + bytes_read + 1);
        if (fileInfo == NULL) {
            close(f);
            exit(EXIT_FAILURE);
        }

        memcpy(fileInfo + fileInfoSize, buffer, bytes_read + 1);
        fileInfoSize += bytes_read;
    }
    close(f);
    char *filename = strrchr(level_file, '/');
    if (filename != NULL) {
        filename++;
    }
    char *dot = strrchr(filename, '.');
    if (dot != NULL) {
        *dot = '\0'; 
    }
    strncpy(info.name, filename, MAX_FILENAME - 1);
    char *board = NULL;
    char *saveptr_line; // Estado para strtok_r
    char *line = strtok_r(fileInfo, "\n", &saveptr_line);
    while (line != NULL) {
        debug("LINE: %s\n", line);
        if (strncmp(line, "DIM", 3) == 0) {
            sscanf(line, "DIM %d %d", &info.width, &info.height);
            info.board = malloc(sizeof(board_pos_t) * (info.width * info.height + 1));
            board = malloc(info.width * info.height + 1);
            board[0] = '\0';
        } else if (strncmp(line, "TEMPO", 5) == 0) {
            sscanf(line, "TEMPO %d", &info.tempo);
        } else if (strncmp(line, "PAC", 3) == 0) {
            sscanf(line, "PAC %s", info.pacman_file);
        } else if (strncmp(line, "MON", 3) == 0) {
            static int ghost_index = 0;
            char *saveptr_token; // Estado para strtok_r dentro da linha
            char *token = strtok_r(line + 4, " ", &saveptr_token);
            while (token != NULL) {
                debug("GHOST FILE: %s\n", token);
                if (ghost_index >= MAX_GHOSTS) { 
                    break;
                }
                strncpy(info.ghost_files[ghost_index], token, MAX_FILENAME - 1);
                info.ghost_files[ghost_index][MAX_FILENAME - 1] = '\0'; 
                ghost_index++;
                token = strtok_r(NULL, " ", &saveptr_token);
            }
            debug("LINE: %s\n", line);
            info.n_ghosts = ghost_index;
        } else if (strncmp(line, "#", 1) == 0) {
            line = strtok_r(NULL, "\n", &saveptr_line);
            continue;
        } else {
            int xyz = 0;
            debug("%d", xyz++);
            strcat(board, line);
            debug("%d", xyz++);
            line = strtok_r(NULL, "\n", &saveptr_line);
            debug("%d", xyz++);
            while (line != NULL) {
                debug("%d", xyz++);
                strcat(board, line);
                line = strtok_r(NULL, "\n", &saveptr_line);
                debug("%d", xyz++);
            }
            debug("%d", xyz++);
            process_board(info.board, board, info.height, info.width);
            debug("%d", xyz++);
            break;
            
        }
        line = strtok_r(NULL, "\n", &saveptr_line);
        debug("NEXT LINE: %s\n", line);
        
    }
    free(fileInfo);
    return info;
}

int read_dir(char *argv, level_info *level_info, char *pacman_files[], char *ghost_files[]) {
    DIR *dir = opendir(argv);
    if (dir == NULL) {
        return 1;
    }
    struct dirent *entry;
    int i = 0;
    int x = 0;
    int y = 0;
    int z = 0;
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
            case 'p':
                pacman_files[y] = malloc(strlen(path) + 1);
                strcpy(pacman_files[y], path);
                y++;
                break;
            case 'm':
                ghost_files[z] = malloc(strlen(path) + 1);
                strcpy(ghost_files[z], path);
                z++;
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
    char *pacman_files[MAX_LEVELS];
    char *ghost_files[MAX_GHOSTS];
    int n_levels = read_dir(argv[1], level_info, pacman_files, ghost_files);

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
