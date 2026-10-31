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
  initConfigOptions();
  cleanup_done = 0;
}

int testConfigDefaults( void ) {
  resetTestEditor();
  initConfigOptions();
  ASSERT( C.version == 1 );
  ASSERT( C.tab_width == 8 );
  ASSERT( C.use_spaces == 0 );
  ASSERT( C.line_numbers == 0 );
  ASSERT( C.mouse_enabled == 0 );
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
