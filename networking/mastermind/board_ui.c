#include <stdio.h>
#include <string.h>
#include "board_ui.h"

void render_board(struct game_state *state){
    printf("\n");

    for(int i = 0; i < 12; i++){
        if(i < state->current_attempt){
            printf("%s  ", state->guesses[i]);

            for(int j = 0; j < 5; j++){
                if(state->feedbacks[i][j] == 'x')
                    printf(ANSI_GREEN "x" ANSI_RESET);
                else if(state->feedbacks[i][j] == 'o')
                    printf(ANSI_YELLOW "o" ANSI_RESET);
                else
                    printf(ANSI_RED "-" ANSI_RESET);
            }
            printf("\n");
        } 
        else{
            printf("*****  *****\n");
        }
    }
    printf("-----\n");

    if(state->is_mastermind || state->master_sequence[0] != '\0')
        printf("%s\n", state->master_sequence);
    else
        printf("*****\n");
    fflush(stdout);
}