#include "config.h"

struct editorConfigOptions C;

void initConfigOptions( void ) {
  C.version = ZINC_CONFIG_VERSION;
  C.tab_width = ZINC_DEFAULT_TAB_WIDTH;
  C.use_spaces = 0;
  C.line_numbers = 0;
  C.mouse_enabled = 0;
}
