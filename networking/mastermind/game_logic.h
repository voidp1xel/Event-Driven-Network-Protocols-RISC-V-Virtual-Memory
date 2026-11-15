#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

int validate_sequence(const char *seq);
int validate_feedback(const char *fb);
void calculate_feedback(const char *master, const char *guess, char *feedback);

#endif