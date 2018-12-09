#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>

/*
  The pipelined compiler should supports:
    add, addi, and, andi, or, ori, slt, slti, beq, bne
    data hazard
    control hazard
    forwarding
    lowercase label
    register $t and $s printing
*/

const int t_max = 10;                   // temporary register from $t0 to $t10
const int s_max = 8;                    // saved register from $s0 to $s7
const int cycle_max = 16;               // max cycles is 16
const int instruction_max = 10;         // max count of instructions is 10
const int buffer_size = 128;            // the size of buffer is set to 128
const char nop[4] = "nop";              // string for no operation

// #define debug                           // flag for debugging

struct registers {
  int t[10];
  int s[8];
  char t_name[10][8];
  char s_name[8][8];
  int t_access[10];
  int s_access[8];
};

struct instructions {
  char o_ins[10][128];                  // original instructions
  char le_ins[10][128];                 // label excluded instructions
  int o_count;                          // original instructions count
  int l_pos[10];                        // position that label points to
  char l[10][128];                      // name of the labels
  int le_count;                         // label excluded instructions count
  int l_count;                          // count of labels
  int w_count;                          // count of working instructions
  char w_ins[16][128];                  // working instructions
};

void data_init(struct registers *reg, struct instructions *ins);
void label_preprocess(struct instructions *ins);
void print_table(struct instructions *ins, int w_table[cycle_max][cycle_max + 1]);
void print_reg(struct registers *reg);
void ins_parse(char *ins, char parsed[4][buffer_size]);
int reg_access(struct registers *reg, char *v);
void set_reg_access(struct registers *reg, char v[buffer_size]);
void reset_reg_access(struct registers *reg, char v[buffer_size]);
int check_reg_access(struct registers *reg, char v[buffer_size]);
int *reg_modify(struct registers *reg, char *v);
void calculate(struct registers *reg, char *ins);
void pipeline(struct registers *reg, struct instructions *ins, int forwarding);

int main(int argc, char **argv) {
  // variable declaration
  struct registers reg;
  struct instructions ins;
  int forwarding;

  // arguments validity check
  if (argc != 3) {
    fprintf(stderr, "ERROR: incorrect number of inputs.\n");
    return EXIT_FAILURE;
  } else if (argv[1][0] != 'F' && argv[1][0] != 'N') {
    fprintf(stderr, "ERROR: invalid forwarding setting.\n");
    return EXIT_FAILURE;
  }
  forwarding = argv[1][0] == 'F'? 1: 0;

  // initialize the data
  data_init(&reg, &ins);

  // read the file
  char buffer[buffer_size];
  FILE *ins_file = fopen(argv[2], "r");
  if (ins_file == NULL) {
    fprintf(stderr, "ERROR: cannot open file.\n");
    return EXIT_FAILURE;
  }
  while (fgets(buffer, buffer_size, ins_file)) {
    buffer[strlen(buffer) - 1] = '\0';      // strip of the last '\n'
    // be compatible with windows style newline character, in which case
    // the file has "\r\n" as ending characters for each lines
    if (buffer[strlen(buffer) - 1] == 13)   // '\r' has ASCII code of 13
      buffer[strlen(buffer) - 1] = '\0';
    strcpy(ins.o_ins[ins.o_count++], buffer);
  }
  fclose(ins_file);

#ifdef debug
  int i;
  printf("original instructions read from the file:\n");
  for (i = 0; i < ins.o_count; ++i)
    printf("\tinstruction #%d is [%s].\n", i, ins.o_ins[i]);
#endif

  // preprocess the labels
  label_preprocess(&ins);

#ifdef debug
  printf("label excluded instructions:\n");
  for (i = 0; i < ins.le_count; ++i)
    printf("\tinstruction #%d is [%s].\n", i, ins.le_ins[i]);
  printf("labels:\n");
  for (i = 0; i < ins.l_count; ++i) {
    printf("\tlabel #%d is [%s], ", i, ins.l[i]);
    printf("pointing to instruction #%d above.\n", ins.l_pos[i]);
  }
  if (ins.l_count == 0)
    printf("\tno label found in the given instructions.\n");
#endif

  // pipeline
  pipeline(&reg, &ins, forwarding);
  return EXIT_SUCCESS;
}

void data_init(struct registers *reg, struct instructions *ins) {
  int i, j;
  for (i = 0; i < t_max; ++i) {
    reg->t[i] = 0;
    reg->t_access[i] = 0;
    snprintf(reg->t_name[i], buffer_size, "$t%d", i);
  }
  for (i = 0; i < s_max; ++i) {
    reg->s[i] = 0;
    reg->s_access[i] = 0;
    snprintf(reg->s_name[i], buffer_size, "$s%d", i);
  }
  for (i = 0; i < instruction_max; ++i) {
    for (j = 0; j < buffer_size; ++j) {
      ins->o_ins[i][j] = 0;
      ins->le_ins[i][j] = 0;
      ins->w_ins[i][j] = 0;
      ins->l[i][j] = 0;
    }
    ins->l_pos[i] = -1;
  }
  ins->le_count = 0;
  ins->l_count = 0;
  ins->o_count = 0;
  ins->w_count = 0;
}

void label_preprocess(struct instructions *ins) {
  int i;
  for (i = 0; i < ins->o_count; ++i) {
    if (ins->o_ins[i][strlen(ins->o_ins[i]) - 1] == ':') {
      ins->l_pos[ins->l_count] = ins->le_count;
      strcpy(ins->l[ins->l_count], ins->o_ins[i]);
      ins->l[ins->l_count][strlen(ins->l[ins->l_count]) - 1] = '\0';
      ++ins->l_count;
    } else {
      strcpy(ins->le_ins[ins->le_count], ins->o_ins[i]);
      ++ins->le_count;
    }
  }
  assert(ins->l_count + ins->le_count == ins->o_count);
}

void print_table(struct instructions *ins, int w_table[cycle_max][cycle_max + 1]) {
  // variable declaration
  int i, j;
  char symbol[7][4];                    // symbol store int-string conversion

  // initialize the data
  strcpy(symbol[0], ".");
  strcpy(symbol[1], "IF");
  strcpy(symbol[2], "ID");
  strcpy(symbol[3], "EX");
  strcpy(symbol[4], "MEM");
  strcpy(symbol[5], "WB");
  strcpy(symbol[6], "*");

  // print the first row
  printf("CPU Cycles ===>     ");
  for (i = 1; i < cycle_max; ++i)
    printf("%-4d", i);
  printf("%d\n", cycle_max);

  // print the rest of the table
  for (i = 0; i < ins->w_count; ++i) {
    printf("%-20s", ins->w_ins[i]);
    for (j = 1; j < cycle_max; ++j)
      printf("%-4s", symbol[w_table[i][j]]);
    printf("%s\n", symbol[w_table[i][cycle_max]]);
  }
}

void print_reg(struct registers *reg) {
  int i;
  char buffer[buffer_size];
  for (i = 0; i < s_max; ++i) {
    snprintf(buffer, buffer_size, "%s = %d", reg->s_name[i], reg->s[i]);
    if (i % 4 == 3)
      printf("%s\n", buffer);
    else 
      printf("%-20s", buffer);
  }
  for (i = 0; i < t_max; ++i) {
    snprintf(buffer, buffer_size, "%s = %d", reg->t_name[i], reg->t[i]);
    if ((i + s_max) % 4 == 3)
      printf("%s\n", buffer);
    else 
      printf("%-20s", buffer);
  }
  if ((s_max + t_max) % 4 != 0)
    printf("\n");
}

void ins_parse(char *ins, char parsed[4][buffer_size]) {
  assert(strcmp(ins, "nop"));
  int i;
  int j;
  for (i = 0; i < 4; ++i)
    for (j = 0; j < buffer_size; ++j)
      parsed[i][j] = 0;
  i = 0;
  j = 0;
  while (ins[j] != ' ') ++j;
  strncpy(parsed[0], ins + i, j - i);
  i = j = j + 1;
  while (ins[j] != ',') ++j;
  strncpy(parsed[1], ins + i, j - i);
  i = j = j + 1;
  while (ins[j] != ',') ++j;
  strncpy(parsed[2], ins + i, j - i);
  i = j = j + 1;
  while (ins[j]) ++j;
  strncpy(parsed[3], ins + i, j - i);
}

int reg_access(struct registers *reg, char v[buffer_size]) {
  if (v[0] != '$')
    return atoi(v);
  if (strcmp(v, "$zero") == 0)
    return 0;
  assert(v[1] == 's' || v[1] == 't');
  if (v[1] == 't')
    return reg->t[v[2] - '0'];
  else
    return reg->s[v[2] - '0'];
}

void set_reg_access(struct registers *reg, char v[buffer_size]) {
  assert(v[0] == '$');
  assert(v[1] == 's' || v[1] == 't');
  if (v[1] == 't')
    reg->t_access[v[2] - '0'] = 1;
  else
    reg->s_access[v[2] - '0'] = 1;
}

void reset_reg_access(struct registers *reg, char v[buffer_size]) {
  assert(v[0] == '$');
  assert(v[1] == 's' || v[1] == 't');
  if (v[1] == 't')
    reg->t_access[v[2] - '0'] = 0;
  else
    reg->s_access[v[2] - '0'] = 0;
}

int check_reg_access(struct registers *reg, char v[buffer_size]) {
  assert(v[0] == '$');
  assert(v[1] == 's' || v[1] == 't');
  if (v[1] == 't')
    return reg->t_access[v[2] - '0'];
  else
    return reg->s_access[v[2] - '0'];
}

int *reg_modify(struct registers *reg, char v[buffer_size]) {
  assert(v[0] == '$');
  assert(v[1] == 's' || v[1] == 't');
  if (v[1] == 't')
    return &reg->t[v[2] - '0'];
  else
    return &reg->s[v[2] - '0'];
}

void calculate(struct registers *reg, char *ins) {
  char parsed[4][buffer_size];
  ins_parse(ins, parsed);
  if (strcmp(parsed[0], "add") == 0 || strcmp(parsed[0], "addi") == 0) {
    int *rd = reg_modify(reg, parsed[1]);
    *rd = reg_access(reg, parsed[2]) + reg_access(reg, parsed[3]);
  } else if (strcmp(parsed[0], "and") == 0 || strcmp(parsed[0], "andi") == 0) {
    int *rd = reg_modify(reg, parsed[1]);
    *rd = reg_access(reg, parsed[2]) & reg_access(reg, parsed[3]);
  } else if (strcmp(parsed[0], "or") == 0 || strcmp(parsed[0], "ori") == 0) {
    int *rd = reg_modify(reg, parsed[1]);
    *rd = reg_access(reg, parsed[2]) | reg_access(reg, parsed[3]);
  } else if (strcmp(parsed[0], "slt") == 0 || strcmp(parsed[0], "slti") == 0) {
    int *rd = reg_modify(reg, parsed[1]);
    *rd = reg_access(reg, parsed[2]) < reg_access(reg, parsed[3])? 1: 0;
  }
}

void pipeline(struct registers *reg, struct instructions *ins, int forwarding) {
  // variable declaration
  int i, j, k;
  int time;                             // frame of time
  int w_table[cycle_max][cycle_max + 1];// table for the pipelined execution
  int w_done[cycle_max];                // record the completion of instructions
  int next_ins;                         // next instruction to be pipelined
  // NOTE: 0 = ".", 1 = "IF", 2 = "ID", 3 = "EX", 4 = "MEM", 5 = "WB", 6 = "*"

  // initialize the data
  time = 0;
  next_ins = 0;
  for (i = 0; i < cycle_max; ++i) {
    for (j = 0; j <= cycle_max; ++j)
      w_table[i][j] = 0;
    w_done[i] = 0;
  }

  // simulate pipelining
  if (forwarding)
    printf("START OF SIMULATION (forwarding)\n");
  else
    printf("START OF SIMULATION (no forwarding)\n");
  while (time < cycle_max) {
    int stall = 0;
    ++time;                             // increment the frame of time
#ifdef debug
  printf("#########################################################\n");
  printf("#########################################################\n");
  printf("#########################################################\n");
  printf("time = %d\n", time);
#endif
#ifdef debug
  printf("next_ins = %d\n", next_ins);    
  printf("ins->w_count = %d\n", ins->w_count);
#endif
    for (i = 0; i < ins->w_count; ++i)
      if (w_done[i] == 0) {             // if this instruction is not done
        if (w_table[i][time - 1] == 6)
          w_table[i][time] = 6;
        else
          w_table[i][time] = w_table[i][time - 1] + 1;
      }
#ifdef debug
  printf("print preliminary table\n");
  print_table(ins, w_table);
  printf("print w_done\n");
  for (i = 0; i < ins->w_count; ++i)
    printf("    ins #%d is %d\n", i, w_done[i]);
#endif
    for (i = 0; i < ins->w_count; ++i) {
      if (w_table[i][time - 1] == 6) {
        for (j = time; j >= 0; j--)
          if (w_table[i][j] != 6)
            break;
        if (w_table[i][j] <= 5 && j + 5 - w_table[i][j] == time - 1) {
          w_table[i][time] = 0;
          w_done[i] = 1;
        }
      }
      if (strcmp(ins->w_ins[i], nop) == 0)
        continue;
      char parsed[4][buffer_size];
      ins_parse(ins->w_ins[i], parsed);
      // reset the register access flag immediately after EX or WB, depending
      // on the given forwarding flag
      if (ins->w_ins[i][0] != 'b' && forwarding && w_table[i][time - 1] == 3)
        reset_reg_access(reg, parsed[1]);
      if (ins->w_ins[i][0] != 'b' && !forwarding && w_table[i][time - 1] == 5)
        reset_reg_access(reg, parsed[1]);
      if (w_done[i] == 1)               // if this instruction is done
        continue;                       // skip to next instruction
      // set the w_done state after WB
      if (w_table[i][time] == 5)
        w_done[i] = 1;
      // handle the data hazard when encounter EX or MEM, depending on whether
      // it is branch or not
      if ((w_table[i][time] == 3 && ins->w_ins[i][0] != 'b') ||
        (w_table[i][time] == 4 && ins->w_ins[i][0] == 'b')) {
        int nop_count = 0;              // count of nop that need to be added
        int reg_access_state = 0;
        // check for access state of rs, rt, and determine nop_count
        int a = ins->w_ins[i][0] == 'b'? 1: 2;
        int b = ins->w_ins[i][0] == 'b'? 3: 4;
        for (j = a; j < b; ++j)
          if (parsed[j][0] == '$' && strcmp(parsed[j], "$zero"))
            if (check_reg_access(reg, parsed[j]) == 1) {
              reg_access_state = 1;
              // data hazard has occurred
              if (strcmp(ins->w_ins[i - 1], nop) && ins->w_ins[i - 1][0] != 'b') {
                char parsed1[4][buffer_size];
                ins_parse(ins->w_ins[i - 1], parsed1);
                if (strcmp(parsed1[1], parsed[j]) == 0) {
                  nop_count = 2;
                  break;
                }
              }
              if (i - 2 >= 0 && strcmp(ins->w_ins[i - 2], nop) && ins->w_ins[i - 1][0] != 'b') {
                char parsed2[4][buffer_size];
                ins_parse(ins->w_ins[i - 2], parsed2);
                if (strcmp(parsed2[1], parsed[j]) == 0)
                  nop_count = 1;
              }
            }
#ifdef debug
  printf("nop_count = %d\n", nop_count);
  printf("reg_access_state = %d\n", reg_access_state);
#endif
        if (nop_count > 0) {
          // add nop to the working instructions
          for (j = ins->w_count - 1 + nop_count; j >= i + nop_count; --j) {
            // shift w_ins
            strcpy(ins->w_ins[j], ins->w_ins[j - nop_count]);
            // shift w_done
            w_done[j] = w_done[j - nop_count];
            // shift w_table
            for (k = 1; k <= time; ++k)
              w_table[j][k] = w_table[j - nop_count][k];
            // when adding nop, all subsequent instructions should remain to
            // be in the stage of the last frame of time
            w_table[j][time] = w_table[j][time - 1];
          }
          for (j = i; j < i + nop_count; ++j) {
            // update w_ins
            strcpy(ins->w_ins[j], nop);
            // update w_done
            w_done[j] = 0;
            // update w_table
            for (k = 1; k <= time; ++k)
              w_table[j][k] = w_table[i][k];
            w_table[j][time] = 6;
          }
          // update w_count
          ins->w_count += nop_count;
          stall = 1;
        } else if (reg_access_state) {
          // no need to add nop, but still stall due to two consecutive nops
          for (j = i; j < ins->w_count; j++)
            w_table[j][time] = w_table[j][time - 1];
          stall = 1;
        } else {
          // if no need to add nop or to stall, then just go to EX
          if (ins->w_ins[i][0] != 'b')
            set_reg_access(reg, parsed[1]);
        }
      }
      // handle the control hazard immediately after MEM
      if (w_table[i][time] == 5 && ins->w_ins[i][0] == 'b') {
        int redirect;
        int a, b;
        char parsed[4][buffer_size];
        ins_parse(ins->w_ins[i], parsed);
        a = reg_access(reg, parsed[1]);
        b = reg_access(reg, parsed[2]);
        if (strcmp(parsed[0], "bne") == 0)
          redirect = (a != b);
        else {
          assert(strcmp(parsed[0], "beq") == 0);
          redirect = (a == b);
        }
#ifdef debug
  printf("redirect = %d\n", redirect);
#endif
        if (redirect) {
          // redirect the next instruction
          int location = -1;
          for (j = 0; j < ins->l_count; ++j)
            if (strcmp(ins->l[j], parsed[3]) == 0) {
              location = ins->l_pos[j];
              break;
            }
          assert(location >= 0);
          next_ins = location;
          // invalidate previous guess
          for (j = i + 1; j < ins->w_count; ++j)
            w_table[j][time] = 6;
          // restore previous access of registers
          for (j = i + 1; j < ins->w_count; ++j) {
            if (strcmp(ins->w_ins[j], nop) && ins->w_ins[j][0] != 'b' && ins->w_ins[j][i - 1] >= 3) {
              char parsed[4][buffer_size];
              ins_parse(ins->w_ins[j] ,parsed);
              reset_reg_access(reg, parsed[1]);
            }
          }
          // immediately add the redirected instruction
          printf("redirect next_ins = %d\n", next_ins);
          if (0 <= next_ins && next_ins < ins->le_count) {
            // if next location is pointing to some valid instruction
            strcpy(ins->w_ins[ins->w_count++], ins->le_ins[next_ins]);
            next_ins = next_ins + 1;
          }
          if (next_ins >= ins->le_count)      // if current instruction is the last
            next_ins = -1;                    // there is no next instruction
          w_table[ins->w_count - 1][time] = 1;
        }
      }
      // handle the register calculation immediately after WB
      if (w_table[i][time] == 5 && ins->w_ins[i][0] != 'b')
        calculate(reg, ins->w_ins[i]);
    }
    if (!stall) {
      if (next_ins != -1) {               // if there is next instruction
        strcpy(ins->w_ins[ins->w_count++], ins->le_ins[next_ins]);
        next_ins = next_ins + 1;
        w_table[ins->w_count - 1][time] = 1;
      }
      if (next_ins >= ins->le_count)      // if current instruction is the last
        next_ins = -1;                    // there is no next instruction
    } else
      stall = 0;
    // print out the results
    char buffer[buffer_size];
    memset(buffer, '-', 82);
    buffer[82] = '\0';
    printf("%s\n", buffer);
    print_table(ins, w_table);          // print the pipelined table
    printf("\n");                       // print a new line
    print_reg(reg);                     // print the registers
    // end the pipeline after the completion of last instruction
    if (w_done[ins->w_count - 1])
      break;
  }
  printf("END OF SIMULATION\n");
}
