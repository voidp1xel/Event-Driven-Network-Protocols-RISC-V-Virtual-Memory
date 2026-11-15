#ifndef BOARD_UI_H
#define BOARD_UI_H

#define ANSI_RESET  "\x1b[0m"
#define ANSI_GREEN  "\x1b[32m"
#define ANSI_YELLOW "\x1b[33m"
#define ANSI_RED    "\x1b[31m"

struct game_state {
    char guesses[12][6];
    char feedbacks[12][6];
    int current_attempt;
    char master_sequence[6];
    int is_mastermind; // 1 if this player set the code, 0 if guessing
};

void render_board(struct game_state *state);

#endif