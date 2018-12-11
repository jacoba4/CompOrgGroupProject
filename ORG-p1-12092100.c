#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>


/*
  The pipelined compiler should supports:
    add, addi, and, andi, or, ori, slt, slti
    beq, bne
    data hazard
    control hazard
    forwarding for data hazard handling
    lowercase label detection
    printing of register $t and $s
    printing of piplined instruction
*/


#define t_max 10                        // temporary register from $t0 to $t10
#define s_max 8                         // saved register from $s0 to $s7
#define cycle_max 16                    // max cycles is 16
#define instruction_max 10              // max count of instructions is 10
#define buffer_size 128                 // the size of buffer is set to 128
const char nop[4] = "nop";              // string for no operation

// #define debug                           // flag for debugging


struct registers {
  int t[t_max];   char t_name[t_max][8];    int t_access[t_max];
  int s[s_max];   char s_name[s_max][8];    int s_access[s_max];
//       |                   |                        |           //
//       V                   V                        V           //
//    value in the     register like            access state of   //
//    register.        "$t0", "$s0".            the register.     //
};

struct instructions {
  char w_ins[16][128];                  // working instructions
  char o_ins[10][128];                  // original instructions
  char le_ins[10][128];                 // label excluded instructions
  char l[10][128];                      // name of the labels
  int l_pos[10];                        // position that label points to
  int le_count;                         // label excluded instructions count
  int o_count;                          // original instructions count
  int l_count;                          // count of labels
  int w_count;                          // count of working instructions
};


void data_init(struct registers *reg, struct instructions *ins);
// data_init() will initialize the data in reg and ins
void label_preprocess(struct instructions *ins);
// label_preprocess() will detect the labels in the instructions, and get
// label excluded instructions, and record all the labels, and the location in
// the final instructions that they are pointed to.
void print_table(struct instructions *ins, int w_table[cycle_max << 2][cycle_max + 1]);
// print_table() will print out the table for working instructions
void print_reg(struct registers *reg);
// print_reg() will print out the values for all registers
void ins_parse(const char *ins, char parsed[4][buffer_size]);
// ins_parse() will parse a given string to four segments, such that original
// string is substr0 + ' ' + substr1 + ',' + substr2 + ',' + substr3
int reg_access(struct registers *reg, char *v);
// reg_access() will return the value of given register
void set_reg_access(struct registers *reg, char v[buffer_size]);
// set_reg_access() will set the access state of given register so that no
// other execution can use this busy register
void reset_reg_access(struct registers *reg, char v[buffer_size]);
// reset_reg_access() will reset the access state of given register so that
// other execution can reuse this freed register
int check_reg_access(struct registers *reg, char v[buffer_size]);
// check_reg_access() will return the access state of given register
int *reg_modify(struct registers *reg, char *v);
// reg_modify() will return the pointer to the register value so that it can
// be modified during after execution
void calculate(struct registers *reg, char *ins);
// calculate() will execute the instruction and modify the destitation
// register accordingly
void pipeline(struct registers *reg, struct instructions *ins, int forwarding);
// pipeline() will pipeline the instructions and process by the frame of time


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
    // be compatible with:
    //    windows style newline character, "\r\n";
    //    linux style newline character, "\n";
    //    and no newline character at the EOF.
    if (buffer[strlen(buffer) - 1] == 10)   // '\n' has ASCII code of 10
      buffer[strlen(buffer) - 1] = '\0';    // strip of the last '\n'
    if (buffer[strlen(buffer) - 1] == 13)   // '\r' has ASCII code of 13
      buffer[strlen(buffer) - 1] = '\0';    // strip of the last '\r'
    strcpy(ins.o_ins[ins.o_count++], buffer);
  }
  fclose(ins_file);

#ifdef debug
  int i;
  printf("original instructions read from the file:\n");
  for (i = 0; i < ins.o_count; ++i)
    printf("    instruction #%d is [%s].\n", i, ins.o_ins[i]);
#endif

  // preprocess the labels
  label_preprocess(&ins);

#ifdef debug
  printf("label excluded instructions:\n");
  for (i = 0; i < ins.le_count; ++i)
    printf("    instruction #%d is [%s].\n", i, ins.le_ins[i]);
  printf("labels:\n");
  for (i = 0; i < ins.l_count; ++i) {
    printf("    label #%d is [%s], ", i, ins.l[i]);
    printf("pointing to instruction #%d above.\n", ins.l_pos[i]);
  }
  if (ins.l_count == 0)
    printf("    no label found in the given instructions.\n");
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

void print_table(struct instructions *ins, int w_table[cycle_max << 2][cycle_max + 1]) {
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
    for (j = 1; j < cycle_max; ++j) {
      if (w_table[i][j] >= 7) {
        fprintf(stderr, "w_count = %d\n", ins->w_count);
        fprintf(stderr, "ERROR: w_table[%d][%d] = %d\n", i, j, w_table[i][j]);
      }
      assert(w_table[i][j] >= 0);
      assert(w_table[i][j] < 7);
      printf("%-4s", symbol[w_table[i][j]]);
    }
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
    if ((i + s_max) % 4 == 3 || i == t_max - 1)
      printf("%s\n", buffer);
    else 
      printf("%-20s", buffer);
  }
}

void ins_parse(const char *ins, char parsed[4][buffer_size]) {
  assert(strcmp(ins, "nop"));
  int i, j;
  for (i = 0; i < 4; ++i)
    for (j = 0; j < buffer_size; ++j)
      parsed[i][j] = 0;
  i = 0;
  j = 0;
  while (ins[j] != ' ')
    ++j;
  strncpy(parsed[0], ins + i, j - i);
  i = j + 1;
  j = j + 1;
  while (ins[j] != ',')
    ++j;
  strncpy(parsed[1], ins + i, j - i);
  i = j + 1;
  j = j + 1;
  while (ins[j] != ',')
    ++j;
  strncpy(parsed[2], ins + i, j - i);
  i = j + 1;
  j = j + 1;
  while (ins[j])
    ++j;
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
  assert(strcmp(ins, nop) && ins[0] != 'b');
  char parsed[4][buffer_size];
  ins_parse(ins, parsed);
  int *rd = reg_modify(reg, parsed[1]);
  if (strcmp(parsed[0], "add") == 0 || strcmp(parsed[0], "addi") == 0)
    *rd = reg_access(reg, parsed[2]) + reg_access(reg, parsed[3]);
  else if (strcmp(parsed[0], "and") == 0 || strcmp(parsed[0], "andi") == 0)
    *rd = reg_access(reg, parsed[2]) & reg_access(reg, parsed[3]);
  else if (strcmp(parsed[0], "or") == 0 || strcmp(parsed[0], "ori") == 0)
    *rd = reg_access(reg, parsed[2]) | reg_access(reg, parsed[3]);
  else if (strcmp(parsed[0], "slt") == 0 || strcmp(parsed[0], "slti") == 0)
    *rd = reg_access(reg, parsed[2]) < reg_access(reg, parsed[3])? 1: 0;
}

#ifdef debug
void check_w_table(int w_table[cycle_max << 2][cycle_max + 1]) {
  int i, j;
  for (i = 0; i < cycle_max; ++i)
    for (j = 0; j <= cycle_max; ++j)
      assert(w_table[i][j] >= 0 && w_table[i][j] < 7);
}
#endif

void pipeline(struct registers *reg, struct instructions *ins, int forwarding) {
  // variable declaration
  int i, j, k;
  int time;                             // frame of time
  int w_table[cycle_max << 2][cycle_max + 1];
  // table for the pipelined execution. NOTE: the count of working instruction
  // may exceeds cycle_max due to the insertion of nops
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
    int stall = 0;                      // flag for stall of pipelining
    ++time;                             // increment the frame of time
#ifdef debug
  printf("#########################################################\n");
  printf("#########################################################\n");
  printf("    (*) time = %d\n", time);
  printf("    (*) next_ins = %d\n", next_ins);    
  printf("    (*) ins->w_count = %d\n", ins->w_count);
#endif
    for (i = 0; i < ins->w_count; ++i)
      if (w_done[i] == 0) {             // if this instruction is not done
        if (w_table[i][time - 1] == 6)  // if previous stage is a bubble
          w_table[i][time] = 6;         // the next stage should be a bubble
        else                            // else, increment the stage
          w_table[i][time] = w_table[i][time - 1] + 1;
      }
#ifdef debug
  printf("print preliminary table\n");
  print_table(ins, w_table);
  printf("print w_done\n");
  for (i = 0; i < ins->w_count; ++i)
    printf("    instruction #%d is %d\n", i, w_done[i]);
#endif
    for (i = 0; i < ins->w_count; ++i) {
      if (w_table[i][time - 1] == 6) {    // handle the special case of nop
        for (j = time; j >= 0; j--)       // and invalidated instruction
          if (w_table[i][j] != 6)         // find the most recent non-bubble
            break;                        // stage and record the position j
        // we assume that bubbles should simulate all the stages, e.g.
        // IF ID ID ID EX * *, or IF ID ID ID ID ID * * *.
        if (w_table[i][j] <= 5 && j + 5 - w_table[i][j] == time - 1) {
          w_table[i][time] = 0;
          w_done[i] = 1;
        }
      }
      if (strcmp(ins->w_ins[i], nop) == 0)// only if the instruction is not
        continue;                         // nop, we continue to parsing
      char parsed[4][buffer_size];
      ins_parse(ins->w_ins[i], parsed);
      // reset the register access flag immediately after EX or WB
      if (ins->w_ins[i][0] != 'b' && forwarding && w_table[i][time - 1] == 3)
        reset_reg_access(reg, parsed[1]);
      if (ins->w_ins[i][0] != 'b' && !forwarding && w_table[i][time - 1] == 5)
        reset_reg_access(reg, parsed[1]);
      if (w_done[i] == 1)               // if this instruction is done
        continue;                       // skip to next instruction
      if (w_table[i][time] == 5)
        w_done[i] = 1;                  // set the w_done state after WB
      // handle the data hazard when encounter EX or MEM, depending on whether
      // it is a branch instruction or not
      if ((w_table[i][time] == 3 && ins->w_ins[i][0] != 'b') ||
        (w_table[i][time] == 4 && ins->w_ins[i][0] == 'b')) {
        int nop_count = 0;              // count of nop that need to be added
        int reg_access_state = 0;       // access state of register
        // check for access state of rs, rt, and determine nop_count
        int a = ins->w_ins[i][0] == 'b'? 1: 2;
        int b = ins->w_ins[i][0] == 'b'? 3: 4;
        // for branch instruction, rd and rs are dependent registers
        // for non-branch instruction, rs and rt are dependent registers
        for (j = a; j < b; ++j)
          if (parsed[j][0] == '$' && strcmp(parsed[j], "$zero")) {
            if (check_reg_access(reg, parsed[j]) == 1) {
              reg_access_state = 1;       // the register is busy for use
              // data hazard has occurred
              if (strcmp(ins->w_ins[i - 1], nop)
                && ins->w_ins[i - 1][0] != 'b') {
                // only check with previous instruction if it is not a branch
                // instruction or nop
                char parsed1[4][buffer_size];
                ins_parse(ins->w_ins[i - 1], parsed1);
                if (strcmp(parsed1[1], parsed[j]) == 0) {
                  nop_count = 2;
                  break;
                }
              }
              if (i - 2 >= 0 && strcmp(ins->w_ins[i - 2], nop)
                && ins->w_ins[i - 1][0] != 'b') {
                // only check with previous two instruction if it exists, and
                // it is not a branch instruction or nop
                char parsed2[4][buffer_size];
                ins_parse(ins->w_ins[i - 2], parsed2);
                if (strcmp(parsed2[1], parsed[j]) == 0)
                  nop_count = 1;
              }
            }
          }
#ifdef debug
  printf("nop_count for instruction[%d] = %d\n", i, nop_count);
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
            strcpy(ins->w_ins[j], nop);   // update w_ins
            w_done[j] = 0;                // update w_done
            for (k = 1; k <= time; ++k)   // update w_table
              w_table[j][k] = w_table[i][k];
            w_table[j][time] = 6;
          }
          ins->w_count += nop_count;      // update w_count
          stall = 1;                      // set the stall flag
        } else if (reg_access_state) {
          // no need to add nop, but still stall due to two consecutive nops
          for (j = i; j < ins->w_count; j++)
            w_table[j][time] = w_table[j][time - 1];
          stall = 1;                      // set the stall flag
        } else {
          // if no need to add nop or to stall, then just go to EX
          if (ins->w_ins[i][0] != 'b')      // set the register access state
            set_reg_access(reg, parsed[1]); // for a non-branch instruction
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
        assert(!(strcmp(parsed[0], "bne") && strcmp(parsed[0], "beq")));
        if (strcmp(parsed[0], "bne") == 0)
          redirect = (a != b);
        else
          redirect = (a == b);
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
#ifdef debug
  printf("redirect next_ins to be %d\n", next_ins);
#endif
          if (0 <= next_ins && next_ins < ins->le_count) {
            // if next location is pointing to some valid instruction
            strcpy(ins->w_ins[ins->w_count++], ins->le_ins[next_ins]);
            next_ins = next_ins + 1;
          }
          if (next_ins >= ins->le_count)      // if current instruction is the last
            next_ins = -1;                    // there is no next instruction
          w_table[ins->w_count - 1][time] = 1;
          stall = 1;                          // temporarily set the stall flag
          // to evade the insertion of instruction routinely at the end
        }
      }
      // handle the register calculation immediately after WB
      if (w_table[i][time] == 5 && ins->w_ins[i][0] != 'b')
        calculate(reg, ins->w_ins[i]);
    }
#ifdef debug
  printf("stall = %d\n", stall);
#endif
    if (!stall) {
      if (next_ins != -1) {               // if there is next instruction
        strcpy(ins->w_ins[ins->w_count++], ins->le_ins[next_ins]);
        next_ins = next_ins + 1;          // increment the next_ins
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
  char buffer[buffer_size];
  memset(buffer, '-', 82);
  buffer[82] = '\0';
  printf("%s\n", buffer);
  printf("END OF SIMULATION\n");
}
