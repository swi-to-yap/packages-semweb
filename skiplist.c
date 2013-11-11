/*  Part of SWI-Prolog

    Author:        Jan Wielemaker
    E-mail:        J.Wielemaker@cs.vu.nl
    WWW:           http://www.swi-prolog.org
    Copyright (C): 2011-2012, VU University Amsterdam

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

    As a special exception, if you link this library with other files,
    compiled with a Free Software compiler, to produce an executable, this
    library does not by itself cause the resulting executable to be covered
    by the GNU General Public License. This exception does not however
    invalidate any other reasons why the executable file might be covered by
    the GNU General Public License.
*/

#include <config.h>
#include <SWI-Stream.h>
#include <SWI-Prolog.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "skiplist.h"

static int debuglevel;

#define DEBUG(n, g) do { if ( debuglevel > n ) { g; } } while(0)

int
skiplist_debug(int new)
{ int old = debuglevel;
  debuglevel = new;
  return old;
}


#ifndef offsetof
#define offsetof(structure, field) ((size_t) &(((structure *)NULL)->field))
#endif

#define subPointer(p,n) (void*)((char*)(p)-(n))
#define addPointer(p,n) (void*)((char*)(p)+(n))

#define SIZEOF_SKIP_CELL_NOPLAYLOAD(n) \
	(offsetof(skipcell, next[n]))
#define SIZEOF_SKIP_CELL(sl, n) \
	((sl)->payload_size + SIZEOF_SKIP_CELL_NOPLAYLOAD(n))

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
On some systems, RAND_MAX is small. We   assume that the C compiler will
remove the conditional if RAND_MAX is a sufficiently large constant
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifndef HAVE_RANDOM
#define random rand
#endif

static int
cell_height(void)
{ long r;
  int h = 1;

  r  = random();
  if ( RAND_MAX < (1UL<<SKIPCELL_MAX_HEIGHT)-1 )
    r ^= random()<<15;
  r &= ((1UL<<SKIPCELL_MAX_HEIGHT)-1);

  while(r&0x1)
  { h++;
    r >>= 1;
  }

  return h;
}


static void *
sl_malloc(size_t bytes, void *client_data)
{ return malloc(bytes);
}


void
skiplist_init(skiplist *sl, size_t payload_size,
	      void *client_data,
	      int  (*compare)(void *p1, void *p2, void *cd),
	      void*(*alloc)(size_t bytes, void *cd),
	      void (*destroy)(void *p, void *cd))
{ memset(sl, 0, sizeof(*sl));

  if ( !alloc )
    alloc = sl_malloc;

  sl->client_data  = client_data;
  sl->payload_size = payload_size;
  sl->compare      = compare;
  sl->alloc        = alloc;
  sl->destroy      = destroy;
  sl->height       = 1;
  sl->count        = 0;
}


void
skiplist_destroy(skiplist *sl)
{ int h=0;
  void **scp;

  scp = (void**)sl->next[h];
  while(scp)
  { void **next = (void**)*scp;
    skipcell *sc = subPointer(scp, SIZEOF_SKIP_CELL_NOPLAYLOAD(h));
    void *cell_payload = subPointer(sc, sl->payload_size);

    if ( sl->destroy )
      (*sl->destroy)(cell_payload, sl->client_data);

    scp = next;
  }
}


skipcell *
new_skipcell(skiplist *sl, void *payload)
{ int h = cell_height();
  char *p = (*sl->alloc)(SIZEOF_SKIP_CELL(sl, h), sl->client_data);

  if ( p )
  { skipcell *sc = (skipcell*)(p+sl->payload_size);

    DEBUG(1, Sdprintf("Allocated payload at %p; cell at %p\n", p, sc));

    memcpy(p, payload, sl->payload_size);
    sc->height = h;
    sc->erased = FALSE;
    sc->magic = SKIPCELL_MAGIC;
    memset(sc->next, 0, sizeof(*sc->next)*h);

    return sc;
  }

  return NULL;
}


void *
skiplist_find(skiplist *sl, void *payload)
{ int h = sl->height-1;			/* h>=1 */
  void **scp, **scpp;

  scp = &sl->next[h];
  scpp = NULL;

  while(h>=0)
  { void *nscp;

    if ( scpp )
    { skipcell *sc = subPointer(scp, SIZEOF_SKIP_CELL_NOPLAYLOAD(h));
      void *cell_payload = subPointer(sc, sl->payload_size);
      int diff = (*sl->compare)(payload, cell_payload, sl->client_data);

      assert(sc->magic == SKIPCELL_MAGIC);

      if ( diff == 0 )
      { if ( !sc->erased )
	  return cell_payload;
	else
	  return NULL;
      } else if ( diff < 0 )		/* cell payload > target */
      { do
	{ scpp--;
	  scp = (void**)*scpp;
	  h--;
	} while(scp == NULL && h>=0 );

	continue;
      }
    }

    if ( (nscp = *scp) )
    { scpp = scp;
      scp  = (void**)nscp;
    } else
    { if ( scpp )
	scpp--;
      scp--;
      h--;
    }
  }

  return NULL;
}

/* Find first cell with key >= payload.  Returns first cell if payload == NULL
*/

void *
skiplist_find_first(skiplist *sl, void *payload, skiplist_enum *en)
{ void **scp;
  int h;

  en->list = sl;

  if ( payload )
  { void **scpp, *nscp;

    h    = sl->height-1;			/* h>=1 */
    scp  = &sl->next[h];
    scpp = NULL;

    while(h>=0)
    { if ( scpp )
      { skipcell *sc = subPointer(scp, SIZEOF_SKIP_CELL_NOPLAYLOAD(h));
	void *cell_payload = subPointer(sc, sl->payload_size);
	int diff = (*sl->compare)(payload, cell_payload, sl->client_data);

	assert(sc->magic == SKIPCELL_MAGIC);

	if ( diff == 0 )
	{ goto found;
	} else if ( diff < 0 )		/* cell payload > target */
	{ if ( h == 0 )
	    goto found;

	  do
	  { scpp--;
	    scp = (void**)*scpp;
	    h--;
	  } while(scp == NULL && h>=0 );

	  continue;
	}
      }

      if ( (nscp = *scp) )
      { scpp = scp;
	scp  = (void**)nscp;
      } else
      { if ( scpp )
	  scpp--;
	scp--;
	h--;
      }
    }

    return NULL;
  } else
  { scp = (void**)sl->next[0];
    h = 0;

    if ( scp )
    { skipcell *sc;

    found:
      sc = subPointer(scp, SIZEOF_SKIP_CELL_NOPLAYLOAD(h));
      assert(sc->magic == SKIPCELL_MAGIC);

      if ( (scp = sc->next[0]) )
      { en->current = subPointer(scp, SIZEOF_SKIP_CELL_NOPLAYLOAD(0));
      } else
      { en->current = NULL;
      }

      if ( !sc->erased )
      { void *cell_payload = subPointer(sc, sl->payload_size);
	return cell_payload;
      } else
      { return skiplist_find_next(en);
      }
    }

    return NULL;
  }
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
skiplist_find_next() returns the payload of the  next cell. Note that if
cells are deleted, they may not  be discarded. I.e., this implementation
depends on Boehm-GC to avoid this.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

void *
skiplist_find_next(skiplist_enum *en)
{ skipcell *sc;
  void **scp;
  void *cell_payload;

  do
  { if ( !(sc = en->current) )
      return NULL;

    if ( (scp = sc->next[0]) )
    { en->current = subPointer(scp, SIZEOF_SKIP_CELL_NOPLAYLOAD(0));
    } else
    { en->current = NULL;
    }
  } while( sc->erased );

  cell_payload = subPointer(sc, en->list->payload_size);

  return cell_payload;
}


void
skiplist_find_destroy(skiplist_enum *en)
{
}


int
skiplist_check(skiplist *sl, int print)
{ int h;

  for(h = SKIPCELL_MAX_HEIGHT-1; h>=0; h--)
  { void **scp  = (void*)sl->next[h];
    void **scpp = NULL;
    int count = 0;

    while(scp)
    { skipcell *sc = subPointer(scp, SIZEOF_SKIP_CELL_NOPLAYLOAD(h));
      assert(sc->magic == SKIPCELL_MAGIC);

      count++;

      if ( h == 0 )
      { int i;

	for(i=1; i<sc->height; i++)
	{ if ( sc->next[i] )
	  { skipcell *next0 = subPointer(sc->next[i-1],
					 SIZEOF_SKIP_CELL_NOPLAYLOAD(i-1));
	    skipcell *next1 = subPointer(sc->next[i],
					 SIZEOF_SKIP_CELL_NOPLAYLOAD(i));
	    void *p0, *p1;
	    assert(next0->magic == SKIPCELL_MAGIC);
	    assert(next1->magic == SKIPCELL_MAGIC);
	    p0 = subPointer(next0, sl->payload_size);
	    p1 = subPointer(next1, sl->payload_size);

	    assert((*sl->compare)(p0, p1, sl->client_data) <= 0);
	  }
	}
      }

      if ( scpp )
      { void *pl1, *pl2;

	skipcell *prev = subPointer(scpp, SIZEOF_SKIP_CELL_NOPLAYLOAD(h));
	assert(prev->magic == SKIPCELL_MAGIC);
	pl1 = subPointer(prev, sl->payload_size);
	pl2 = subPointer(sc,   sl->payload_size);
	assert((*sl->compare)(pl1, pl2, sl->client_data) < 0);
      }

      scpp = scp;
      scp = (void**)*scp;
    }

    if ( print )
      Sdprintf("%-4d: %-4d\n", h, count);
  }

  return TRUE;
}


void *
skiplist_insert(skiplist *sl, void *payload, int *is_new)
{ void *rc;

  if ( !(rc=skiplist_find(sl, payload)) )
  { skipcell *new = new_skipcell(sl, payload);
    int h;
    void **scp, **scpp;

    if ( new->height > sl->height )
      sl->height = new->height;

    h    = sl->height-1;
    scp  = &sl->next[h];
    scpp = NULL;

    DEBUG(2, Sdprintf("Inserting new cell %p of height %d\n", new, new->height));

    while(h>=0)
    { if ( scpp )
      { skipcell *sc = subPointer(scp, SIZEOF_SKIP_CELL_NOPLAYLOAD(h));
	void *cell_payload = subPointer(sc, sl->payload_size);
	int diff = (*sl->compare)(payload, cell_payload, sl->client_data);

	assert(sc->magic == SKIPCELL_MAGIC);
	DEBUG(2, Sdprintf("Cell payload at %p\n", cell_payload));

	assert(diff != 0);

	if ( diff < 0 )			/* cell payload > target */
	{ if ( h < new->height )
	  { DEBUG(3, Sdprintf("Between %p and %p at height = %d\n",
			      scpp, scp, h));
	    new->next[h] = scp;
	    *scpp = &new->next[h];
	    scpp--;
	    scp = (void**)*scpp;
	    h--;
	  } else			/* new one is too low */
	  { scpp--;
	    scp = (void**)*scpp;
	    h--;
	  }
	  continue;
	}
      }

      if ( *scp == NULL )
      { if ( new->height > h )
	  *scp = &new->next[h];
	if ( scpp )
	  scpp--;
	scp--;
	h--;
	continue;
      }

      scpp = scp;
      scp  = (void**)*scp;
    }
    sl->count++;

    DEBUG(1, skiplist_check(sl, FALSE));
    if ( is_new )
      *is_new = TRUE;

    return subPointer(new, sl->payload_size);
  }

  if ( is_new )
    *is_new = FALSE;

  return rc;
}


void *
skiplist_delete(skiplist *sl, void *payload)
{ int h = sl->height-1;
  void **scp, **scpp;

  if ( h < 0 )
    return NULL;			/* empty */

  scp = &sl->next[h];
  scpp = NULL;

  while(h>=0)
  { if ( scpp )
    { skipcell *sc = subPointer(scp, SIZEOF_SKIP_CELL_NOPLAYLOAD(h));
      void *cell_payload = subPointer(sc, sl->payload_size);
      int diff = (*sl->compare)(payload, cell_payload, sl->client_data);

      assert(sc->magic == SKIPCELL_MAGIC);

      if ( diff == 0 )
      { sc->erased = TRUE;

	*scpp = (void**)*scp;
	if ( h > 0 )
	{ scpp--;
	  scp = (void**)*scpp;
	  h--;
	  continue;
	} else
	{ sl->count--;

	  return cell_payload;
	}
      } else if ( diff < 0 )		/* cell payload > target */
      { scpp--;
	scp = (void**)*scpp;
	h--;
	continue;
      }
    }

    if ( *scp == NULL )
    { if ( scpp )
	scpp--;
      scp--;
      h--;
      continue;
    }

    scpp = scp;
    scp  = (void**)*scp;
  }

  return NULL;
}


/* skiplist_erased_payload() returns whether or not the cell associated
   with payload is erased or not. This may only be used on payloads
   returned from one of the skiplist calls.
*/

int
skiplist_erased_payload(skiplist *sl, void *payload)
{ skipcell *sc = addPointer(payload, sl->payload_size);

  return sc->erased;
}
