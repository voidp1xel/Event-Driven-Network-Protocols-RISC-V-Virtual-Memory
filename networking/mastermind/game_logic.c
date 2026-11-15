#include <string.h>
#include <ctype.h>
#include "game_logic.h"

int validate_sequence(const char *seq){
    if(strlen(seq) != 5) return 0;
    for(int i = 0; i < 5; i++) {
        if(!isdigit((unsigned char)seq[i])) return 0;
    }
    return 1;
}

int validate_feedback(const char *fb){
    if(strlen(fb) != 5) return 0;
    for(int i = 0; i < 5; i++) {
        if(fb[i] != 'o' && fb[i] != 'x' && fb[i] != '-') return 0;
    }
    return 1;
}

void calculate_feedback(const char *master, const char *guess, char *feedback){
    int x_count = 0;
    int master_counts[10] = {0};
    int guess_counts[10] = {0};

    /* Pass 1: count exact positional matches ('x') and record remaining digit frequencies. */
    for(int i = 0; i < 5; i++){
        if(master[i] == guess[i]){
            x_count++;
        } 
        else{
            master_counts[master[i] - '0']++;
            guess_counts[guess[i] - '0']++;
        }
    }

    /* Pass 2: count misplaced digit matches ('o') from non-exact positions. */
    int o_count = 0;
    for(int i = 0; i < 10; i++){
        if(master_counts[i] < guess_counts[i])
            o_count += master_counts[i];
        else
            o_count += guess_counts[i];
    }

    /* Format canonical feedback string (e.g., "xxoo-"). */
    int idx = 0;
    for(int i = 0; i < x_count; i++) feedback[idx++] = 'x';
    for(int i = 0; i < o_count; i++) feedback[idx++] = 'o';
    while(idx < 5) feedback[idx++] = '-';
    feedback[5] = '\0';
}