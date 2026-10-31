#include "config.h"

struct editorConfigOptions C;

void initConfigOptions() {
  C.version = ZINC_CONFIG_VERSION;
  C.tab_width = ZINC_DEFAULT_TAB_WIDTH;
  C.use_spaces = C.line_numbers = C.mouse_enabled = 0;
}
