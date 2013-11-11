/*  Part of SWI-Prolog

    Author:        Jan Wielemaker
    E-mail:        J.Wielemaker@cs.vu.nl
    WWW:           http://www.swi-prolog.org
    Copyright (C): 2013, VU University Amsterdam

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
    MA 02110-1301 USA
*/

#include <SWI-Stream.h>
#include <SWI-Prolog.h>
#include <string.h>
#include "turtle_chars.c"

static atom_t ATOM_end_of_file;

static functor_t FUNCTOR_node1;
static functor_t FUNCTOR_literal1;
static functor_t FUNCTOR_type2;
static functor_t FUNCTOR_lang2;
static functor_t FUNCTOR_triple3;
static functor_t FUNCTOR_error2;
static functor_t FUNCTOR_syntax_error1;
static functor_t FUNCTOR_stream4;


		 /*******************************
		 *	CHARACTER CLASSES	*
		 *******************************/

#define WS	0x01			/* white space */
#define EL	0x02			/* end-of-line */
#define DI	0x04			/* digit */
#define LC	0x08			/* digit */
#define UC	0x10			/* digit */
#define IV	0x20			/* invalid */
#define EF	0x80			/* END-OF-FILE */

static const char char_type0[] =
{/*0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F  */
  EF,
   0,  0,  0,  0,  0,  0,  0,  0,  0, WS, EL,  0,  0, EL,  0,  0, /* 00-0f */
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 10-1F */
  WS,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 20-2F */
  DI, DI, DI, DI, DI, DI, DI, DI, DI, DI,  0,  0,  0,  0,  0,  0, /* 30-3F */
   0, UC, UC, UC, UC, UC, UC, UC, UC, UC, UC, UC, UC, UC, UC, UC, /* 40-4F */
  UC, UC, UC, UC, UC, UC, UC, UC, UC, UC, UC,  0,  0,  0,  0,  0, /* 50-5F */
   0, LC, LC, LC, LC, LC, LC, LC, LC, LC, LC, LC, LC, LC, LC, LC, /* 60-6F */
  LC, LC, LC, LC, LC, LC, LC, LC, LC, LC, LC,  0,  0,  0,  0,  0  /* 70-7F */
};

static const char* char_type = &char_type0[1];

static inline int
is_ws(int c)
{ return c < 128 && char_type[c] == WS;
}

static inline int
is_eol(int c)
{ return c < 128 && char_type[c] == EL;
}

static inline int
is_digit(int c)
{ return c >= '0' && c <= '9';
}

static inline int
is_ascii_letter(int c)
{ return c < 128 && ((char_type[c] & (LC|UC)) != 0);
}

static int
is_pn_chars_u(int c)
{ return wcis_pn_chars_base(c) || c == '_' || c == ':';
}

static int
is_pn_chars(int c)
{ return ( wcis_pn_chars_base(c) ||
	   is_digit(c) ||
	   c == '-' ||
	   wcis_pn_chars_extra(c)
	 );
}


static const char hexval0[] =
{/*0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F  */
  -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 00-0f */
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 10-1F */
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 20-2F */
   0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1, /* 30-3F */
  -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 40-4F */
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 50-5F */
  -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1  /* 60-6F */
};

static const char* hexval = &hexval0[1];

static inline int
hexd(int c)
{ return (c <= 'f' ? hexval[c] : -1);
}


		 /*******************************
		 *	      ERROR		*
		 *******************************/

static int
syntax_error(IOSTREAM *in, const char *msg)
{ term_t ex = PL_new_term_refs(2);
  IOPOS *pos;

  if ( !PL_unify_term(ex+0, PL_FUNCTOR, FUNCTOR_syntax_error1,
		              PL_CHARS, msg) )
    return FALSE;

  if ( (pos=in->position) )
  { term_t stream;

    if ( !(stream = PL_new_term_ref()) ||
	 !PL_unify_stream(stream, in) ||
	 !PL_unify_term(ex+1,
			PL_FUNCTOR, FUNCTOR_stream4,
			  PL_TERM, stream,
			  PL_INT, (int)pos->lineno,
			  PL_INT, (int)(pos->linepos-1), /* one too late */
			  PL_INT64, (int64_t)(pos->charno-1)) )
      return FALSE;
  }

  if ( PL_cons_functor_v(ex, FUNCTOR_error2, ex) )
  { int c;

    do
    { c = Sgetcode(in);
    } while(c != '\n' && c != -1);

    return PL_raise_exception(ex);
  }

  return FALSE;
}


		 /*******************************
		 *	       BUFFER		*
		 *******************************/

#ifdef STRING_BUF_DEBUG
#define FAST_BUF_SIZE 1
#else
#define FAST_BUF_SIZE 512
#endif

typedef struct string_buffer
{ wchar_t fast[FAST_BUF_SIZE];
  wchar_t *buf;
  wchar_t *in;
  wchar_t *end;
#ifdef STRING_BUF_DEBUG
  int	   discarded;
#endif
} string_buffer;


static int
growBuffer(string_buffer *b, int c)
{ if ( b->buf == b->fast )
  { wchar_t *new = malloc((FAST_BUF_SIZE*2)*sizeof(wchar_t));

    if ( new )
    { memcpy(new, b->fast, sizeof(b->fast));
      b->buf = new;
      b->in  = b->buf+FAST_BUF_SIZE;
      b->end = b->in+FAST_BUF_SIZE;
      *b->in++ = c;

      return TRUE;
    }
  } else
  { size_t sz = b->end - b->buf;
    wchar_t *new = realloc(b->buf, sz*sizeof(wchar_t)*2);

    if ( new )
    { b->buf = new;
      b->in  = new+sz;
      b->end = b->in+sz;
      *b->in++ = c;

      return TRUE;
    }
  }

  PL_resource_error("memory");
  return FALSE;
}


static inline void
initBuf(string_buffer *b)
{ b->buf = b->fast;
  b->in  = b->buf;
  b->end = &b->fast[FAST_BUF_SIZE];
#ifdef STRING_BUF_DEBUG
  b->discarded = FALSE;
#endif
}


static inline void
discardBuf(string_buffer *b)
{
#ifdef STRING_BUF_DEBUG
  assert(b->discarded == FALSE);
  b->discarded = TRUE;
#endif
  if ( b->buf != b->fast )
    free(b->buf);
}


static inline int
addBuf(string_buffer *b, int c)
{ if ( b->in < b->end )
  { *b->in++ = c;
    return TRUE;
  }

  return growBuffer(b, c);
}


static inline int
bufSize(string_buffer *b)
{ return b->in - b->buf;
}

#define baseBuf(b)   ( (b)->buf )


		 /*******************************
		 *	     SKIPPING		*
		 *******************************/

static int
skip_ws(IOSTREAM *in, int *cp)
{ int c = *cp;

  while(is_ws(c))
    c = Sgetcode(in);

  *cp = c;

  return !Sferror(in);
}


static int
skip_comment_line(IOSTREAM *in, int *cp)
{ int c;

  do
  { c = Sgetcode(in);
  } while ( c != -1 && !is_eol(c) );

  while(is_eol(c))
    c = Sgetcode(in);

  *cp = c;

  return !Sferror(in);
}


static int
skip_eol(IOSTREAM *in, int *cp)
{ if ( skip_ws(in, cp) )
  { int c = *cp;

    if ( c == '\n' )
      return TRUE;
    if ( c == '\r' )
    { if ( Speekcode(in) == '\n' )
	(void)Sgetcode(in);
      return TRUE;
    }
    if ( c == EOF )
      return TRUE;
    if ( c == '#' )
      return skip_comment_line(in, cp);

    return syntax_error(in, "end-of-line expected");
  } else
  { return FALSE;
  }
}


		 /*******************************
		 *	      READING		*
		 *******************************/

#define ESCAPED_CODE (-1)

static int
read_hex(IOSTREAM *in, int *cp, int len)
{ int c = 0;

  while(len-- > 0)
  { int v0;
    int c2 = Sgetcode(in);

    if ( (v0 = hexd(c2)) >= 0 )
    { c <<= 4;
      c += v0;
    } else
    { return syntax_error(in, "illegal unicode escape");
    }
  }

  *cp = c;
  return ESCAPED_CODE;
}


static int
get_iri_code(IOSTREAM *in, int *cp)
{ int c = Sgetcode(in);

  switch(c)
  { case '\r':
    case '\n':
      return syntax_error(in, "newline in uriref");
    case EOF:
      return syntax_error(in, "EOF in uriref");
    case '<':
    case '"':
    case '{':
    case '}':
    case '|':
    case '^':
    case '`':
      return syntax_error(in, "Illegal character in uriref");
    case '\\':
    { int c2 = Sgetcode(in);

      switch(c2)
      { case 'u':	return read_hex(in, cp, 4);
	case 'U':	return read_hex(in, cp, 8);
	default:	return syntax_error(in, "illegal escape");
      }
    }
    default:
    { if ( c > ' ' )
      { *cp = c;
	return TRUE;
      }
      return syntax_error(in, "Illegal control character in uriref");
    }
  }
}

static int
read_uniref(IOSTREAM *in, term_t subject, int *cp)
{ int c;
  string_buffer buf;

  initBuf(&buf);
  for(;;)
  { int rc;

    if ( (rc=get_iri_code(in, &c)) == TRUE )
    { switch(c)
      { case '>':
	{ int rc = PL_unify_wchars(subject, PL_ATOM,
				   bufSize(&buf), baseBuf(&buf));
	  discardBuf(&buf);
	  *cp = Sgetcode(in);
	  return rc;
	}
	default:
	  if ( !addBuf(&buf, c) )
	  { discardBuf(&buf);
	    return FALSE;
	  }
      }
    } else if ( rc == ESCAPED_CODE )
    { if ( !addBuf(&buf, c) )
      { discardBuf(&buf);
	return FALSE;
      }
    } else
    { discardBuf(&buf);
      return FALSE;
    }
  }
}


static int
read_node_id(IOSTREAM *in, term_t subject, int *cp)
{ int c;

  c = Sgetcode(in);
  if ( c != ':' )
    return syntax_error(in, "invalid nodeID");

  c = Sgetcode(in);
  if ( is_pn_chars_u(c) || is_digit(c) )
  { string_buffer buf;

    initBuf(&buf);
    addBuf(&buf, c);
    for(;;)
    { int c2;

      c = Sgetcode(in);

      if ( is_pn_chars(c) )
      { addBuf(&buf, c);
      } else if ( c == '.' && (is_pn_chars((c2=Speekcode(in))) || c2 == '.') )
      { addBuf(&buf, c);
      } else
      { term_t av = PL_new_term_refs(1);
	int rc;

	rc = ( PL_unify_wchars(av+0, PL_ATOM, bufSize(&buf), baseBuf(&buf)) &&
	       PL_cons_functor_v(subject, FUNCTOR_node1, av)
	     );
	discardBuf(&buf);
	*cp = c;

	return rc;
      }
    }
  } else
    return syntax_error(in, "invalid nodeID");
}


static int
read_lan(IOSTREAM *in, term_t lan, int *cp)
{ int c;
  string_buffer buf;
  int rc;

  c = Sgetcode(in);
  if ( !is_ascii_letter(c) )
    return syntax_error(in, "language tag must start with a-zA-Z");

  initBuf(&buf);
  addBuf(&buf, c);
  for(;;)
  { c = Sgetcode(in);
    if ( is_ascii_letter(c) )
    { addBuf(&buf, c);
    } else
    { break;
    }
  }
  while(c=='-')
  { addBuf(&buf, c);
    c = Sgetcode(in);
    if ( !is_ascii_letter(c) )
    { discardBuf(&buf);
      return syntax_error(in, "Illegal language tag");
    }
    addBuf(&buf, c);
    for(;;)
    { c = Sgetcode(in);
      if ( is_ascii_letter(c) )
      { addBuf(&buf, c);
      } else
      { break;
      }
    }
  }

  *cp = c;
  rc = PL_unify_wchars(lan, PL_ATOM, bufSize(&buf), baseBuf(&buf));
  discardBuf(&buf);

  return rc;
}


static int
read_subject(IOSTREAM *in, term_t subject, int *cp)
{ int c = *cp;
  int rc;

  switch ( c )
  { case '<':
      rc = read_uniref(in, subject, cp);
      break;
    case '_':
      rc = read_node_id(in, subject, cp);
      break;
    default:
      return syntax_error(in, "subject expected");
  }

  if ( rc && !is_ws(*cp) )
    return syntax_error(in, "subject not followed by whitespace");

  return rc;
}


static int
read_predicate(IOSTREAM *in, term_t predicate, int *cp)
{ int c = *cp;
  int rc;

  switch ( c )
  { case '<':
      rc = read_uniref(in, predicate, cp);
      break;
    default:
      return syntax_error(in, "predicate expected");
  }

  if ( rc && !is_ws(*cp) )
    return syntax_error(in, "predicate not followed by whitespace");

  return rc;
}


static int
wrap_literal(term_t lit)
{ return PL_cons_functor_v(lit, FUNCTOR_literal1, lit);
}


static int
get_string_code(IOSTREAM *in, int *cp)
{ int c = Sgetcode(in);

  switch(c)
  { case '\r':
    case '\n':
      return syntax_error(in, "newline in string");
    case '\\':
    { int c2 = Sgetcode(in);

      switch(c2)
      { case 'b':	*cp = '\b'; return ESCAPED_CODE;
	case 't':	*cp = '\t'; return ESCAPED_CODE;
	case 'f':	*cp = '\f'; return ESCAPED_CODE;
	case 'n':	*cp = '\n'; return ESCAPED_CODE;
	case 'r':	*cp = '\r'; return ESCAPED_CODE;
	case '"':	*cp =  '"'; return ESCAPED_CODE;
	case '\\':	*cp = '\\'; return ESCAPED_CODE;
	case 'u':	return read_hex(in, cp, 4);
	case 'U':	return read_hex(in, cp, 8);
	default:	return syntax_error(in, "illegal escape");
      }
    }
    default:
      *cp = c;
      return TRUE;
  }
}


static int
read_literal(IOSTREAM *in, term_t literal, int *cp)
{ int c;
  string_buffer buf;

  initBuf(&buf);
  for(;;)
  { int rc;

    if ( (rc=get_string_code(in, &c)) == TRUE )
    { switch(c)
      { case '"':
	{ c = Sgetcode(in);

	  switch(c)
	  { case '@':
	    { term_t av = PL_new_term_refs(2);

	      if ( read_lan(in, av+0, cp) )
	      { int rc = ( PL_unify_wchars(av+1, PL_ATOM,
					   bufSize(&buf), baseBuf(&buf)) &&
			   PL_cons_functor_v(literal, FUNCTOR_lang2, av) &&
			   wrap_literal(literal)
			 );
		discardBuf(&buf);
		return rc;
	      } else
	      { discardBuf(&buf);
		return FALSE;
	      }
	    }
	    case '^':
	    { c = Sgetcode(in);

	      if ( c == '^' )
	      { term_t av = PL_new_term_refs(2);

		c = Sgetcode(in);
		if ( c == '<' )
		{ if ( read_uniref(in, av+0, cp) )
		  { int rc = ( PL_unify_wchars(av+1, PL_ATOM,
					       bufSize(&buf), baseBuf(&buf)) &&
			       PL_cons_functor_v(literal, FUNCTOR_type2, av) &&
			       wrap_literal(literal)
			     );
		    discardBuf(&buf);
		    return rc;
		  } else
		  { discardBuf(&buf);
		    return FALSE;
		  }
		} else
		{ discardBuf(&buf);
		  return syntax_error(in, "datatype uriref expected");
		}
	      } else
	      { discardBuf(&buf);
		return syntax_error(in, "^ expected");
	      }
	    }
	    default:
	    { int rc;

	      *cp = c;
	      rc = ( PL_unify_wchars(literal, PL_ATOM,
				     bufSize(&buf), baseBuf(&buf)) &&
		     wrap_literal(literal)
		   );
	      discardBuf(&buf);
	      return rc;
	    }
	  }
	}
	case EOF:
	  discardBuf(&buf);
	  return syntax_error(in, "EOF in string");
	case '\n':
	case '\r':
	  discardBuf(&buf);
	  return syntax_error(in, "newline in string");
	default:
	  if ( !addBuf(&buf, c) )
	  { discardBuf(&buf);
	    return FALSE;
	  }
      }
    } else if ( rc == ESCAPED_CODE )
    { if ( !addBuf(&buf, c) )
      { discardBuf(&buf);
	return FALSE;
      }
    } else
    { discardBuf(&buf);
      return FALSE;
    }
  }
}


static int
read_object(IOSTREAM *in, term_t object, int *cp)
{ int c = *cp;

  switch ( c )
  { case '<':
      return read_uniref(in, object, cp);
    case '_':
      return read_node_id(in, object, cp);
    case '"':
      return read_literal(in, object, cp);
    default:
      return syntax_error(in, "object expected");
  }
}


static int
check_full_stop(IOSTREAM *in, int *cp)
{ int c = *cp;

  if ( c == '.' )
  { *cp = Sgetcode(in);

    return TRUE;
  }

  return syntax_error(in, "fullstop (.) expected");
}


static foreign_t
read_ntriple(term_t from, term_t triple)
{ IOSTREAM *in;
  int rc;
  int c;

  if ( !PL_get_stream_handle(from, &in) )
    return FALSE;

  c=Sgetcode(in);
next:
  rc = skip_ws(in, &c);

  if ( rc )
  { if ( c == '#' )
    { if ( skip_comment_line(in, &c) )
	goto next;
    } else if ( c == EOF )
    { rc = PL_unify_atom(triple, ATOM_end_of_file);
    } else if ( (char_type[c]&EL) )
    { if ( skip_eol(in, &c) )
      { c=Sgetcode(in);
	goto next;
      } else
	return FALSE;
    } else
    { term_t av = PL_new_term_refs(4);

      rc = (  read_subject(in, av+1, &c) &&
	      skip_ws(in, &c) &&
	      read_predicate(in, av+2, &c) &&
	      skip_ws(in, &c) &&
	      read_object(in, av+3, &c) &&
	      skip_ws(in, &c) &&
	      check_full_stop(in, &c) &&
	      skip_eol(in, &c)
	   );

      if ( rc )
      { rc = ( PL_cons_functor_v(av+0, FUNCTOR_triple3, av+1) &&
	       PL_unify(triple, av+0)
	     );
      }
    }
  }

  return (PL_release_stream(in) && rc);
}


		 /*******************************
		 *	       INSTALL		*
		 *******************************/

#define MKFUNCTOR(n, a) \
	FUNCTOR_ ## n ## a = PL_new_functor(PL_new_atom(#n), a)

install_t
install_ntriples(void)
{ ATOM_end_of_file = PL_new_atom("end_of_file");

  MKFUNCTOR(node,         1);
  MKFUNCTOR(literal,      1);
  MKFUNCTOR(type,         2);
  MKFUNCTOR(lang,         2);
  MKFUNCTOR(triple,       3);
  MKFUNCTOR(error,        2);
  MKFUNCTOR(syntax_error, 1);
  MKFUNCTOR(stream,       4);

  PL_register_foreign("read_ntriple", 2, read_ntriple, 0);
}
