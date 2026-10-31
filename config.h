#ifndef ZINC_CONFIG_H
#define ZINC_CONFIG_H

#define ZINC_CONFIG_VERSION 1
#define ZINC_DEFAULT_TAB_WIDTH 8

struct editorConfigOptions {
  int version, tab_width, use_spaces, line_numbers, mouse_enabled;
};

extern struct editorConfigOptions C;

void initConfigOptions( void );

#endif
