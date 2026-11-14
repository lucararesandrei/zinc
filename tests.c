#define main zinc_editor_main
#include "zinc.c"
#undef main

#define ASSERT( condition ) \
  do { \
    if ( !(condition) ) { \
      fprintf( stderr, "test failed: %s\n", #condition ); \
      return 1; \
    } \
  } while ( 0 )

void resetTestEditor( void ) {
  int i;

  for ( i = 0; i < E.numrows; i++ )
    editorFreeRow( &E.row[i] );
  free( E.row );
  free( E.filename );
  memset( &E, 0, sizeof( E ) );
  memset( &T, 0, sizeof( T ) );
  memset( &S, 0, sizeof( S ) );
  initConfigOptions();
  cleanup_done = 0;
}

int testConfigDefaults( void ) {
  resetTestEditor();
  initConfigOptions();
  ASSERT( C.version == 1 );
  ASSERT( C.tab_width == 8 );
  ASSERT( C.scroll_rows == 3 );
  ASSERT( C.use_spaces == 0 );
  ASSERT( C.line_numbers == 0 );
  ASSERT( C.mouse_enabled == 1 );
  return 0;
}

int testSgrMouseEvents( void ) {
  struct mouseEvent event;

  ASSERT( terminalParseSgrMouseEvent( "\x1b[<64;40;12M", &event ) == 1 );
  ASSERT( event.button == 64 && event.col == 40 && event.row == 12 );
  ASSERT( event.released == 0 );

  ASSERT( terminalParseSgrMouseEvent( "\x1b[<0;5;7M", &event ) == 1 );
  ASSERT( event.button == 0 && event.col == 5 && event.row == 7 );

  ASSERT( terminalParseSgrMouseEvent( "\x1b[<0;5;7m", &event ) == 1 );
  ASSERT( event.released == 1 );

  ASSERT( terminalParseSgrMouseEvent( "\x1b[<64;0;12M", &event ) == 0 );
  ASSERT( terminalParseSgrMouseEvent( "\x1b[<64;40;12X", &event ) == 0 );
  ASSERT( terminalParseSgrMouseEvent( "\x1b[64;40;12M", &event ) == 0 );
  return 0;
}

int testMouseScrolling( void ) {
  struct mouseEvent event;

  resetTestEditor();
  E.screenrows = 4;
  editorInsertRow( 0, "one", 3 );
  editorInsertRow( 1, "two", 3 );
  editorInsertRow( 2, "three", 5 );
  editorInsertRow( 3, "four", 4 );
  editorInsertRow( 4, "five", 4 );
  editorInsertRow( 5, "six", 3 );
  E.rowoff = 0;
  C.scroll_rows = 1;

  event.button = 65;
  event.row = 2;
  ASSERT( editorHandleMouse( &event ) == 1 );
  ASSERT( E.rowoff == 1 );

  event.button = 65;
  ASSERT( editorHandleMouse( &event ) == 1 );
  ASSERT( E.rowoff == 2 );

  event.button = 64;
  ASSERT( editorHandleMouse( &event ) == 1 );
  ASSERT( E.rowoff == 1 );

  event.button = 65;
  event.row = 5;
  ASSERT( editorHandleMouse( &event ) == 0 );
  ASSERT( E.rowoff == 1 );
  return 0;
}

int testMouseClicks( void ) {
  struct mouseEvent event;

  resetTestEditor();
  E.screenrows = 3;
  E.coloff = 0;
  editorInsertRow( 0, "first", 5 );
  editorInsertRow( 1, "ab\tcd", 5 );
  editorInsertRow( 2, "third", 5 );
  editorInsertRow( 3, "last", 4 );
  E.rowoff = 1;

  event.button = 0;
  event.col = 3;
  event.row = 2;
  event.released = 0;
  ASSERT( editorHandleMouseClick( &event ) == 1 );
  ASSERT( E.cy == 2 && E.cx == 2 );

  event.col = 7;
  ASSERT( editorHandleMouseClick( &event ) == 1 );
  ASSERT( E.cy == 2 && E.cx == 5 );

  event.row = 4;
  ASSERT( editorHandleMouseClick( &event ) == 0 );
  event.released = 1;
  ASSERT( editorHandleMouseClick( &event ) == 0 );
  return 0;
}

int testMouseSelection( void ) {
  struct mouseEvent event;

  resetTestEditor();
  E.screenrows = 3;
  editorInsertRow( 0, "first line", 10 );
  editorInsertRow( 1, "second line", 11 );

  event.button = 0;
  event.col = 2;
  event.row = 1;
  event.released = 0;
  event.motion = 0;
  ASSERT( editorHandleMouse( &event ) == 1 );
  ASSERT( S.active == 1 && S.dragging == 1 );

  event.col = 6;
  event.row = 2;
  event.motion = 1;
  ASSERT( editorHandleMouse( &event ) == 1 );
  ASSERT( S.end_cy == 1 && S.end_cx == 5 );
  ASSERT( editorIsSelected( 0, 1 ) == 1 );
  ASSERT( editorIsSelected( 1, 5 ) == 0 );

  event.motion = 0;
  event.released = 1;
  ASSERT( editorHandleMouse( &event ) == 1 );
  ASSERT( S.active == 1 && S.dragging == 0 );
  return 0;
}

int testViewportBounds( void ) {
  resetTestEditor();
  E.screenrows = 2;
  E.screencols = 4;
  editorInsertRow( 0, "0123456789", 10 );
  editorInsertRow( 1, "short", 5 );
  editorInsertRow( 2, "last", 4 );

  E.cy = 0;
  E.rowoff = -4;
  E.coloff = -4;
  editorClampViewport();
  ASSERT( E.rowoff == 0 );
  ASSERT( E.coloff == 0 );

  E.rowoff = 20;
  E.coloff = 20;
  editorClampViewport();
  ASSERT( E.rowoff == 1 );
  ASSERT( E.coloff == 6 );

  E.cy = 1;
  editorClampViewport();
  ASSERT( E.coloff == 1 );
  return 0;
}

int testResizeState( void ) {
  resetTestEditor();
  E.screenrows = 4;
  E.screencols = 80;
  editorInsertRow( 0, "one", 3 );
  editorInsertRow( 1, "two", 3 );
  E.rowoff = 10;
  E.coloff = 10;
  terminal_resize_pending = 0;

  terminalHandleResize( SIGWINCH );
  ASSERT( terminal_resize_pending == 1 );
  editorHandleResize();
  ASSERT( terminal_resize_pending == 0 );
  ASSERT( E.rowoff == 0 );
  ASSERT( E.coloff == 0 );
  return 0;
}

int testRowsAndTabs( void ) {
  erow *row;

  resetTestEditor();
  editorInsertRow( 0, "ab\tcd", 5 );
  ASSERT( E.numrows == 1 );
  ASSERT( E.row[0].size == 5 );
  ASSERT( E.row[0].rsize == 10 );
  ASSERT( strcmp( E.row[0].render, "ab      cd" ) == 0 );

  C.tab_width = 4;
  editorUpdateRow( &E.row[0] );
  ASSERT( E.row[0].rsize == 6 );
  ASSERT( strcmp( E.row[0].render, "ab  cd" ) == 0 );

  C.tab_width = 8;
  editorUpdateRow( &E.row[0] );
  row = &E.row[0];
  ASSERT( editorRowCxToRx( row, 3 ) == 8 );
  ASSERT( editorRowRxToCx( row, 2 ) == 2 );

  editorRowInsertChar( row, 2, 'X' );
  ASSERT( strcmp( row->chars, "abX\tcd" ) == 0 );
  editorRowDelChar( row, 2 );
  ASSERT( strcmp( row->chars, "ab\tcd" ) == 0 );
  return 0;
}

int testCursorMovement( void ) {
  resetTestEditor();
  editorInsertRow( 0, "one", 3 );
  editorInsertRow( 1, "two", 3 );
  E.cy = 0;
  E.cx = 3;

  editorMoveCursor( ARROW_RIGHT );
  ASSERT( E.cy == 1 && E.cx == 0 );
  editorMoveCursor( ARROW_LEFT );
  ASSERT( E.cy == 0 && E.cx == 3 );
  editorMoveCursor( ARROW_DOWN );
  ASSERT( E.cy == 1 && E.cx == 3 );
  editorMoveCursor( ARROW_UP );
  ASSERT( E.cy == 0 && E.cx == 3 );
  return 0;
}

int testSyntaxHighlighting( void ) {
  resetTestEditor();
  E.filename = strdup( "sample.c" );
  editorInsertRow( 0, "int value = 42; // note", 23 );
  editorInsertRow( 1, "/* comment", 10 );
  editorInsertRow( 2, "continues */", 12 );
  editorSelectSyntaxHighlight();

  ASSERT( E.syntax != NULL );
  ASSERT( E.row[0].hl[0] == HL_KEYWORD2 );
  ASSERT( E.row[0].hl[12] == HL_NUMBER );
  ASSERT( E.row[0].hl[16] == HL_COMMENT );
  ASSERT( E.row[1].hl_open_comment == 1 );
  ASSERT( E.row[2].hl[0] == HL_MLCOMMENT );
  ASSERT( E.row[2].hl_open_comment == 0 );
  return 0;
}

int testFileIO( void ) {
  char path[] = "/tmp/zinc-test-XXXXXX";
  char buf[32];
  int fd, len;
  FILE *fp;

  resetTestEditor();
  fd = mkstemp( path );
  ASSERT( fd != -1 );
  close( fd );

  editorOpen( path );
  editorInsertRow( 0, "saved text", 10 );
  editorSave();

  resetTestEditor();
  editorOpen( path );
  ASSERT( E.numrows == 1 );
  ASSERT( strcmp( E.row[0].chars, "saved text" ) == 0 );

  fp = fopen( path, "r" );
  ASSERT( fp != NULL );
  len = fread( buf, 1, sizeof( buf ) - 1, fp );
  fclose( fp );
  buf[len] = '\0';
  ASSERT( strcmp( buf, "saved text\n" ) == 0 );
  unlink( path );
  return 0;
}

int main() {
  if ( testConfigDefaults() != 0 )
    return 1;
  if ( testSgrMouseEvents() != 0 )
    return 1;
  if ( testMouseScrolling() != 0 )
    return 1;
  if ( testMouseClicks() != 0 )
    return 1;
  if ( testMouseSelection() != 0 )
    return 1;
  if ( testViewportBounds() != 0 )
    return 1;
  if ( testResizeState() != 0 )
    return 1;
  if ( testRowsAndTabs() != 0 )
    return 1;
  if ( testCursorMovement() != 0 )
    return 1;
  if ( testSyntaxHighlighting() != 0 )
    return 1;
  if ( testFileIO() != 0 )
    return 1;

  resetTestEditor();
  printf( "All tests passed.\n" );
  return 0;
}
