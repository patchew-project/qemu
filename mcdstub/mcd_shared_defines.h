// this file is shared between the mcd dll and the mcd stub. it has to be kept exectly the same!

#ifndef MCD_SHARED_DEFINES
#define MCD_SHARED_DEFINES

// tcp characters
#define TCP_CHAR_INIT 'i'
#define TCP_CHAR_GO 'c'
#define TCP_CHAR_QUERY 'q'
#define TCP_CHAR_OPEN_CORE 'H'
#define TCP_CHAR_DETACH 'D'
#define TCP_CHAR_KILLQEMU 'k'

// tcp protocol chars
#define TCP_ACKNOWLEDGED '+'
#define TCP_NOT_ACKNOWLEDGED '-'
#define TCP_COMMAND_START '$'
#define TCP_COMMAND_END '#'
#define TCP_WAS_LAST '|'
#define TCP_WAS_NOT_LAST '~'



#endif