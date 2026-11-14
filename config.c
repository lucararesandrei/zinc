#include "config.h"

struct editorConfigOptions C;

void initConfigOptions() {
  C.version = ZINC_CONFIG_VERSION;
  C.tab_width = ZINC_DEFAULT_TAB_WIDTH;
  C.scroll_rows = ZINC_DEFAULT_SCROLL_ROWS;
  C.use_spaces = C.line_numbers = 0;
  C.mouse_enabled = 1;
}
