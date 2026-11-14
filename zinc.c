/*   INCLUDES   */
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "config.h"

/*   DEFINES   */
#define ZINC_VERSION "0.2"
#define ZINC_QUIT_TIMES 1

#define CTRL_KEY( k ) ( (k) & 0x1f )

enum editorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN,
  MOUSE_EVENT
};

enum editorHighlight {
  HL_NORMAL = 0,
  HL_COMMENT,
  HL_MLCOMMENT,
  HL_KEYWORD1,
  HL_KEYWORD2,
  HL_STRING,
  HL_NUMBER,
  HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1 << 1)

/*   DATA   */
struct editorSyntax {
  char *filetype;
  char **filematch;
  char **keywords;
  char *singleline_comment_start;
  char *multiline_comment_start;
  char *multiline_comment_end;
  int flags;
};

typedef struct erow {
  int idx;
  int size, rsize;
  char *chars, *render;
	unsigned char *hl;
  int hl_open_comment;
} erow;

struct editorConfig {
  int cx, cy;
  int rx;
  int coloff, rowoff;
  int screenrows, screencols;
  int numrows; erow *row;
  int dirty;
  char *filename;
  char statusmsg[80]; time_t statusmsg_time;
  int statusmsg_warning;
  struct editorSyntax *syntax;
  struct termios orig_termios;
};

struct editorConfig E;
struct terminalState {
  int raw_mode, supports_sgr_mouse, supports_truecolor, rows, cols;
};

struct terminalState T;
int cleanup_done = 0;
volatile sig_atomic_t terminal_resize_pending = 0;
struct mouseEvent {
  int button, col, row, released, motion;
};
struct mouseEvent last_mouse_event;
struct editorSelection {
  int active, dragging;
  int anchor_cx, anchor_cy, anchor_rx, anchor_ry;
  int end_cx, end_cy, end_rx, end_ry;
};
struct editorSelection S;

/*   FILETYPES   */
char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL };
char *C_HL_keywords[] = {
  "switch", "if", "while", "for", "break", "continue", "return", "else",
  "struct", "union", "typedef", "static", "enum", "class", "case",

  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", NULL
};

struct editorSyntax HLDB[] = {
  {
    "c",
    C_HL_extensions,
    C_HL_keywords,
    "//", "/*", "*/",
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
  },
};

#define HLDB_ENTRIES (sizeof( HLDB ) / sizeof( HLDB[0] ))

/*   PROTOTYPES   */
void editorSetStatusMessage( const char *fmt, ... );
void editorRefreshScreen();
char *editorPrompt( char *prompt, void (*callback)(char *, int) );
void editorCleanup( void );
ssize_t terminalRead( void *buf, size_t count );
void terminalWrite( const char *s, size_t len );
int terminalGetSize( int *rows, int *cols );
void terminalDetectCapabilities( void );
int getWindowSize( int *rows, int *cols );
int terminalParseSgrMouseEvent( const char *sequence,
                                struct mouseEvent *event );
void terminalSetMouseReporting( int enabled );
void editorToggleMouse( void );
void terminalHandleResize( int signal_number );
void editorHandleResize( void );
void initConfigOptions( void );
void editorClampViewport( void );
void editorScrollRows( int amount );
int editorHandleMouse( struct mouseEvent *event );
int editorHandleMouseClick( struct mouseEvent *event );
int editorMouseToCursor( struct mouseEvent *event, int *cy, int *cx, int *rx );
int editorIsSelected( int filerow, int render_col );
void editorStartSelection( int cy, int cx, int rx );
void editorUpdateSelection( int cy, int cx, int rx );

/*   TERMINAL   */
void die( const char *s ) {
  editorCleanup();
  perror( s );
  exit( 1 );
}

ssize_t terminalRead( void *buf, size_t count ) {
  return read( STDIN_FILENO, buf, count );
}

void terminalWrite( const char *s, size_t len ) {
  size_t written;
  ssize_t result;

  written = 0;
  while ( written < len ) {
    result = write( STDOUT_FILENO, &s[written], len - written );
    if ( result <= 0 ) {
      perror( "write" );
      return;
    }
    written += result;
  }
}

int terminalGetSize( int *rows, int *cols ) {
  int result;

  result = getWindowSize( rows, cols );
  if ( result == 0 ) {
    T.rows = *rows;
    T.cols = *cols;
  }
  return result;
}

void terminalDetectCapabilities( void ) {
  char *term;
  char *colorterm;

  term = getenv( "TERM" );
  colorterm = getenv( "COLORTERM" );
  T.supports_sgr_mouse = term != NULL && strcmp( term, "dumb" ) != 0;
  T.supports_truecolor = colorterm != NULL &&
    (strstr( colorterm, "truecolor" ) != NULL ||
     strstr( colorterm, "24bit" ) != NULL);
}

int terminalParseSgrMouseEvent( const char *sequence,
                                struct mouseEvent *event ) {
  int consumed, len;
  char type;

  if ( sequence == NULL || event == NULL )
    return 0;

  len = strlen( sequence );
  if ( len < 9 || (sequence[len - 1] != 'M' && sequence[len - 1] != 'm') )
    return 0;

  if ( sscanf( sequence, "\x1b[<%d;%d;%d%n", &event->button,
               &event->col, &event->row, &consumed ) != 3 )
    return 0;

  type = sequence[len - 1];
  if ( consumed != len - 1 || event->button < 0 ||
      event->col < 1 || event->row < 1 )
    return 0;

  event->motion = (event->button & 32) != 0;
  if ( event->button < 64 )
    event->button &= 3;
  event->released = type == 'm';
  return 1;
}

void terminalSetMouseReporting( int enabled ) {
  if ( !T.supports_sgr_mouse )
    return;

  if ( enabled )
    terminalWrite( "\x1b[?1003h\x1b[?1006h", 18 );
  else
    terminalWrite( "\x1b[?1003l\x1b[?1006l", 18 );
}

void editorToggleMouse( void ) {
  C.mouse_enabled = !C.mouse_enabled;
  terminalSetMouseReporting( C.mouse_enabled );
  editorSetStatusMessage( "Mouse support: %s",
                          C.mouse_enabled ? "enabled" : "disabled" );
}

void terminalHandleResize( int signal_number ) {
  (void) signal_number;
  terminal_resize_pending = 1;
}

void editorHandleResize( void ) {
  int rows, cols;

  if ( !terminal_resize_pending )
    return;
  terminal_resize_pending = 0;
  if ( terminalGetSize( &rows, &cols ) == -1 ) {
    editorClampViewport();
    return;
  }
  E.screenrows = rows - 2;
  E.screencols = cols;
  if ( E.screenrows < 1 )
    E.screenrows = 1;
  if ( E.screencols < 1 )
    E.screencols = 1;
  editorClampViewport();
}

void disableRawMode() {
  if ( !T.raw_mode )
    return;

  if ( tcsetattr( STDIN_FILENO, TCSAFLUSH, &E.orig_termios ) == -1 )
    perror( "tcsetattr" );
  T.raw_mode = 0;
}

void enableRawMode() {
  if ( tcgetattr( STDIN_FILENO, &E.orig_termios ) == -1 )
    die( "tcgetattr" );
  atexit( editorCleanup );

  struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(IXON | ICRNL | INPCK | ISTRIP | BRKINT);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if ( tcsetattr( STDIN_FILENO, TCSAFLUSH, &raw ) == -1 )
    die( "tcsetattr" );
  T.raw_mode = 1;
  terminalSetMouseReporting( C.mouse_enabled );
}

int editorReadKey() {
  int nread;
  char c;

  while ( (nread = terminalRead( &c, 1 )) != 1 ) 
    if ( nread == -1 && errno != EAGAIN )
      die( "read" );

  if ( c == '\x1b' ) {
    char seq[3];

    if ( terminalRead( &seq[0], 1 ) != 1 )
      return '\x1b';
    if ( terminalRead( &seq[1], 1 ) != 1 )
      return '\x1b';

    if ( seq[0] == '[' ) {
      if ( seq[1] == '<' && C.mouse_enabled ) {
        char mouse_seq[64];
        int mouse_len;
        char mouse_char;

        mouse_seq[0] = '\x1b';
        mouse_seq[1] = '[';
        mouse_seq[2] = '<';
        mouse_len = 3;
        while ( mouse_len < (int) sizeof( mouse_seq ) - 1 ) {
          if ( terminalRead( &mouse_char, 1 ) != 1 )
            return '\x1b';
          mouse_seq[mouse_len++] = mouse_char;
          if ( mouse_char == 'M' || mouse_char == 'm' )
            break;
        }
        mouse_seq[mouse_len] = '\0';
        if ( terminalParseSgrMouseEvent( mouse_seq, &last_mouse_event ) )
          return MOUSE_EVENT;
        return '\x1b';
      }
      if ( seq[1] >= '0' && seq[1] <= '9' ) {
        if ( terminalRead( &seq[2], 1 ) != 1 )
          return '\x1b';
        if ( seq[2] == '~' ) {
          switch ( seq[1] ) {
            case '1': return HOME_KEY;
            case '3': return DEL_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
      } else {
        switch ( seq[1] ) {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    } else if ( seq[0] == 'O' ) {
      switch ( seq[1] ) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }

    return '\x1b';
  }

  return c;
}

int getCursorPosition( int *rows, int *cols ) {
  char buf[32];
  unsigned int i = 0;

  terminalWrite( "\x1b[6n", 4 );

  while ( i < sizeof( buf ) - 1 ) {
    if ( terminalRead( &buf[i], 1 ) != 1 )
      break;
    if ( buf[i] == 'R' )
      break;
    i++;
  }

  buf[i] = '\0';

  if ( buf[0] != '\x1b' || buf[1] != '[' )
    return -1;
  if ( sscanf( &buf[2], "%d;%d", rows, cols ) != 2 )
    return -1;

  return 0;
}

int getWindowSize( int *rows, int *cols ) {
  struct winsize ws;

  if ( ioctl( STDOUT_FILENO, TIOCGWINSZ, &ws ) == -1 || ws.ws_col == 0 ) {
    
    terminalWrite( "\x1b[999C\x1b[999B", 12 );

    return getCursorPosition( rows, cols );

  }
  
  *cols = ws.ws_col;
  *rows = ws.ws_row;
  return 0;
}

/*   SYNTAX HIGHLIGHTING   */
int is_separator( int c ) {
  return isspace( c ) || c == '\0' || strchr( ",.()+-/*=~%<>[];", c ) != NULL;
}

void editorUpdateSyntax( erow *row ) {
  row->hl = realloc( row->hl, row->rsize );
  memset( row->hl, HL_NORMAL, row->rsize );

  if ( E.syntax == NULL )
    return;

  char **keywords = E.syntax->keywords;

  char *scs = E.syntax->singleline_comment_start;
  char *mcs = E.syntax->multiline_comment_start;
  char *mce = E.syntax->multiline_comment_end;
  int scs_len = scs ? strlen( scs ) : 0;
  int mcs_len = mcs ? strlen( mcs ) : 0;
  int mce_len = mce ? strlen( mce ) : 0;
  
  int prev_sep = 1;
  int in_string = 0;
  int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

  int i = 0;
  while ( i < row->rsize ) {
    char c = row->render[i];
    unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

    if ( scs_len && !in_string && !in_comment ) {
      if ( !strncmp( &row->render[i], scs, scs_len ) ) {
        memset( &row->hl[i], HL_COMMENT, row->rsize - i );
        break;
      }
    }

    if ( mcs_len && mce_len && !in_string ) {
      if ( in_comment ) {
        row->hl[i] = HL_MLCOMMENT;
        if ( !strncmp( &row->render[i], mce, mce_len ) ) {
          memset( &row->hl[i], HL_MLCOMMENT, mce_len );
          i += mce_len;
          in_comment = 0;
          prev_sep = 1;
          continue;
        } else {
          i++;
          continue;
        }
      } else if ( !strncmp( &row->render[i], mcs, mcs_len ) ) {
        memset( &row->hl[i], HL_MLCOMMENT, mcs_len );
        i += mcs_len;
        in_comment = 1;
        continue;
      }
    }

    if ( E.syntax->flags & HL_HIGHLIGHT_STRINGS ) {
      if ( in_string ) {
        row->hl[i] = HL_STRING;
        if ( c == '\\' && i + 1 < row->rsize ) {
          row->hl[i + 1] = HL_STRING;
          i += 2;
          continue;
        }
        if ( c == in_string )
          in_string = 0;
        i++;
        prev_sep = 1;
        continue;
      } else {
        if ( c == '"' || c == '\'') {
          in_string = c;
          row->hl[i] = HL_STRING;
          i++;
          continue;
        }
      }
    }

    if ( E.syntax->flags & HL_HIGHLIGHT_NUMBERS ) {
      if ( ( isdigit( c ) && (prev_sep || prev_hl == HL_NUMBER) ) || 
          ( c == '.' && prev_hl == HL_NUMBER ) ) {
        row->hl[i] = HL_NUMBER;
        i++;
        prev_sep = 0;
        continue;
      }
    }

    if ( prev_sep ) {
      int j;
      for ( j = 0; keywords[j]; j++ ) {
        int klen = strlen( keywords[j] );
        int kw2 = keywords[j][klen - 1] == '|';
        if ( kw2 )
          klen--;

        if ( !strncmp( &row->render[i], keywords[j], klen ) &&
            is_separator( row->render[i + klen] ) ) {
          memset( &row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen );
          i += klen;
          break;
        }
      }

      if ( keywords[j] != NULL ) {
        prev_sep = 0;
        continue;
      }
    }

    prev_sep = is_separator( c );
    i++;
  }
  
  int changed = (row->hl_open_comment != in_comment);
  row->hl_open_comment = in_comment;
  if ( changed && row->idx + 1 < E.numrows )
    editorUpdateSyntax( &E.row[row->idx + 1] );
}

int editorSyntaxToColor( int hl ) {
  switch ( hl ) {
    case HL_COMMENT:
    case HL_MLCOMMENT: return 36;
    case HL_KEYWORD1: return 33;
    case HL_KEYWORD2: return 32;
    case HL_STRING: return 35;
    case HL_NUMBER: return 31;
    case HL_MATCH: return 34;
    default: return 37;
  }
}

void editorSelectSyntaxHighlight() {
  E.syntax = NULL;
  if ( E.filename == NULL )
    return;

  char *ext = strrchr( E.filename, '.' );
  unsigned int j;

  for ( j = 0; j < HLDB_ENTRIES; j++ ) {
    struct editorSyntax *s = &HLDB[j];
    unsigned int i = 0;
    while ( s->filematch[i] ) {
      int is_ext = (s->filematch[i][0] == '.');
      if ( ( is_ext && ext && !strcmp( ext, s->filematch[i] ) ) ||
          ( !is_ext && strstr( E.filename, s->filematch[i] ) ) ) {
        E.syntax = s;
        
        int filerow;
        for ( filerow = 0; filerow < E.numrows; filerow++ )
          editorUpdateSyntax( &E.row[filerow] );

        return;
      }
      i++;
    }
  }
}

/*   ROW OPERATIONS   */
int editorRowCxToRx( erow *row, int cx ) {
  int rx = 0, j;
  for ( j = 0; j < cx; j++ ) {
    if ( row->chars[j] == '\t' )
      rx += (C.tab_width - 1) - (rx % C.tab_width);
    rx++;
  }

  return rx;
}

int editorRowRxToCx( erow *row, int rx ) {
  int cur_rx = 0, cx;
  for ( cx = 0; cx < row->size; cx++ ) {
    if ( row->chars[cx] == '\t' )
      cur_rx += (C.tab_width - 1) - (cur_rx % C.tab_width);
    cur_rx++;

    if ( cur_rx > rx )
      return cx;
  }
  return cx;
}

void editorUpdateRow( erow *row ) {
  int tabs = 0, j;
  for ( j = 0; j < row->size; j++ )
    if ( row->chars[j] == '\t' )
      tabs++;

  free( row->render );
  row->render = malloc( row->size + (C.tab_width - 1) * tabs + 1 );

  int idx = 0;
  for ( j = 0; j < row->size; j++ ) {
    if ( row->chars[j] == '\t' ) {
      row->render[idx++] = ' ';
      while ( idx % C.tab_width != 0 )
        row->render[idx++] = ' ';
    } else 
      row->render[idx++] = row->chars[j];
  }
  row->render[idx] = '\0';
  row->rsize = idx;

  editorUpdateSyntax(row);
}

void editorInsertRow( int at, char *s, size_t len ) {
  if ( at < 0 || at > E.numrows )
    return;

  int j;
  E.row = realloc( E.row, sizeof( erow ) * (E.numrows + 1) );
  memmove( &E.row[at + 1], &E.row[at], sizeof( erow ) * (E.numrows - at) );
  for ( j = at + 1; j <= E.numrows; j++ )
    E.row[j].idx++;

  E.row[at].idx = at;

  E.row[at].size = len;
  E.row[at].chars = malloc( len + 1 );
  memcpy( E.row[at].chars, s, len );
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  E.row[at].hl = NULL;
  E.row[at].hl_open_comment = 0;
  editorUpdateRow( &E.row[at] );

  E.numrows++;
  E.dirty++;
}

void editorFreeRow( erow *row ) {
  free( row->render );
  free( row->chars );
  free( row->hl );
}

void editorCleanup( void ) {
  int i;

  if ( cleanup_done )
    return;
  cleanup_done = 1;

  terminalSetMouseReporting( 0 );
  disableRawMode();
  for ( i = 0; i < E.numrows; i++ )
    editorFreeRow( &E.row[i] );
  free( E.row );
  free( E.filename );
  terminalWrite( "\x1b[2J\x1b[H", 7 );
}

void editorDelRow( int at ) {
  if ( at < 0 || at >= E.numrows)
    return;
  editorFreeRow( &E.row[at] );
  memmove( &E.row[at], &E.row[at + 1], sizeof( erow ) * (E.numrows - at - 1) );
  int j;
  for ( j = at; j < E.numrows - 1; j++ )
    E.row[j].idx--;
  E.numrows--;
  E.dirty++;
}

void editorRowInsertChar( erow *row, int at, int c ) {
  if ( at < 0 || at > row->size )
    at = row->size;
  row->chars = realloc( row->chars, row->size + 2 );
  memmove( &row->chars[at + 1], &row->chars[at], row->size - at + 1 );
  row->size++;
  row->chars[at] = c;
  editorUpdateRow( row );
  E.dirty++;
}

void editorRowAppendString( erow *row, char *s, size_t len ) {
  row->chars = realloc( row->chars, row->size + len + 1 );
  memcpy( &row->chars[row->size], s, len );
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow( row );
  E.dirty++;
}

void editorRowDelChar( erow *row, int at ) {
  if ( at < 0 || at >= row->size)
    return;
  memmove( &row->chars[at], &row->chars[at + 1], row->size - at );
  row->size--;
  editorUpdateRow( row );
  E.dirty++;
}

/*   EDITOR OPERATIONS   */
void editorInsertChar( int c ) {
  if ( E.cy == E.numrows )
    editorInsertRow( E.numrows, "", 0 );

  editorRowInsertChar( &E.row[E.cy], E.cx, c );
  E.cx++;
}

void editorInsertNewline() {
  if ( E.cx == 0 ) 
    editorInsertRow( E.cy, "", 0 );
  else {
    erow *row = &E.row[E.cy];
    editorInsertRow( E.cy + 1, &row->chars[E.cx], row->size - E.cx );
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow( row );
  }
  E.cy++;
  E.cx = 0;
}

void editorDelChar() {
  if ( E.cy == E.numrows )
    return;
  if ( E.cx == 0 && E.cy == 0 )
    return;

  erow *row = &E.row[E.cy];
  if ( E.cx > 0 ) {
    editorRowDelChar( row, E.cx - 1 );
    E.cx--;
  } else {
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString( &E.row[E.cy - 1], row->chars, row->size );
    editorDelRow( E.cy );
    E.cy--;
  }
}

/*   FILE I/O   */
char *editorRowsToString( int *buflen ) {
  int totlen = 0, j;
  for ( j = 0; j < E.numrows; j++ )
    totlen += E.row[j].size + 1;
  *buflen = totlen;

  char *buf = malloc( totlen );
  char *p = buf;
  for ( j = 0; j < E.numrows; j++ ) {
    memcpy( p, E.row[j].chars, E.row[j].size );
    p += E.row[j].size;
    *p = '\n';
    p++;
  }

  return buf;
}

void editorOpen( char *filename ) {
  free( E.filename );
  E.filename = strdup( filename );

  editorSelectSyntaxHighlight();

  FILE *fp = fopen( filename, "r" );
  if ( !fp )
    die( "fopen" );

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  while ( ( linelen = getline( &line, &linecap, fp ) ) != -1 ) {
    while ( linelen > 0 && ( line[linelen - 1] == '\n' || line[linelen - 1] == '\r' ) )
      linelen--;

    editorInsertRow( E.numrows, line, linelen );  
  }

  free( line );
  fclose( fp );
  E.dirty = 0;
}

void editorSave() {
  if ( E.filename == NULL ) {
    E.filename = editorPrompt( "Save as: %s (ESC to cancel)", NULL );
    if ( E.filename == NULL ) {
      editorSetStatusMessage( "Save aborted" );
      return;
    }
    editorSelectSyntaxHighlight();
  }

  int len;
  char *buf = editorRowsToString( &len );

  int fd = open( E.filename, O_RDWR | O_CREAT, 0644 );
  if ( fd != -1 ) {
    if ( ftruncate( fd, len ) != -1 ) {
      if ( write( fd, buf, len ) == len ) {
        close( fd );
        free( buf );
        E.dirty = 0;
        editorSetStatusMessage( "%d bytes written to disk", len );
        return;
      }
    }
    close( fd );
  }  

  free( buf );
  editorSetStatusMessage( "Can't save! I/O error: %s", strerror( errno ) );
}

/*   FIND   */
void editorFindCallback( char *query, int key ) {
  static int last_match = -1;
  static int direction = 1;

  static int saved_hl_line;
  static char *saved_hl = NULL;

  if ( saved_hl ) {
    memcpy( E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize );
    free( saved_hl );
    saved_hl = NULL;
  }

  if ( key == '\r' || key == '\x1b' ) {
    last_match = -1;
    direction = 1;
    return;
  } else if ( key == ARROW_RIGHT || key == ARROW_DOWN ) {
    direction = 1;
  } else if ( key == ARROW_LEFT || key == ARROW_UP ) {
    direction = -1;
  } else {
    last_match = -1;
    direction = 1;
  }

  if ( last_match == -1 )
    direction = 1;
  int current = last_match, i;
  for ( i = 0; i < E.numrows; i++ ) {
    current += direction;
    if ( current == -1 )
      current = E.numrows - 1;
    else if ( current == E.numrows )
      current = 0;

    erow *row = &E.row[current];
    char *match = strstr( row->render, query );
    if ( match ) {
      last_match = current;
      E.cy = current;
      E.cx = editorRowRxToCx( row, match - row->render );
      E.rowoff = E.numrows;

      saved_hl_line = current;
      saved_hl = malloc( row->rsize );
      memcpy( saved_hl, row->hl, row->rsize );
      memset( &row->hl[match - row->render], HL_MATCH, strlen( query ) );
      break;
    }
  }
}

void editorFind() {
  int saved_cx = E.cx,
      saved_cy = E.cy,
      saved_coloff = E.coloff,
      saved_rowoff = E.rowoff;
  
  char *query = editorPrompt( "Search: %s (Use ESC/Arrows/Enter)", editorFindCallback );

  if ( query )
    free( query );
  else {
    E.cx = saved_cx;
    E.cy = saved_cy;
    E.coloff = saved_coloff;
    E.rowoff = saved_rowoff;
  }
}

/*   APPEND BUF   */
struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT { NULL, 0 }

void abAppend( struct abuf *ab, const char *s, int len ) {
  char *new = realloc( ab->b, ab->len + len );

  if ( new == NULL )
    return;

  memcpy( &new[ab->len], s, len );
  ab->b = new;
  ab->len += len;
}

void abFree( struct abuf *ab ) {
  free(ab->b);
}

/*   OUTPUT   */
void editorScroll() {
  E.rx = 0;
  if ( E.cy < E.numrows ) 
    E.rx = editorRowCxToRx( &E.row[E.cy], E.cx );

  if ( E.cy < E.rowoff )
    E.rowoff = E.cy;

  if ( E.cy >= E.rowoff + E.screenrows )
    E.rowoff = E.cy - E.screenrows + 1;

  if ( E.rx < E.coloff )
    E.coloff = E.rx;

  if ( E.rx >= E.coloff + E.screencols )
    E.coloff = E.rx - E.screencols + 1;
  editorClampViewport();
}

void editorClampViewport( void ) {
  int max_rowoff, max_coloff, rowlen;

  max_rowoff = E.numrows - E.screenrows;
  if ( max_rowoff < 0 )
    max_rowoff = 0;
  if ( E.rowoff < 0 )
    E.rowoff = 0;
  if ( E.rowoff > max_rowoff )
    E.rowoff = max_rowoff;

  rowlen = E.cy < E.numrows ? E.row[E.cy].rsize : 0;
  max_coloff = rowlen - E.screencols;
  if ( max_coloff < 0 )
    max_coloff = 0;
  if ( E.coloff < 0 )
    E.coloff = 0;
  if ( E.coloff > max_coloff )
    E.coloff = max_coloff;
}

void editorScrollRows( int amount ) {
  E.rowoff += amount;
  editorClampViewport();
}

int editorHandleMouse( struct mouseEvent *event ) {
  int cy, cx, rx;

  if ( event == NULL || event->row > E.screenrows )
    return 0;
  if ( event->button == 0 && !event->released && !event->motion ) {
    if ( !editorMouseToCursor( event, &cy, &cx, &rx ) )
      return 0;
    editorStartSelection( cy, cx, rx );
    return 1;
  }
  if ( event->button == 0 && event->motion && S.dragging ) {
    if ( !editorMouseToCursor( event, &cy, &cx, &rx ) )
      return 0;
    editorUpdateSelection( cy, cx, rx );
    return 1;
  }
  if ( event->button == 0 && event->released && S.dragging ) {
    S.dragging = 0;
    return 1;
  }
  if ( event->button == 0 && !event->released )
    return editorHandleMouseClick( event );
  if ( event->button == 64 )
    editorScrollRows( -C.scroll_rows );
  else if ( event->button == 65 )
    editorScrollRows( C.scroll_rows );
  else
    return 0;
  return 1;
}

void editorStartSelection( int cy, int cx, int rx ) {
  S.active = 1;
  S.dragging = 1;
  S.anchor_cy = S.end_cy = cy;
  S.anchor_cx = S.end_cx = cx;
  S.anchor_ry = S.end_ry = cy;
  S.anchor_rx = S.end_rx = rx;
  E.cy = cy;
  E.cx = cx;
}

void editorUpdateSelection( int cy, int cx, int rx ) {
  S.end_cy = cy;
  S.end_cx = cx;
  S.end_ry = cy;
  S.end_rx = rx;
  E.cy = cy;
  E.cx = cx;
}

int editorMouseToCursor( struct mouseEvent *event, int *cy, int *cx, int *rx ) {
  int filerow, render_col, rowlen;

  if ( event == NULL || event->row > E.screenrows || event->col < 1 )
    return 0;
  filerow = E.rowoff + event->row - 1;
  if ( filerow < 0 || filerow >= E.numrows )
    return 0;
  render_col = E.coloff + event->col - 1;
  rowlen = E.row[filerow].rsize;
  if ( render_col > rowlen )
    render_col = rowlen;
  *cy = filerow;
  *rx = render_col;
  *cx = editorRowRxToCx( &E.row[filerow], render_col );
  return 1;
}

int editorHandleMouseClick( struct mouseEvent *event ) {
  int cy, cx, rx;

  if ( event == NULL || event->button != 0 || event->released ||
      event->row > E.screenrows || event->col < 1 )
    return 0;
  if ( !editorMouseToCursor( event, &cy, &cx, &rx ) )
    return 0;
  E.cy = cy;
  E.cx = cx;
  return 1;
}

int editorIsSelected( int filerow, int render_col ) {
  int start_row, end_row, start_col, end_col;

  if ( !S.active )
    return 0;
  start_row = S.anchor_ry;
  end_row = S.end_ry;
  start_col = S.anchor_rx;
  end_col = S.end_rx;
  if ( start_row > end_row ||
      (start_row == end_row && start_col > end_col) ) {
    start_row = S.end_ry;
    end_row = S.anchor_ry;
    start_col = S.end_rx;
    end_col = S.anchor_rx;
  }
  if ( filerow < start_row || filerow > end_row )
    return 0;
  if ( start_row == end_row )
    return render_col >= start_col && render_col < end_col;
  if ( filerow == start_row )
    return render_col >= start_col;
  if ( filerow == end_row )
    return render_col < end_col;
  return 1;
}

void editorDrawRows( struct abuf *ab ) {
  int y;
  for ( y = 0; y < E.screenrows; y++ ) {
    int filerow = y + E.rowoff;
    if ( filerow >= E.numrows ) {
      if ( E.numrows == 0 && y == E.screenrows / 3 ) {

        char welcome[80];
        int welcomelen = snprintf( welcome, sizeof( welcome ),
            "Zinc v%s", ZINC_VERSION );
        if ( welcomelen > E.screencols )
          welcomelen = E.screencols;

        int padding = (E.screencols - welcomelen) / 2;
        if ( padding ) {
          abAppend( ab, "~", 1 );
          padding--;
        }

        while ( padding-- )
          abAppend( ab, " ", 1 );
      
        abAppend( ab, welcome, welcomelen );

      } else 
        abAppend( ab, "~", 1 );
    } else {
      int len = E.row[filerow].size - E.coloff;
      if ( len < 0 )
        len = 0;
      if ( len > E.screencols )
        len = E.screencols;
          
      char *c = &E.row[filerow].render[E.coloff];
      unsigned char *hl = &E.row[filerow].hl[E.coloff];
      int current_color = -1, j;
      for ( j = 0; j < len; j++ ) {
        if ( editorIsSelected( filerow, E.coloff + j ) ) {
          abAppend( ab, "\x1b[7m", 4 );
          abAppend( ab, &c[j], 1 );
          abAppend( ab, "\x1b[27m", 5 );
        } else if ( iscntrl( c[j] ) ) {
          char sym = (c[j] <= 26) ? '@' + c[j] : '?';
          abAppend( ab, "\x1b[7m", 4 );
          abAppend( ab, &sym, 1 );
          abAppend( ab, "\x1b[m", 3 );
          if ( current_color != -1 ) {
            char buf[16];
            int clen = snprintf( buf, sizeof( buf ), "\x1b[%dm", current_color );
            abAppend( ab, buf, clen );
          }
        } else if ( hl[j] == HL_NORMAL ) {
          if ( current_color != -1 ) {
            abAppend( ab, "\x1b[39m", 5 );
            current_color = -1;
          }
          abAppend( ab, &c[j], 1 );
        } else {
          int color = editorSyntaxToColor( hl[j] );
          if ( color != current_color ) {
            current_color = color;
            char buf[16];
            int clen = snprintf( buf, sizeof( buf ), "\x1b[%dm", color );
            abAppend( ab, buf, clen );
          }
          abAppend( ab, &c[j], 1 );
        }
      }
      abAppend( ab, "\x1b[39m", 5 );
    }   

    abAppend( ab, "\x1b[K", 3 );
    abAppend( ab, "\r\n", 2 );
  }
}

void editorDrawStatusBar( struct abuf *ab ) {
  abAppend( ab, "\x1b[7m", 4 );
  char status[80], rstatus[80];
  int len = snprintf( status, sizeof( status ), "%.20s - %d lines %s",
      E.filename ? E.filename : "[No Name]", E.numrows,
      E.dirty ? "(modified)" : "" );
  int rlen = snprintf( rstatus, sizeof( rstatus ), "%s | %d/%d",
      E.syntax ? E.syntax->filetype : "no filetype", E.cy + 1, E.numrows );
  if ( len > E.screencols )
    len = E.screencols;
  abAppend( ab, status, len );
  while ( len < E.screencols ) {
    if ( E.screencols - len == rlen ) {
      abAppend( ab, rstatus, rlen );
      break;
    }
    abAppend( ab, " ", 1 );
    len++;
  }
  abAppend( ab, "\x1b[m", 3 );
  abAppend( ab, "\r\n", 2 );
}

void editorDrawMessageBar( struct abuf *ab ) {
  abAppend( ab, "\x1b[K", 3 );
  int msglen = strlen( E.statusmsg );
  if ( msglen > E.screencols )
    msglen = E.screencols;
  if ( msglen && time( NULL ) - E.statusmsg_time < 5 ) {
    if ( E.statusmsg_warning )
      abAppend( ab, "\x1b[1;33m", 7 );
    abAppend( ab, E.statusmsg, msglen );
    if ( E.statusmsg_warning )
      abAppend( ab, "\x1b[0m", 4 );
  }
}

void editorRefreshScreen() {
  editorScroll();

  struct abuf ab = ABUF_INIT;

  abAppend( &ab, "\x1b[?25l", 6 );
  abAppend( &ab, "\x1b[H", 3 );

  editorDrawRows( &ab );
  editorDrawStatusBar( &ab );
  editorDrawMessageBar( &ab );
  
  char buf[32];
  snprintf( buf, sizeof( buf ), "\x1b[%d;%dH", 
            (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1 );
  abAppend( &ab, buf, strlen( buf ) );

  abAppend( &ab, "\x1b[?25h", 6 );

  terminalWrite( ab.b, ab.len );
  abFree( &ab );
}

void editorSetStatusMessage( const char *fmt, ... ) {
  va_list ap;
  va_start( ap, fmt );
  vsnprintf( E.statusmsg, sizeof( E.statusmsg ), fmt, ap );
  va_end( ap );
  E.statusmsg_time = time( NULL );
  E.statusmsg_warning = 0;
}

/*   INPUT   */
char *editorPrompt( char *prompt, void (*callback)(char *, int) ) {
  size_t bufsize = 128;
  char *buf = malloc( bufsize );
  size_t buflen = 0;
  buf[0] = '\0';

  while ( 1 ) {
    editorSetStatusMessage( prompt, buf );
    editorRefreshScreen();

    int c = editorReadKey();
    if ( c == DEL_KEY || c == CTRL_KEY( 'h' ) || c == BACKSPACE ) {
      if ( buflen != 0)
        buf[--buflen] = '\0';
    } else if ( c == '\x1b' ) {
      editorSetStatusMessage( "" );
      if ( callback )
        callback( buf, c );
      free( buf );
      return NULL;
    } else if ( c == '\r' ) {
      if ( buflen != 0 ) {
        editorSetStatusMessage( "" );
        if ( callback )
          callback( buf, c );
        return buf;
      }
    } else if ( !iscntrl( c ) && c < 128 ) {
      if ( buflen == bufsize - 1 ) {
        bufsize *= 2;
        buf = realloc( buf, bufsize );
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }
    if ( callback )
      callback( buf, c );
  }
}

void editorMoveCursor( int key ) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch ( key ) {
    case ARROW_LEFT:
      if ( E.cx != 0 )
        E.cx--;
      else if ( E.cy > 0 ) {
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      break;

    case ARROW_RIGHT:
      if ( row && E.cx < row->size )
        E.cx++;
      else if ( row && E.cx == row->size ) {
        E.cy++;
        E.cx = 0;
      }
      break;

    case ARROW_UP:
      if ( E.cy != 0 )
        E.cy--;
      break;

    case ARROW_DOWN:
      if ( E.cy < E.numrows )
        E.cy++;
      break;
  }

  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if ( E.cx > rowlen )
    E.cx = rowlen;
}

void editorProcessKeypress() {
  static int quit_times = ZINC_QUIT_TIMES;

  int c = editorReadKey();

  switch ( c ) {
    case '\r':
      editorInsertNewline();
      break;

    case CTRL_KEY( 'q' ):
      if ( E.dirty && quit_times > 0 ) {
        editorSetStatusMessage(
          "WARNING: File has unsaved changes. Press Ctrl-Q again to quit." );
        E.statusmsg_warning = 1;
        quit_times--;
        return;
      }
      terminalWrite( "\x1b[2J", 4 );
      terminalWrite( "\x1b[H", 3 );

      exit( 0 );
      break;

    case CTRL_KEY( 's' ):
      editorSave();
      break;

    case HOME_KEY:
      E.cx = 0;
      break;

    case END_KEY:
      if ( E.cy < E.numrows )
        E.cx = E.screencols - 1;
      break;

    case CTRL_KEY( 'f' ):
      editorFind();
      break;

    case BACKSPACE:
    case CTRL_KEY( 'h' ):
    case DEL_KEY:
      if ( c == DEL_KEY )
        editorMoveCursor( ARROW_RIGHT );
      editorDelChar();

      break;

    case PAGE_UP:
    case PAGE_DOWN:
      {
        if ( c == PAGE_UP )
          E.cy = E.rowoff;
        else if ( c == PAGE_DOWN ) {
          E.cy = E.rowoff + E.screenrows - 1;
          if ( E.cy > E.numrows )
            E.cy = E.numrows;
        }

        int times = E.screenrows;
        while ( times-- )
          editorMoveCursor( c == PAGE_UP ? ARROW_UP : ARROW_DOWN );
      }
      break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor( c );
      break;

    case MOUSE_EVENT:
      editorHandleMouse( &last_mouse_event );
      break;

    case CTRL_KEY( 'l' ):
      editorToggleMouse();
      break;

    case '\x1b':
      break;

    default:
      editorInsertChar( c );
      break;
  }
  quit_times = ZINC_QUIT_TIMES;
}

/*   INIT   */
void initEditor() {
  E.cx = E.cy = E.numrows = E.rowoff = E.coloff = 
    E.rx = E.dirty = 0;
  E.statusmsg_time = 0;
  E.statusmsg_warning = 0;
  E.row = NULL;
  E.filename = NULL;
  E.syntax = NULL;
  E.statusmsg[0] = '\0';
  signal( SIGWINCH, terminalHandleResize );

  if ( terminalGetSize( &E.screenrows, &E.screencols ) == -1 )
    die( "getWindowSize" );

  E.screenrows -= 2;
}

int main( int argc, char *argv[] ) {
  initConfigOptions();
  terminalDetectCapabilities();
  enableRawMode();
  initEditor();
  if ( argc >= 2 )
    editorOpen( argv[1] );

  editorSetStatusMessage( "HELP: Ctrl-Q = quit | Ctrl-S = save | Ctrl-F = find" );
  
  while ( 1 ) {
    editorHandleResize();
    editorRefreshScreen();
    editorProcessKeypress();    
  }

  return 0;
}
