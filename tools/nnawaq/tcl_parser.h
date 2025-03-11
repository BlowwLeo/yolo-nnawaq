
#pragma once

int tcl_init_interp(const char* argv0);
int tcl_exec_line(const char* tcl_line);
int tcl_exec_file(const char* tcl_filename);
int tcl_clear(void);

