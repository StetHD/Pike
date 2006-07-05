/*
|| This file is part of Pike. For copyright information see COPYRIGHT.
|| Pike is distributed under GPL, LGPL and MPL. See the file COPYING
|| for more information.
|| $Id: pike_memory.c,v 1.146 2006/07/05 01:10:25 mast Exp $
*/

#include "global.h"
#include "pike_memory.h"
#include "pike_error.h"
#include "pike_macros.h"
#include "gc.h"
#include "fd_control.h"
#include "dmalloc.h"

#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#include <errno.h>

RCSID("$Id: pike_memory.c,v 1.146 2006/07/05 01:10:25 mast Exp $");

/* strdup() is used by several modules, so let's provide it */
#ifndef HAVE_STRDUP
char *strdup(const char *str)
{
  char *res = NULL;
  if (str) {
    int len = strlen(str)+1;

    res = xalloc(len);
    MEMCPY(res, str, len);
  }
  return(res);
}
#endif /* !HAVE_STRDUP */

ptrdiff_t pcharp_memcmp(PCHARP a, PCHARP b, int sz)
{
  return generic_quick_binary_strcmp((char *)a.ptr, sz, a.shift,
				     (char *)b.ptr, sz, b.shift);
}

long pcharp_strlen(PCHARP a)
{
  long len;
  for(len=0;INDEX_PCHARP(a,len);len++);
  return len;
}

/* NOTE: Second arg is a p_char2 to avoid warnings on some compilers. */
INLINE p_wchar1 *MEMCHR1(p_wchar1 *p, p_wchar2 c, ptrdiff_t e)
{
  while(--e >= 0) if(*(p++) == (p_wchar1)c) return p-1;
  return (p_wchar1 *)0;
}

INLINE p_wchar2 *MEMCHR2(p_wchar2 *p, p_wchar2 c, ptrdiff_t e)
{
  while(--e >= 0) if(*(p++) == (p_wchar2)c) return p-1;
  return (p_wchar2 *)0;
}

void swap(char *a, char *b, size_t size)
{
  size_t tmp;
  char tmpbuf[1024];
  while(size)
  {
    tmp = MINIMUM((size_t)sizeof(tmpbuf), size);
    MEMCPY(tmpbuf,a,tmp);
    MEMCPY(a,b,tmp);
    MEMCPY(b,tmpbuf,tmp);
    size-=tmp;
    a+=tmp;
    b+=tmp;
  }
}

void reverse(char *memory, size_t nitems, size_t size)
{

#define DOSIZE(X,Y)						\
 case X:							\
 {								\
  Y tmp;							\
  Y *start=(Y *) memory;					\
  Y *end=start+nitems-1;					\
  while(start<end){tmp=*start;*(start++)=*end;*(end--)=tmp;}	\
  break;							\
 }

#ifdef HANDLES_UNALIGNED_MEMORY_ACCESS
  switch(size)
#else
  switch( (((size_t)memory) % size) ? 0 : size)
#endif
  {
    DOSIZE(1,B1_T)
#ifdef B2_T
    DOSIZE(2,B2_T)
#endif
#ifdef B4_T
    DOSIZE(4,B4_T)
#endif
#ifdef B16_T
    DOSIZE(8,B8_T)
#endif
#ifdef B16_T
    DOSIZE(16,B8_T)
#endif
  default:
  {
    char *start = (char *) memory;
    char *end=start+(nitems-1)*size;
    while(start<end)
    {
      swap(start,end,size);
      start+=size;
      end-=size;
    }
  }
  }
}

/*
 * This function may NOT change 'order'
 * This function is hopefully fast enough...
 */
void reorder(char *memory, INT32 nitems, INT32 size,INT32 *order)
{
  INT32 e;
  char *tmp;
  if(nitems<2) return;


  tmp=xalloc(size * nitems);

#undef DOSIZE
#define DOSIZE(X,Y)				\
 case X:					\
 {						\
  Y *from=(Y *) memory;				\
  Y *to=(Y *) tmp;				\
  for(e=0;e<nitems;e++) to[e]=from[order[e]];	\
  break;					\
 }
  

#ifdef HANDLES_UNALIGNED_MEMORY_ACCESS
  switch(size)
#else
  switch( (((size_t)memory) % size) ? 0 : size )
#endif
 {
   DOSIZE(1,B1_T)
#ifdef B2_T
     DOSIZE(2,B2_T)
#endif
#ifdef B4_T
     DOSIZE(4,B4_T)
#endif
#ifdef B8_T
     DOSIZE(8,B8_T)
#endif
#ifdef B16_T
    DOSIZE(16,B16_T)
#endif

  default:
    for(e=0;e<nitems;e++) MEMCPY(tmp+e*size, memory+order[e]*size, size);
  }

  MEMCPY(memory, tmp, size * nitems);
  free(tmp);
}

size_t hashmem(const unsigned char *a_, size_t len_, size_t mlen_)
{
  size_t ret_;

  DO_HASHMEM(ret_, a_, len_, mlen_);

  return ret_;
}

size_t hashstr(const unsigned char *str, ptrdiff_t maxn)
{
  size_t ret,c;
  
  if(!(ret=str++[0]))
    return ret;
  for(; maxn>=0; maxn--)
  {
    c=str++[0];
    if(!c) break;
    ret ^= ( ret << 4 ) + c ;
    ret &= 0x7fffffff;
  }

  return ret;
}

size_t simple_hashmem(const unsigned char *str, ptrdiff_t len, ptrdiff_t maxn)
{
  size_t ret,c;
  
  ret = len*92873743;

  len = MINIMUM(maxn,len);
  for(; len>=0; len--)
  {
    c=str++[0];
    ret ^= ( ret << 4 ) + c ;
    ret &= 0x7fffffff;
  }

  return ret;
}


#ifndef PIKE_SEARCH_H
/*
 * a quick memory search function.
 * Written by Fredrik Hubinette (hubbe@lysator.liu.se)
 */
void init_memsearch(struct mem_searcher *s,
		    char *needle,
		    size_t needlelen,
		    size_t max_haystacklen)
{
  s->needle=needle;
  s->needlelen=needlelen;

  switch(needlelen)
  {
  case 0: s->method=no_search; break;
  case 1: s->method=use_memchr; break;
  case 2:
  case 3:
  case 4:
  case 5:
  case 6: s->method=memchr_and_memcmp; break;
  default:
    if(max_haystacklen <= needlelen + 64)
    {
      s->method=memchr_and_memcmp;
    }else{
      INT32 tmp, h;
      size_t hsize, e, max;
      unsigned char *q;
      struct link *ptr;

      hsize=52+(max_haystacklen >> 7)  - (needlelen >> 8);
      max  =13+(max_haystacklen >> 4)  - (needlelen >> 5);

      if(hsize > NELEM(s->set))
      {
	hsize=NELEM(s->set);
      }else{
	for(e=8;e<hsize;e+=e);
	hsize=e;
      }
    
      for(e=0;e<hsize;e++) s->set[e]=0;
      hsize--;

      if(max > needlelen) max=needlelen;
      max=(max-sizeof(INT32)+1) & -sizeof(INT32);
      if(max > MEMSEARCH_LINKS) max=MEMSEARCH_LINKS;

      ptr=& s->links[0];

      q=(unsigned char *)needle;

#if PIKE_BYTEORDER == 4321
      for(tmp=e=0;e<sizeof(INT32)-1;e++)
      {
	tmp<<=8;
	tmp|=*(q++);
      }
#endif

      for(e=0;e<max;e++)
      {
#if PIKE_BYTEORDER == 4321
	tmp<<=8;
	tmp|=*(q++);
#else
	tmp=EXTRACT_INT(q);
	q++;
#endif
	h=tmp;
	h+=h>>7;
	h+=h>>17;
	h&=hsize;

	ptr->offset=e;
	ptr->key=tmp;
	ptr->next=s->set[h];
	s->set[h]=ptr;
	ptr++;
      }
      s->hsize=hsize;
      s->max=max;
      s->method=hubbe_search;
    }
  }
}

char *memory_search(struct mem_searcher *s,
		    char *haystack,
		    size_t haystacklen)
{
  if(s->needlelen > haystacklen) return 0;

  switch(s->method)
  {
    default:
      Pike_fatal("Don't know how to search like that!\n");
  case no_search:
    return haystack;

  case use_memchr:
    return MEMCHR(haystack,s->needle[0],haystacklen);

  case memchr_and_memcmp:
    {
      char *end,c,*needle;
      size_t needlelen;
      
      needle=s->needle;
      needlelen=s->needlelen;
      
      end=haystack + haystacklen - needlelen+1;
      c=needle[0];
      needle++;
      needlelen--;
      while((haystack=MEMCHR(haystack,c,end-haystack)))
	if(!MEMCMP(++haystack,needle,needlelen))
	  return haystack-1;

      return 0;
    }

  case hubbe_search:
    {
      INT32 tmp, h;
      char *q, *end;
      register struct link *ptr;
      
      end=haystack+haystacklen;
      q=haystack + s->max - sizeof(INT32);
      q=(char *)( ((ptrdiff_t)q) & -sizeof(INT32));
      for(;q<=end-sizeof(INT32);q+=s->max)
      {
	h=tmp=*(INT32 *)q;
	
	h+=h>>7;
	h+=h>>17;
	h&=s->hsize;
	
	for(ptr=s->set[h];ptr;ptr=ptr->next)
	{
	  char *where;
	  
	  if(ptr->key != tmp) continue;
	  
	  where=q-ptr->offset;
	  if(where<haystack) continue;
	  if(where+s->needlelen>end) return 0;
	  
	  if(!MEMCMP(where,s->needle,s->needlelen))
	    return where;
	}
      }
    }
  }
  return 0;
}


void init_generic_memsearcher(struct generic_mem_searcher *s,
			      void *needle,
			      size_t needlelen,
			      char needle_shift,
			      size_t estimated_haystack,
			      char haystack_shift)
{
  s->needle_shift=needle_shift;
  s->haystack_shift=haystack_shift;

  if(needle_shift ==0 && haystack_shift ==0)
  {
    init_memsearch(& s->data.eightbit, (char *)needle, needlelen,estimated_haystack);
    return;
  }

  s->data.other.needlelen=needlelen;
  s->data.other.needle=needle;

  switch(needlelen)
  {
    case 0: s->data.other.method=no_search; break;
    case 1:
      s->data.other.method=use_memchr;
      switch(s->needle_shift)
      {
	case 0: s->data.other.first_char=*(p_wchar0 *)needle; break;
	case 1: s->data.other.first_char=*(p_wchar1 *)needle; break;
	case 2: s->data.other.first_char=*(p_wchar2 *)needle; break;
      }
      break;

    default:
      s->data.other.method=memchr_and_memcmp;
      switch(s->needle_shift)
      {
	case 0: s->data.other.first_char=*(p_wchar0 *)needle; break;
	case 1: s->data.other.first_char=*(p_wchar1 *)needle; break;
	case 2: s->data.other.first_char=*(p_wchar2 *)needle; break;
      }
      break;
  }
}

void *generic_memory_search(struct generic_mem_searcher *s,
			    void *haystack,
			    size_t haystacklen,
			    char haystack_shift)
{
  if(s->needle_shift==0 && s->haystack_shift==0)
  {
    return memory_search(& s->data.eightbit, (char *)haystack, haystacklen);
  }
  switch(s->data.other.method)
  {
    case no_search:  return haystack;

    case use_memchr:
      switch(haystack_shift)
      {
        case 0:
	  return MEMCHR0((p_wchar0 *)haystack,
			 s->data.other.first_char,
			 haystacklen);
	  

        case 1:
	  return MEMCHR1((p_wchar1 *)haystack,
			 s->data.other.first_char,
			 haystacklen);

        case 2:
	  return MEMCHR2((p_wchar2 *)haystack,
			 s->data.other.first_char,
			 haystacklen);

	default:
	  Pike_fatal("Shift size out of range!\n");
      }

    case memchr_and_memcmp:
      switch((haystack_shift << 2)+s->needle_shift)
      {
#define SEARCH(X,Y)							  \
	case (X<<2)+Y:							  \
	{								  \
	  PIKE_CONCAT(p_wchar,X) *end,*hay;				  \
	  PIKE_CONCAT(p_wchar,Y) *needle;				  \
	  size_t needlelen;						  \
	  								  \
	  needle=(PIKE_CONCAT(p_wchar,Y) *)s->data.other.needle;	  \
	  hay=(PIKE_CONCAT(p_wchar,X) *)haystack;			  \
	  needlelen=s->data.other.needlelen;				  \
	  								  \
	  end=hay + haystacklen - needlelen+1;				  \
	  needle++;							  \
	  needlelen--;							  \
	  while((hay=(PIKE_CONCAT(p_wchar,X)*)PIKE_CONCAT(MEMCHR,X)(hay,  \
					   s->data.other.first_char,	  \
					   end-hay)))			  \
	    if(!PIKE_CONCAT4(compare_,Y,_to_,X)(++hay,needle,needlelen)) \
	      return (void *)(hay-1);					  \
	  								  \
	  return 0;							  \
	}


	SEARCH(0,0)
	SEARCH(0,1)
	SEARCH(0,2)

	SEARCH(1,0)
	SEARCH(1,1)
	SEARCH(1,2)

	SEARCH(2,0)
	SEARCH(2,1)
	SEARCH(2,2)

#undef SEARCH
      }

    default:
     Pike_fatal("Wacko method!\n");
  }
  /* NOT REACHED */
  return NULL;	/* Keep the compiler happy. */
}
		    

PMOD_EXPORT char *my_memmem(char *needle,
		size_t needlelen,
		char *haystack,
		size_t haystacklen)
{
  struct mem_searcher tmp;
  init_memsearch(&tmp, needle, needlelen, haystacklen);
  return memory_search(&tmp, haystack, haystacklen);
}

#endif

void memfill(char *to,
	     INT32 tolen,
	     char *from,
	     INT32 fromlen,
	     INT32 offset)
{
  if(fromlen==1)
  {
    MEMSET(to, *from, tolen);
  }
  else if(tolen>0)
  {
    INT32 tmp=MINIMUM(tolen, fromlen - offset);
    MEMCPY(to, from + offset, tmp);
    to+=tmp;
    tolen-=tmp;

    if(tolen > 0)
    {
      tmp=MINIMUM(tolen, fromlen);
      MEMCPY(to, from, tmp);
      from=to;
      to+=tmp;
      tolen-=tmp;
      
      while(tolen>0)
      {
	tmp=MINIMUM(tolen, fromlen);
	MEMCPY(to, from, tmp);
	fromlen+=tmp;
	tolen-=tmp;
	to+=tmp;
      }
    }
  }
}

#if 0
#if defined(HAVE_GETRLIMIT) && defined(HAVE_SETRLIMIT) && defined(RLIMIT_DATA)
#define SOFTLIM 

static long softlim_should_be=0;
#endif
#endif


PMOD_EXPORT void *debug_xalloc(size_t size)
{
  void *ret;
  if(!size) 
     Pike_error("Allocating zero bytes.\n");

  ret=(void *)malloc(size);
  if(ret) return ret;

  Pike_error("Out of memory - failed to allocate %"PRINTSIZET"d bytes.\n",
	     size);
  return 0;
}

PMOD_EXPORT void debug_xfree(void *mem)
{
  free(mem);
}

PMOD_EXPORT void *debug_xmalloc(size_t s)
{
  return malloc(s);
}

PMOD_EXPORT void *debug_xrealloc(void *m, size_t s)
{
  return realloc(m,s);
}

PMOD_EXPORT void *debug_xcalloc(size_t n, size_t s)
{
  return calloc(n,s);
}

char *debug_qalloc(size_t size)
{
  char *ret;
  if(!size) return 0;

  ret=(char *)malloc(size);
  if(ret) return ret;

#ifdef SOFTLIM
  {
    struct rlim lim;
    if(getrlimit(RLIMIT_DATA,&lim)>= 0)
    {
      if(lim.rlim_cur < lim.rlim_max)
      {
	lim.rlim_cur+=size;
	while(1)
	{
	  softlim_should_be=lim.rlim_cur;
	  if(lim.rlim_cur > lim.rlim_max)
	    lim.rlim_cur=lim.rlim_max;
	  
	  if(setrlimit(RLIM_DATA, &lim)>=0)
	  {
	    ret=(char *)malloc(size);
	    if(ret) return ret;
	  }
	  if(lim.rlim_cur >= lim.rlim_max) break;
	  lim.rlim_cur+=4096;
	}
      }
    }
  }
#endif

  Pike_fatal("Completely out of memory - "
	     "failed to allocate %"PRINTSIZET"d bytes!\n", size);
  /* NOT_REACHED */
  return NULL;	/* Keep the compiler happy. */
}

/*
 * mexec_*()
 *
 * Allocation of executable memory.
 */

#if defined(HAVE_MMAP) && defined(MEXEC_USES_MMAP)
#ifndef PAGESIZE
#define PAGESIZE	8192
#endif /* !PAGESIZE */
#if 0
#define MEXEC_MAGIC	0xdeadfeedf00dfaddLL
#endif /* 0 */
static int dev_zero = -1;
struct mexec_block {
  struct mexec_hdr *hdr;
  ptrdiff_t size;
#ifdef MEXEC_MAGIC
  unsigned long long magic;
#endif /* MEXEC_MAGIC */
};
struct mexec_free_block {
  struct mexec_free_block *next;
  ptrdiff_t size;
};
static struct mexec_hdr {
  struct mexec_hdr *next;
  ptrdiff_t size;
  char *bottom;
  struct mexec_free_block *free;/* Ordered according to reverse address. */
} *mexec_hdrs = NULL;		/* Ordered according to reverse address. */

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS	MAP_ANON
#endif /* !MAP_ANONYMOUS && MAP_ANON */

static struct mexec_hdr *grow_mexec_hdr(struct mexec_hdr *base, size_t sz)
{
  struct mexec_hdr *wanted = NULL;
  struct mexec_hdr *hdr;
  /* fprintf(stderr, "grow_mexec_hdr(%p, %p)\n", base, (void *)sz); */
  if (!sz) return NULL;
#ifndef MAP_ANONYMOUS
  /* Neither MAP_ANONYMOUS nor MAP_ANON.
   * Map /dev/zero.
   */
  if (dev_zero < 0) {
    if ((dev_zero = open("/dev/zero", O_RDONLY)) < 0) {
      fprintf(stderr, "Failed to open /dev/zero.\n");
      return NULL;
    }
    dmalloc_accept_leak_fd(dev_zero);
    set_close_on_exec(dev_zero, 1);
  }
#define MAP_ANONYMOUS	0
#endif /* !MAP_ANONYMOUS */
  sz = (sz + sizeof(struct mexec_hdr) + (PAGESIZE-1)) & ~(PAGESIZE-1);

  if (base) {
    wanted = (struct mexec_hdr *)(((char *)base) + base->size);
  }
  
  hdr = mmap(wanted, sz, PROT_EXEC|PROT_READ|PROT_WRITE,
	     MAP_PRIVATE|MAP_ANONYMOUS, dev_zero, 0);
  if (hdr == MAP_FAILED) {
    fprintf(stderr, "mmap failed, errno=%d.\n", errno);
    return NULL;
  }
  if (hdr == wanted) {
    /* We succeeded in growing. */
    base->size += sz;
    return base;
  }
  /* Find insertion slot in hdr list. */
  if ((wanted = mexec_hdrs)) {
    while ((size_t)wanted->next > (size_t)hdr) {
      wanted = wanted->next;
    }
    if (wanted->next) {
      if ((((char *)wanted->next)+wanted->next->size) == (char *)hdr) {
	/* We succeeded in growing some other hdr. */
	wanted->next->size += sz;
	return wanted->next;
      }
    }
    hdr->next = wanted->next;
    wanted->next = hdr;
  } else {
    hdr->next = NULL;
    mexec_hdrs = hdr;
  }
  hdr->size = sz;
  hdr->free = NULL;
  hdr->bottom = (char *)(hdr+1);
  return hdr;
}

/* Assumes sz has sufficient alignment and has space for a mexec_block. */
static struct mexec_block *low_mexec_alloc(struct mexec_hdr *hdr, size_t sz)
{
  struct mexec_free_block **free;
  struct mexec_block *res;
    
  /* fprintf(stderr, "low_mexec_alloc(%p, %p)\n", hdr, (void *)sz); */
  if (!hdr) return NULL;
  free = &hdr->free;
  while (*free && ((*free)->size < (ptrdiff_t)sz)) {
    free = &((*free)->next);
  }
  if ((res = (struct mexec_block *)*free)) {
    if (res->size <= (ptrdiff_t)(sz + 2*sizeof(struct mexec_block))) {
      /* No space for a split. */
      sz = (*free)->size;
      *free = ((struct mexec_free_block *)res)->next;
    } else {
      /* Split the block. */
      struct mexec_free_block *next =
	(struct mexec_free_block *)(((char *)res) + sz);
      next->next = (*free)->next;
      next->size = (*free)->size - sz;
      *free = next;
    }
  } else if ((hdr->bottom - ((char *)hdr)) <= (hdr->size - (ptrdiff_t)sz)) {
    res = (struct mexec_block *)hdr->bottom;
    if (hdr->bottom + 2*sizeof(struct mexec_block) - ((char *)hdr) >=
	hdr->size) {
      /* No space for a split. */
      sz = hdr->size - (hdr->bottom - ((char *)hdr));
    }
    hdr->bottom += sz;
  } else {
    return NULL;
  }
  res->size = sz;
  res->hdr = hdr;
#ifdef MEXEC_MAGIC
  res->magic = MEXEC_MAGIC;
#endif /* MEXEC_MAGIC */
  return res;
}

void mexec_free(void *ptr)
{
  struct mexec_free_block *prev_prev = NULL;
  struct mexec_free_block *prev = NULL;
  struct mexec_free_block *next = NULL;
  struct mexec_free_block *blk;
  struct mexec_block *mblk;
  struct mexec_hdr *hdr;

  /* fprintf(stderr, "mexec_free(%p)\n", ptr); */

  if (!ptr) return;
  mblk = ((struct mexec_block *)ptr)-1;
  hdr = mblk->hdr;
#ifdef MEXEC_MAGIC
  if (mblk->magic != MEXEC_MAGIC) {
    Pike_fatal("mexec_free() called with non mexec pointer.\n"
	       "ptr: %p, magic: 0x%016llx, hdr: %p\n",
	       ptr, mblk->magic, hdr);
  }
#endif /* MEXEC_MAGIC */

#ifdef VALGRIND_DISCARD_TRANSLATIONS
  VALGRIND_DISCARD_TRANSLATIONS (mblk, mblk->size);
#endif

  blk = (struct mexec_free_block *)mblk;

  next = hdr->free;
  while ((size_t)next > (size_t)blk) {
    prev_prev = prev;
    prev = next;
    next = next->next;
  }

  if (next && ((((char *)next) + next->size) == (char *)blk)) {
    /* Join with successor. */
    next->size += blk->size;
    blk = next;
  } else {
    blk->next = next;
  }

  if (prev) {
    if ((((char *)blk) + blk->size) == (char *)prev) {
      /* Join with prev. */
      blk->size += prev->size;
      if (prev_prev) {
	prev_prev->next = blk;
      } else {
	hdr->free = blk;
      }
    } else {
      prev->next = blk;
    }
  } else {
    if ((((char *)blk) + blk->size) == hdr->bottom) {
      /* Join with bottom. */
      hdr->bottom = (char *)blk;
      hdr->free = blk->next;
#if 0
      if (hdr->bottom == (char *)(hdr + 1)) {
	/* The entire mmapped block is free.
	 * FIXME: Consider unmaopping it.
	 */
      }
#endif /* 0 */
    } else {
      hdr->free = blk;
    }
  }
}

void *mexec_alloc(size_t sz)
{
  struct mexec_hdr *hdr;
  struct mexec_block *res;

  /* fprintf(stderr, "mexec_alloc(%p)\n", (void *)sz); */

  if (!sz) {
    /* fprintf(stderr, " ==> NULL (no size)\n"); */
    return NULL;
  }
  /* 32 byte alignment. */
  sz = (sz + sizeof(struct mexec_block) + 0x1f) & ~0x1f;
  hdr = mexec_hdrs;
  while (hdr && !(res = low_mexec_alloc(hdr, sz))) {
    hdr = hdr->next;
  }
  if (!hdr) {
    hdr = grow_mexec_hdr(NULL, sz);
    if (!hdr) {
      /* fprintf(stderr, " ==> NULL (grow failed)\n"); */
      return NULL;
    }
    res = low_mexec_alloc(hdr, sz);
#ifdef PIKE_DEBUG
    if (!res) {
      Pike_fatal("mexec_alloc(%d) failed to allocate from grown hdr!\n",
		 sz);
    }
#endif /* PIKE_DEBUG */
  }
  /* fprintf(stderr, " ==> %p\n", res + 1); */
#ifdef PIKE_DEBUG
  if ((res->hdr) != hdr) {
    Pike_fatal("mexec_alloc: Resulting block is not member of hdr: %p != %p\n",
	       res->hdr, hdr);
  }
#endif /* PIKE_DEBUG */
  return res + 1;
}

void *mexec_realloc(void *ptr, size_t sz)
{
  struct mexec_hdr *hdr;
  struct mexec_hdr *old_hdr = NULL;
  struct mexec_block *old;
  struct mexec_block *res = NULL;

  /* fprintf(stderr, "mexec_realloc(%p, %p)\n", ptr, (void *)sz); */

  if (!sz) {
    /* fprintf(stderr, " ==> NULL (no size)\n"); */
    return NULL;
  }
  if (!ptr) {
    /* fprintf(stderr, " ==> "); */
    return mexec_alloc(sz);
  }
  old = ((struct mexec_block *)ptr)-1;
  hdr = old->hdr;

#ifdef MEXEC_MAGIC
  if (old->magic != MEXEC_MAGIC) {
    Pike_fatal("mexec_realloc() called with non mexec pointer.\n"
	       "ptr: %p, magic: 0x%016llx, hdr: %p\n",
	       ptr, old->magic, hdr);
  }
#endif /* MEXEC_MAGIC */

  sz = (sz + sizeof(struct mexec_block) + 0x1f) & ~0x1f;
  if (old->size >= (ptrdiff_t)sz) {
    /* fprintf(stderr, " ==> %p (space existed)\n", ptr); */
    return ptr;			/* FIXME: Shrinking? */
  }

  if ((((char *)old) + old->size) == hdr->bottom) {
    /* Attempt to grow the block. */
#if 0
    if (((((char *)old) - ((char *)hdr)) <= (hdr->size - sz)) ||
	((res = low_mexec_alloc(hdr, sz)) == old)) {
      old->size = sz;
      hdr->bottom = ((char *)old) + sz;
      /* fprintf(stderr, " ==> %p (succeded in growing)\n", ptr); */
#ifdef PIKE_DEBUG
      if (old->hdr != hdr) {
	Pike_fatal("mexec_realloc: "
		   "Grown block is not member of hdr: %p != %p\n",
		   old->next, hdr);
      }
#endif /* PIKE_DEBUG */
      return ptr;
    }
#endif /* 0 */
    /* FIXME: Consider using grow_mexec_hdr to grow our hdr. */
    old_hdr = hdr;
  } else {
    res = low_mexec_alloc(hdr, sz);
  }
  if (!res) {
    hdr = mexec_hdrs;
    while (hdr && !(res = low_mexec_alloc(hdr, sz))) {
      hdr = hdr->next;
    }
    if (!hdr) {
      hdr = grow_mexec_hdr(old_hdr, sz);
      if (!hdr) {
	/* fprintf(stderr, " ==> NULL (grow failed)\n"); */
	return NULL;
      }
      if (hdr == old_hdr) {
	/* Succeeded in growing our old hdr. */
	old->size = sz;
	hdr->bottom = ((char *)old) + sz;
	/* fprintf(stderr, " ==> %p (succeded in growing hdr)\n", ptr); */
#ifdef PIKE_DEBUG
	if (old->hdr != hdr) {
	  Pike_fatal("mexec_realloc: "
		     "Grown block is not member of hdr: %p != %p\n",
		     old->hdr, hdr);
	}
#endif /* PIKE_DEBUG */
	return ptr;
      }
      res = low_mexec_alloc(hdr, sz);
#ifdef PIKE_DEBUG
      if (!res) {
	Pike_fatal("mexec_realloc(%p, %d) failed to allocate from grown hdr!\n",
		   ptr, sz);
      }
#endif /* PIKE_DEBUG */
    }
  }

  if (!res) {
    /* fprintf(stderr, " ==> NULL (low_mexec_alloc failed)\n"); */
    return NULL;
  }

  /* fprintf(stderr, " ==> %p\n", res + 1); */

  memcpy(res+1, old+1, old->size - sizeof(*old));
  mexec_free(ptr);

#ifdef PIKE_DEBUG
  if (res->hdr != hdr) {
    Pike_fatal("mexec_realloc: "
	       "Resulting block is not member of hdr: %p != %p\n",
	       res->hdr, hdr);
  }
#endif /* PIKE_DEBUG */

  return res + 1;
}

#elif defined (VALGRIND_DISCARD_TRANSLATIONS)

void *mexec_alloc (size_t sz)
{
  size_t *blk = malloc (sz + sizeof (size_t));
  if (!blk) return NULL;
  *blk = sz;
  return blk + 1;
}

void *mexec_realloc (void *ptr, size_t sz)
{
  if (ptr) {
    size_t *oldblk = ptr;
    size_t oldsz = oldblk[-1];
    size_t *newblk = realloc (oldblk - 1, sz + sizeof (size_t));
    if (!newblk) return NULL;
    if (newblk != oldblk)
      VALGRIND_DISCARD_TRANSLATIONS (oldblk, oldsz);
    else if (oldsz > sz)
      VALGRIND_DISCARD_TRANSLATIONS (newblk + sz, oldsz - sz);
    *newblk = sz;
    return newblk + 1;
  }
  return mexec_malloc (sz);
}

void mexec_free (void *ptr)
{
  size_t *blk = ptr;
  VALGRIND_DISCARD_TRANSLATIONS (blk, blk[-1]);
  free (blk - 1);
}

#else  /* !(HAVE_MMAP && MEXEC_USES_MMAP) && !VALGRIND_DISCARD_TRANSLATIONS */

void *mexec_alloc(size_t sz)
{
  return malloc(sz);
}
void *mexec_realloc(void *ptr, size_t sz)
{
  if (ptr) return realloc(ptr, sz);
  return malloc(sz);
}
void mexec_free(void *ptr)
{
  free(ptr);
}

#endif  /* !(HAVE_MMAP && MEXEC_USES_MMAP) && !VALGRIND_DISCARD_TRANSLATIONS */

/* #define DMALLOC_TRACE */
/* #define DMALLOC_TRACELOGSIZE	256*1024 */

/* #define DMALLOC_TRACE_MEMHDR ((struct memhdr *)0x40157218) */
/* #define DMALLOC_TRACE_MEMLOC ((struct memloc *)0x405500d8) */
#ifdef DMALLOC_TRACE_MEMLOC
#define DO_IF_TRACE_MEMLOC(X)	do { X; } while(0)
#else /* !DMALLOC_TRACE_MEMLOC */
#define DO_IF_TRACE_MEMLOC(X)
#endif /* DMALLOC_TRACE_MEMLOC */

#ifdef DMALLOC_TRACE
/* this can be used to supplement debugger data
 * to find out *how* the interpreter got to a certain
 * point, it is also useful when debuggers are not working.
 * -Hubbe
 *
 * Please note: We *should* probably make this so that
 * it remembers which thread was there too, and then we
 * would need a mutex to make sure all acceses to dmalloc_tracelogptr
 * are atomic.
 * -Hubbe
 */
char *dmalloc_tracelog[DMALLOC_TRACELOGSIZE];
size_t dmalloc_tracelogptr=0;

/*
 * This variable can be used to debug stack trashing,
 * simply insert:
 * {extern int dmalloc_print_trace; dmalloc_print_trace=1;}
 * somewhere before the code occurs, and from that point,
 * dmalloc will output all checkpoints to stderr.
 */
int dmalloc_print_trace;
#define DMALLOC_TRACE_LOG(X)
#endif


#ifdef DEBUG_MALLOC

#undef DO_IF_DMALLOC
#define DO_IF_DMALLOC(X)
#undef DO_IF_NOT_DMALLOC
#define DO_IF_NOT_DMALLOC(X) X


#include "threads.h"

#ifdef _REENTRANT
static MUTEX_T debug_malloc_mutex;
#endif


#undef malloc
#undef free
#undef realloc
#undef calloc
#undef strdup
#undef main

#ifdef HAVE_DLOPEN
#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif


#ifdef ENCAPSULATE_MALLOC
#ifdef RTLD_NEXT

#ifdef TH_KEY_T
#define DMALLOC_REMEMBER_LAST_LOCATION

TH_KEY_T dmalloc_last_seen_location;

#endif


#define dl_setup()  ( real_malloc ? 0 : real_dl_setup() )

void *(*real_malloc)(size_t);
void (*real_free)(void *);
void *(*real_realloc)(void *,size_t);
void *(*real_calloc)(size_t,size_t);

union fake_body
{
  struct fakemallocblock *next;
  char block[1];
  char *pad;
  double pad2;
};

struct fakemallocblock
{
  size_t size;
  union fake_body body;
};

static struct fakemallocblock fakemallocarea[65536];
static struct fakemallocblock *fake_free_list;
#define ALIGNMENT OFFSETOF(fakemallocblock, body)
#define FAKEMALLOCED(X) \
  ( ((char *)(X)) >= ((char *)fakemallocarea) && ((char *)(X)) < (((char *)(fakemallocarea))+sizeof(fakemallocarea)))
static void initialize_dmalloc(void);

void init_fake_malloc(void)
{
  fake_free_list=fakemallocarea;
  fake_free_list->size=sizeof(fakemallocarea) - ALIGNMENT;
  fake_free_list->body.next=0;
}

#if 0
#define LOWDEBUG2(X) write(2,X,strlen(X) - strlen(""));
#define LOWDEBUG(X) LOWDEBUG2( __FILE__ ":" DEFINETOSTR(__LINE__) ":" X)

void debug_output(char *x, void *p)
{
  char buf[120];
  write(2,x,strlen(x));
  sprintf(buf," (%p %d)\n",p,p);
  write(2,buf,strlen(buf));
}

#define LOWDEBUG3(X,Y) debug_output( __FILE__ ":" DEFINETOSTR(__LINE__) ":" X,Y)
#else
#define LOWDEBUG(X)
#define LOWDEBUG3(X,Y)
#endif

void *fake_malloc(size_t x)
{
  void *ret;
  struct fakemallocblock *block, **prev;

  LOWDEBUG3("fakemalloc",x);

  if(real_malloc)
  {
    ret=real_malloc(x);
    LOWDEBUG3("fakemalloc --> ",ret);
    return ret;
  }
    
  if(!x) return 0;

  if(!fake_free_list) init_fake_malloc();

  x+=ALIGNMENT-1;
  x&=-ALIGNMENT;

  for(prev=&fake_free_list;(block=*prev);prev=& block->body.next)
  {
    if(block->size >= x)
    {
      if(block->size > x + ALIGNMENT)
      {
	/* Split block */
	block->size-=x+ALIGNMENT;
	block=(struct fakemallocblock *)(block->body.block+block->size);
	block->size=x;
      }else{
	/* just return block */
	*prev = block->body.next;
      }
      LOWDEBUG3("fakemalloc --> ",block->body.block);
      return block->body.block;
    }
  }

  LOWDEBUG("Out of memory.\n");
  return 0;
}

PMOD_EXPORT void *malloc(size_t x)
{
  void *ret;
  LOWDEBUG3("malloc",x);
  if(!x) return 0;
  if(real_malloc)
  {
#ifdef DMALLOC_REMEMBER_LAST_LOCATION
    char * tmp=(char *)th_getspecific(dmalloc_last_seen_location);
    if(tmp)
    {
      if(tmp[0] == 'S' && tmp[-1]=='N')
	ret=debug_malloc(x,tmp-1);
      else
	ret=debug_malloc(x,tmp);
    }else{
      ret=debug_malloc(x,DMALLOC_LOCATION());
    }
#else
    ret=debug_malloc(x,DMALLOC_LOCATION());
#endif
  }else{
    ret=fake_malloc(x);
  }
#ifndef REPORT_ENCAPSULATED_MALLOC
  dmalloc_accept_leak(ret);
#endif
  LOWDEBUG3("malloc --> ",ret);
  return ret;
}

PMOD_EXPORT void fake_free(void *x)
{
  struct fakemallocblock * block;

  if(!x) return;

  LOWDEBUG3("fake_free",x);

  if(FAKEMALLOCED(x))
  {
    block=BASEOF(x,fakemallocblock,body.block);
    block->body.next=fake_free_list;
    fake_free_list=block;
  }else{
    return real_free(x);
  }
}

PMOD_EXPORT void free(void *x)
{
  struct fakemallocblock * block;

  if(!x) return;
  LOWDEBUG3("free",x);
  if(FAKEMALLOCED(x))
  {
    block=BASEOF(x,fakemallocblock,body.block);
    block->body.next=fake_free_list;
    fake_free_list=block;
  }else{
    return debug_free(x, DMALLOC_LOCATION(), 0);
  }
}

PMOD_EXPORT void *realloc(void *x,size_t y)
{
  void *ret;
  size_t old_size;
  struct fakemallocblock * block;

  LOWDEBUG3("realloc <-",x);
  LOWDEBUG3("realloc ",y);

  if(FAKEMALLOCED(x) || !real_realloc)
  {
    old_size = x?BASEOF(x,fakemallocblock,body.block)->size :0;
    LOWDEBUG3("realloc oldsize",old_size);
    if(old_size >= y) return x;
    ret=malloc(y);
    if(!ret) return 0;
    MEMCPY(ret, x, old_size);
    if(x) free(x);
  }else{
    ret=debug_realloc(x, y, DMALLOC_LOCATION());
  }
#ifndef REPORT_ENCAPSULATED_MALLOC
  dmalloc_accept_leak(ret);
#endif
  LOWDEBUG3("realloc --> ",ret);
  return ret;
}

void *fake_realloc(void *x,size_t y)
{
  void *ret;
  size_t old_size;
  struct fakemallocblock * block;

  LOWDEBUG3("fake_realloc <-",x);
  LOWDEBUG3("fake_realloc ",y);

  if(FAKEMALLOCED(x) || !real_realloc)
  {
    old_size = x?BASEOF(x,fakemallocblock,body.block)->size :0;
    if(old_size >= y) return x;
    ret=malloc(y);
    if(!ret) return 0;
    MEMCPY(ret, x, old_size);
    if(x) free(x);
  }else{
    ret=real_realloc(x,y);
  }
#ifndef REPORT_ENCAPSULATED_MALLOC
  dmalloc_accept_leak(ret);
#endif
  LOWDEBUG3("fake_realloc --> ",ret);
  return ret;
}

void *calloc(size_t x, size_t y)
{
  void *ret;
  LOWDEBUG3("calloc x",x);
  LOWDEBUG3("calloc y",y);
  ret=malloc(x*y);
  if(ret) MEMSET(ret,0,x*y);
#ifndef REPORT_ENCAPSULATED_MALLOC
  dmalloc_accept_leak(ret);
#endif
  LOWDEBUG3("calloc --> ",ret);
  return ret;
}

void *fake_calloc(size_t x, size_t y)
{
  void *ret;
  ret=fake_malloc(x*y);
  if(ret) MEMSET(ret,0,x*y);
#ifndef REPORT_ENCAPSULATED_MALLOC
  dmalloc_accept_leak(ret);
#endif
  LOWDEBUG3("fake_calloc --> ",ret);
  return ret;
}


#define DMALLOC_USING_DLOPEN

#endif /* RTLD_NEXT */
#endif /* ENCAPSULATE_MALLOC */
#endif /* HAVE_DLOPEN */

#ifndef DMALLOC_USING_DLOPEN
#define real_malloc malloc
#define real_free free
#define real_calloc calloc
#define real_realloc realloc

#define fake_malloc malloc
#define fake_free free
#define fake_calloc calloc
#define fake_realloc realloc
#else
#define malloc fake_malloc
#define free fake_free
#define realloc fake_realloc
#define calloc fake_calloc
#endif


#ifdef WRAP
#define malloc __real_malloc
#define free __real_free
#define realloc __real_realloc
#define calloc __real_calloc
#define strdup __real_strdup
#endif

#define LOCATION char *
#define LOCATION_NAME(X) ((X)+1)
#define LOCATION_IS_DYNAMIC(X) ((X)[0]=='D')


static struct memhdr *my_find_memhdr(void *, int);

#include "block_alloc_h.h"

BLOCK_ALLOC_FILL_PAGES(memloc, n/a)
BLOCK_ALLOC_FILL_PAGES(memory_map, n/a)
BLOCK_ALLOC_FILL_PAGES(memory_map_entry, n/a)

#include "block_alloc.h"

int verbose_debug_malloc = 0;
int verbose_debug_exit = 1;
int debug_malloc_check_all = 0;

/* #define DMALLOC_PROFILE */
#define DMALLOC_AD_HOC
/* #define DMALLOC_VERIFY_INTERNALS */

#ifdef DMALLOC_AD_HOC
/* A gigantic size (16Mb) will help a lot in AD_HOC mode */
#define LHSIZE 4100011
#else
#define LHSIZE 1109891
#endif

#ifdef DMALLOC_VERIFY_INTERNALS
#define DO_IF_VERIFY_INTERNALS(X) X
#else
#define DO_IF_VERIFY_INTERNALS(X)
#endif

#define DSTRHSIZE 10007

#define DEBUG_MALLOC_PAD 32
#define FREE_DELAY 4096
#define MAX_UNFREE_MEM 1024*1024*32
#define RNDSIZE 1777 /* A small size will help keep it in the cache */
#define AD_HOC_CHECK_INTERVAL 620 * 10

static void *blocks_to_free[FREE_DELAY];
static unsigned int blocks_to_free_ptr=0;
static unsigned long unfree_mem=0;
static int exiting=0;

struct memloc
{
  struct memloc *next;
  struct memhdr *mh;
  LOCATION location;
  int times;
};

#define MEM_PADDED			1
#define MEM_WARN_ON_FREE		2
#define MEM_REFERENCED			4
#define MEM_IGNORE_LEAK			8
#define MEM_FREE			16
#define MEM_LOCS_ADDED_TO_NO_LEAKS	32
#define MEM_TRACE			64

BLOCK_ALLOC_FILL_PAGES(memloc, 64)

struct memhdr
{
  struct memhdr *next;
  long size;
  int flags;
#ifdef DMALLOC_VERIFY_INTERNALS
  int times;		/* Should be equal to `+(@(locations->times))... */
#endif
#ifdef DMALLOC_AD_HOC
  int misses;
#endif
  int gc_generation;
  void *data;
  struct memloc *locations;
};

static struct memloc *mlhash[LHSIZE];
static char rndbuf[RNDSIZE + DEBUG_MALLOC_PAD*2];

static struct memhdr no_leak_memlocs;
static int memheader_references_located=0;


#ifdef DMALLOC_PROFILE
static int add_location_calls=0;
static int add_location_seek=0;
static int add_location_new=0;
static int add_location_cache_hits=0;
static int add_location_duplicate=0;  /* Used in AD_HOC mode */
#endif

#if DEBUG_MALLOC_PAD - 0 > 0
char *do_pad(char *mem, long size)
{
  mem+=DEBUG_MALLOC_PAD;

  if (PIKE_MEM_CHECKER()) {
    PIKE_MEM_NA_RANGE(mem - DEBUG_MALLOC_PAD, DEBUG_MALLOC_PAD);
    PIKE_MEM_NA_RANGE(mem + size, DEBUG_MALLOC_PAD);
  }

  else {
    unsigned long q,e;
    q= (((long)mem) ^ 0x555555) + (size * 9248339);
  
    /*  fprintf(stderr,"Padding  %p(%d) %ld\n",mem, size, q); */
#if 1
    q%=RNDSIZE;
    MEMCPY(mem - DEBUG_MALLOC_PAD, rndbuf+q, DEBUG_MALLOC_PAD);
    MEMCPY(mem + size, rndbuf+q, DEBUG_MALLOC_PAD);
#else
    for(e=0;e< DEBUG_MALLOC_PAD; e+=4)
    {
      char tmp;
      q=(q<<13) ^ ~(q>>5);

#define BLORG(X,Y)							\
      tmp=(Y);								\
      mem[e+(X)-DEBUG_MALLOC_PAD] = tmp;				\
      mem[size+e+(X)] = tmp;

      BLORG(0, (q) | 1)
	BLORG(1, (q >> 5) | 1)
	BLORG(2, (q >> 10) | 1)
	BLORG(3, (q >> 15) | 1)
	}
#endif
  }

  return mem;
}

#define FD2PTR(X) (void *)(ptrdiff_t)((X)*4+1)
#define PTR2FD(X) (((ptrdiff_t)(X))>>2)


void check_pad(struct memhdr *mh, int freeok)
{
  static int out_biking=0;
  unsigned long q,e;
  char *mem=mh->data;
  long size=mh->size;
  if(out_biking) return;

  if(!(mh->flags & MEM_PADDED)) return;
  if(size < 0)
  {
    if(!freeok)
    {
      fprintf(stderr,"Access to free block: %p (size %ld)!\n",mem, ~mh->size);
      dump_memhdr_locations(mh, 0, 0);
      abort();
    }else{
      size = ~size;
    }
  }

  if (PIKE_MEM_CHECKER()) return;

/*  fprintf(stderr,"Checking %p(%d) %ld\n",mem, size, q);  */
#if 1
  /* optimization? */
  if(MEMCMP(mem - DEBUG_MALLOC_PAD, mem+size, DEBUG_MALLOC_PAD))
  {
    q= (((long)mem) ^ 0x555555) + (size * 9248339);
    
    q%=RNDSIZE;
    if(MEMCMP(mem - DEBUG_MALLOC_PAD, rndbuf+q, DEBUG_MALLOC_PAD))
    {
      out_biking=1;
      fprintf(stderr,"Pre-padding overwritten for "
	      "block at %p (size %ld)!\n", mem, size);
      describe(mem);
      abort();
    }
    
    if(MEMCMP(mem + size, rndbuf+q, DEBUG_MALLOC_PAD))
    {
      out_biking=1;
      fprintf(stderr,"Post-padding overwritten for "
	      "block at %p (size %ld)!\n", mem, size);
      describe(mem);
      abort();
    }

    out_biking=1;
    fprintf(stderr,"Padding completely screwed up for "
	    "block at %p (size %ld)!\n", mem, size);
    describe(mem);
    abort();
  }
#else
  q= (((long)mem) ^ 0x555555) + (size * 9248339);

  for(e=0;e< DEBUG_MALLOC_PAD; e+=4)
  {
    char tmp;
    q=(q<<13) ^ ~(q>>5);

#undef BLORG
#define BLORG(X,Y) 						\
    tmp=(Y);                                                    \
    if(mem[e+(X)-DEBUG_MALLOC_PAD] != tmp)			\
    {								\
      out_biking=1;						\
      fprintf(stderr,"Pre-padding overwritten for "		\
	      "block at %p (size %ld) (e=%ld %d!=%d)!\n",	\
	      mem, size, e, tmp, mem[e-DEBUG_MALLOC_PAD]);	\
      describe(mem);						\
      abort();							\
    }								\
    if(mem[size+e+(X)] != tmp)					\
    {								\
      out_biking=1;						\
      fprintf(stderr,"Post-padding overwritten for "		\
	      "block at %p (size %ld) (e=%ld %d!=%d)!\n",	\
	      mem, size, e, tmp, mem[size+e]);			\
      describe(mem);						\
      abort();							\
    }

    BLORG(0, (q) | 1)
    BLORG(1, (q >> 5) | 1)
    BLORG(2, (q >> 10) | 1)
    BLORG(3, (q >> 15) | 1)
  }
#endif
}
#else
#define do_pad(X,Y) (X)
#define check_pad(M,X)
#endif


static inline unsigned long lhash(struct memhdr *m, LOCATION location)
{
  unsigned long l;
  l=(long)m;
  l*=53;
  l+=(long)location;
  l%=LHSIZE;
  return l;
}

#undef INIT_BLOCK
#undef EXIT_BLOCK

#define INIT_BLOCK(X) do {				\
    X->locations = NULL;				\
    X->flags=0;						\
    DO_IF_VERIFY_INTERNALS(				\
      X->times=0;					\
    );							\
  } while(0)
#define EXIT_BLOCK(X) do {				\
  struct memloc *ml;					\
  DO_IF_VERIFY_INTERNALS(				\
    int times = 0;					\
    ml = X->locations;					\
    while(ml)						\
    {							\
      times += ml->times;				\
      ml = ml->next;					\
    }							\
    if (times != X->times) {				\
      dump_memhdr_locations(X, 0, 0);			\
      Pike_fatal("%p: Dmalloc lost locations for block at %p? " \
		 "total:%d  !=  accumulated:%d\n",	\
		 X, X->data, X->times, times);		\
    }							\
  );							\
  while((ml=X->locations))				\
  {							\
    unsigned long l = lhash(X, ml->location);		\
    if(mlhash[l]==ml) mlhash[l] = NULL;			\
							\
    X->locations=ml->next;				\
    DO_IF_TRACE_MEMLOC(					\
      if (ml == DMALLOC_TRACE_MEMLOC) {			\
        fprintf(stderr, "EXIT:Freeing memloc %p location "	\
		"was %s memhdr: %p data: %p\n",		\
		ml, ml->location, ml->mh, ml->mh->data);\
    });							\
    if (X->flags & MEM_TRACE) {				\
      fprintf(stderr, "  0x%p: Freeing loc: %p:%s\n",	\
	      X, ml, ml->location);			\
    }							\
    really_free_memloc(ml);				\
  }							\
}while(0)

#undef BLOCK_ALLOC_HSIZE_SHIFT
#define BLOCK_ALLOC_HSIZE_SHIFT 1
PTR_HASH_ALLOC_FILL_PAGES(memhdr, 128)

#undef INIT_BLOCK
#undef EXIT_BLOCK

#define INIT_BLOCK(X)
#define EXIT_BLOCK(X)



static struct memhdr *my_find_memhdr(void *p, int already_gone)
{
  struct memhdr *mh;

#if DEBUG_MALLOC_PAD - 0 > 0
  if(debug_malloc_check_all)
  {
    unsigned long h;
    for(h=0;h<(unsigned long)memhdr_hash_table_size;h++)
    {
      for(mh=memhdr_hash_table[h]; mh; mh=mh->next)
      {
	check_pad(mh,1);
      }
    }
  }
#endif

  if((mh=find_memhdr(p))) {
#ifdef DMALLOC_VERIFY_INTERNALS
    if (mh->data != p) {
      Pike_fatal("find_memhdr() returned memhdr for different block\n");
    }
#endif
    if(!already_gone)
      check_pad(mh,0);
  }

  return mh;
}


static int find_location(struct memhdr *mh, LOCATION location)
{
  struct memloc *ml;
  unsigned long l=lhash(mh,location);

  if(mlhash[l] &&
     mlhash[l]->mh==mh &&
     mlhash[l]->location==location)
    return 1;

  for(ml=mh->locations;ml;ml=ml->next)
  {
    if(ml->location==location)
    {
#ifdef DMALLOC_TRACE_MEMLOC
      if (ml == DMALLOC_TRACE_MEMLOC) {
        fprintf(stderr, "find_loc: Found memloc %p location %s memhdr: %p data: %p\n",
		ml, ml->location, ml->mh, ml->mh->data);
      }
#endif /* DMALLOC_TRACE_MEMLOC */
      mlhash[l]=ml;
      return 1;
    }
  }
  return 0;
}

#ifdef DMALLOC_AD_HOC
void merge_location_list(struct memhdr *mh)
{
  struct memloc *ml,*ml2,**prev;
  for(ml=mh->locations;ml;ml=ml->next)
  {
    /* Make sure only live memloc's are left in the cache. */
    unsigned long l = lhash(mh, ml->location);
    mlhash[l] = ml;

#ifdef DMALLOC_TRACE_MEMLOC
    if (ml == DMALLOC_TRACE_MEMLOC) {
      fprintf(stderr, "merge_loc: Found memloc %p location %s memhdr: %p data: %p\n",
	      ml, ml->location, ml->mh, ml->mh->data);
    }
#endif /* DMALLOC_TRACE_MEMLOC */

#ifdef DMALLOC_VERIFY_INTERNALS
    if (ml->mh != mh) {
      Pike_fatal("Non-owned memloc in location list!\n");
    }
#endif

    prev=&ml->next;
    while((ml2=*prev))
    {
      if(ml->location == ml2->location)
      {
	ml->times+=ml2->times;
	*prev=ml2->next;
#ifdef DMALLOC_TRACE_MEMLOC
	if (ml2 == DMALLOC_TRACE_MEMLOC) {
	  fprintf(stderr, "merge_loc: Freeing memloc %p location %s memhdr: %p data: %p\n",
		  ml2, ml2->location, ml2->mh, ml2->mh->data);
	}
#endif /* DMALLOC_TRACE_MEMLOC */
	really_free_memloc(ml2);
#ifdef DMALLOC_PROFILE
	add_location_duplicate++;
#endif
      }else{
	prev=&ml2->next;
      }
#ifdef DMALLOC_VERIFY_INTERNALS
      if (ml2->mh != mh) {
	Pike_fatal("Non-owned memloc in location list!\n");
      }
#endif
    }
  }
}
#endif

#ifdef DMALLOC_AD_HOC
static int add_location_cleanup(struct memhdr *mh,
				LOCATION location,
				unsigned long l)
{
  struct memloc *ml;
  unsigned long l2;
  struct memloc **prev=&mh->locations;

  mh->misses=0;

  while((ml=*prev))
  {
#ifdef DMALLOC_PROFILE
    add_location_seek++;
#endif
#ifdef DMALLOC_VERIFY_INTERNALS
    if (ml->mh != mh) {
      Pike_fatal("Non-owned memloc in location list!\n");
    }
#endif

    if(ml->location == location)
    {
      /* Found. */
      ml->times++;
      mlhash[l]=ml;
      prev=&ml->next;
      
      while((ml=*prev))
      {
	l2=lhash(mh, ml->location);

#ifdef DMALLOC_VERIFY_INTERNALS
	if (ml->mh != mh) {
	  Pike_fatal("Non-owned memloc in location list!\n");
	}
#endif

	if(mlhash[l2] && mlhash[l2]!=ml &&
	   mlhash[l2]->mh == mh && mlhash[l2]->location == ml->location)
	{
	  /* We found a duplicate */
#ifdef DMALLOC_PROFILE
	  add_location_duplicate++;
#endif
	  mlhash[l2]->times+=ml->times;
	  *prev=ml->next;
#ifdef DMALLOC_TRACE_MEMLOC
	  if (ml == DMALLOC_TRACE_MEMLOC) {
	    fprintf(stderr, "add_loc: Freeing memloc %p location %s memhdr: %p data: %p\n",
		    ml, ml->location, ml->mh, ml->mh->data);
	  }
#endif /* DMALLOC_TRACE_MEMLOC */
	  really_free_memloc(ml);
	}else{
	  if (!mlhash[l2]) mlhash[l2] = ml;
	  prev=&ml->next;
	}
      }
      return 1;
    }

    l2=lhash(mh, ml->location);
        
    if(mlhash[l2] && mlhash[l2]!=ml &&
       mlhash[l2]->mh == mh && mlhash[l2]->location == ml->location)
    {
      /* We found a duplicate */
#ifdef DMALLOC_PROFILE
      add_location_duplicate++;
#endif
      mlhash[l2]->times+=ml->times;
      *prev=ml->next;
#ifdef DMALLOC_TRACE_MEMLOC
      if (ml == DMALLOC_TRACE_MEMLOC) {
        fprintf(stderr, "add_loc: 2 Freeing memloc %p location %s memhdr: %p data: %p\n",
		ml, ml->location, ml->mh, ml->mh->data);
      }
#endif /* DMALLOC_TRACE_MEMLOC */
      really_free_memloc(ml);
    }else{
      if (!mlhash[l2]) mlhash[l2] = ml;
      prev=&ml->next;
    }
  }

  return 0;
}
#endif
				 
static void add_location(struct memhdr *mh,
			 LOCATION location)
{
  struct memloc *ml;
  unsigned long l;

#ifdef DMALLOC_TRACE
  if(dmalloc_print_trace)
    fprintf(stderr,"%p: %s\n", mh, location);
  DMALLOC_TRACE_LOG(location);
#endif

#if DEBUG_MALLOC - 0 < 2
  if(find_location(&no_leak_memlocs, location)) return;
#endif

#ifdef DMALLOC_VERIFY_INTERNALS
  mh->times++;
#endif

  if (mh->flags & MEM_TRACE) {
    fprintf(stderr, "add_location(0x%p, %s)\n", mh, location);
  }

#ifdef DMALLOC_PROFILE
  add_location_calls++;
#endif

  l=lhash(mh,location);

  if(mlhash[l] && (mlhash[l]->mh==mh) && (mlhash[l]->location==location))
  {
    if (mh->flags & MEM_TRACE) {
      fprintf(stderr, "Found in cache.\n");
      for (ml = mh->locations; ml; ml = ml->next) {
	if (ml == mlhash[l]) break;
      }
#ifdef DMALLOC_VERIFY_INTERNALS
      if (!ml) {
	dump_memhdr_locations(mh, 0, 0);
	Pike_fatal("Memloc 0x%p in mlhash, but not in mh->locations!\n"
		   "data:0x%p, location:%s\n",
		   mlhash[l], mh->data, location);
      }
#endif
    }
    mlhash[l]->times++;
#ifdef DMALLOC_PROFILE
    add_location_cache_hits++;
#endif
    return;
  }

#ifdef DMALLOC_AD_HOC
  if(mh->misses > AD_HOC_CHECK_INTERVAL)
    if(add_location_cleanup(mh, location, l)) {
      if (mh->flags & MEM_TRACE) {
	fprintf(stderr, "Adjusted by add_location_cleanup().\n");
      }
      return;
    }
#else
  for(ml=mh->locations;ml;ml=ml->next)
  {
#ifdef DMALLOC_PROFILE
    add_location_seek++;
#endif
    if(ml->location==location) {
      ml->times++;
      mlhash[l]=ml;
      return;
    }
  }
#endif

  if (mh->flags & MEM_TRACE) {
    fprintf(stderr, "Creating a new entry.\n");
  }

#ifdef DMALLOC_PROFILE
  add_location_new++;
#endif
  ml=alloc_memloc();
  ml->times=1;
  ml->location=location;
  ml->next=mh->locations;
  ml->mh=mh;
  mh->locations=ml;

#ifdef DMALLOC_AD_HOC
  mh->misses++;
#endif
  mlhash[l]=ml;
#ifdef DMALLOC_TRACE_MEMLOC
  if (ml == DMALLOC_TRACE_MEMLOC) {
    fprintf(stderr, "add_loc: Allocated memloc %p location %s memhdr: %p data: %p\n",
	    ml, ml->location, ml->mh, ml->mh->data);
  }
#endif /* DMALLOC_TRACE_MEMLOC */
  return;
}

static void remove_location(struct memhdr *mh, LOCATION location)
{
  struct memloc *ml,**prev;
  unsigned long l;

#if defined(DMALLOC_VERIFY_INTERNALS) && !defined(__NT__) && defined(PIKE_THREADS)
  if(!mt_trylock(& debug_malloc_mutex))
    Pike_fatal("remove_location running unlocked!\n");
#endif
  
#if DEBUG_MALLOC - 0 < 2
  if(find_location(&no_leak_memlocs, location)) return;
#endif

  l=lhash(mh,location);

  if(mlhash[l] && mlhash[l]->mh==mh && mlhash[l]->location==location)
    mlhash[l]=NULL;

  
  prev=&mh->locations;
  while((ml=*prev))
  {
#ifdef DMALLOC_VERIFY_INTERNALS
    if (ml->mh != mh) {
      Pike_fatal("Non-owned memloc in location list!\n");
    }
#endif

    if(ml->location==location)
    {
      *prev=ml->next;
#ifdef DMALLOC_VERIFY_INTERNALS
      mh->times -= ml->times;
#endif
#ifdef DMALLOC_TRACE_MEMLOC
      if (ml == DMALLOC_TRACE_MEMLOC) {
        fprintf(stderr, "rem_loc: Freeing memloc %p location %s memhdr: %p data: %p\n",
		ml, ml->location, ml->mh, ml->mh->data);
      }
#endif /* DMALLOC_TRACE_MEMLOC */
      really_free_memloc(ml);
#ifndef DMALLOC_AD_HOC
      break;
#endif
    }else{
      prev=&ml->next;
    }
  }
}

LOCATION dmalloc_default_location=0;

static struct memhdr *low_make_memhdr(void *p, int s, LOCATION location)
{
  struct memhdr *mh = get_memhdr(p);
  struct memloc *ml = alloc_memloc();
  unsigned long l;

  if (mh->locations) {
    dump_memhdr_locations(mh, NULL, 0);
    Pike_fatal("New block at %p already has locations.\n"
	       "location: %s\n", p, location);
  }

  l = lhash(mh,location);
  mh->size=s;
  mh->flags=0;
#ifdef DMALLOC_VERIFY_INTERNALS
  mh->times=1;
#endif
#ifdef DMALLOC_AD_HOC
  mh->misses=0;
#endif
  ml->next = mh->locations;
  mh->locations = ml;
  mh->gc_generation=gc_generation * 1000 + Pike_in_gc;
  ml->location=location;
  ml->times=1;
  ml->mh=mh;
  mlhash[l]=ml;

#ifdef DMALLOC_TRACE_MEMHDR
  if (mh == DMALLOC_TRACE_MEMHDR) {
    fprintf(stderr, "Allocated memhdr 0x%p\n", mh);
    mh->flags |= MEM_TRACE;
  }
#endif /* DMALLOC_TRACE_MEMHDR */

#ifdef DMALLOC_TRACE_MEMLOC
  if (ml == DMALLOC_TRACE_MEMLOC) {
    fprintf(stderr, "mk_mhdr: Allocated memloc %p location %s memhdr: %p data: %p\n",
	    ml, ml->location, ml->mh, ml->mh->data);
  }
#endif /* DMALLOC_TRACE_MEMLOC */

  if(dmalloc_default_location)
    add_location(mh, dmalloc_default_location);
  return mh;
}

void dmalloc_trace(void *p)
{
  struct memhdr *mh = my_find_memhdr(p, 0);
  if (mh) {
    mh->flags |= MEM_TRACE;
  }
}

void dmalloc_register(void *p, int s, LOCATION location)
{
  mt_lock(&debug_malloc_mutex);
  low_make_memhdr(p, s, location);
  mt_unlock(&debug_malloc_mutex);
}

void dmalloc_accept_leak(void *p)
{
  if(p)
  {
    struct memhdr *mh;
    mt_lock(&debug_malloc_mutex);
    if((mh=my_find_memhdr(p,0)))
      mh->flags |= MEM_IGNORE_LEAK;
    mt_unlock(&debug_malloc_mutex);
  }
}

static void low_add_marks_to_memhdr(struct memhdr *to,
				    struct memhdr *from)
{
  struct memloc *l;
  if(!from) return;
  for(l=from->locations;l;l=l->next)
    add_location(to, l->location);
}

void add_marks_to_memhdr(struct memhdr *to, void *ptr)
{
  mt_lock(&debug_malloc_mutex);

  low_add_marks_to_memhdr(to,my_find_memhdr(ptr,0));

  mt_unlock(&debug_malloc_mutex);
}

static void unregister_memhdr(struct memhdr *mh, int already_gone)
{
  if(mh->size < 0) mh->size=~mh->size;
  if(!already_gone) check_pad(mh,0);
  if(!(mh->flags & MEM_LOCS_ADDED_TO_NO_LEAKS))
    low_add_marks_to_memhdr(&no_leak_memlocs, mh);
  if (mh->flags & MEM_TRACE) {
    fprintf(stderr, "Removing memhdr %p\n", mh);
  }
  if (!remove_memhdr(mh->data)) {
#ifdef DMALLOC_VERIFY_INTERNALS
    Pike_fatal("remove_memhdr(%p) returned false.\n", mh->data);
#endif
  }
}

static int low_dmalloc_unregister(void *p, int already_gone)
{
  struct memhdr *mh=find_memhdr(p);
  if(mh)
  {
    unregister_memhdr(mh, already_gone);
    return 1;
  }
  return 0;
}

int dmalloc_unregister(void *p, int already_gone)
{
  int ret;
  mt_lock(&debug_malloc_mutex);
  ret=low_dmalloc_unregister(p,already_gone);
  mt_unlock(&debug_malloc_mutex);
  return ret;
}

static int low_dmalloc_mark_as_free(void *p, int already_gone)
{
  struct memhdr *mh=find_memhdr(p);
  if(mh)
  {
    if (mh->flags & MEM_TRACE) {
      fprintf(stderr, "Marking memhdr %p as free\n", mh);
    }
    if(!(mh->flags & MEM_FREE))
    {
      mh->size=~mh->size;
      mh->flags |= MEM_FREE | MEM_IGNORE_LEAK | MEM_LOCS_ADDED_TO_NO_LEAKS;
      low_add_marks_to_memhdr(&no_leak_memlocs, mh);
    }
    return 1;
  }
  return 0;
}

int dmalloc_mark_as_free(void *p, int already_gone)
{
  int ret;
  mt_lock(&debug_malloc_mutex);
  ret=low_dmalloc_mark_as_free(p,already_gone);
  mt_unlock(&debug_malloc_mutex);
  return ret;
}


void *debug_malloc(size_t s, LOCATION location)
{
  char *m;

  /* Complain on attempts to allocate more than 128MB memory */
  if (s > (size_t)0x08000000) {
    Pike_fatal("malloc(0x%08lx) -- Huge malloc!\n", (unsigned long)s);
  }

  mt_lock(&debug_malloc_mutex);

  m=(char *)real_malloc(s + DEBUG_MALLOC_PAD*2);
  if(m)
  {
    m=do_pad(m, s);
    low_make_memhdr(m, s, location)->flags|=MEM_PADDED;
  }

  if(verbose_debug_malloc)
    fprintf(stderr, "malloc(%ld) => %p  (%s)\n",
	    DO_NOT_WARN((long)s),
	    m, LOCATION_NAME(location));

  mt_unlock(&debug_malloc_mutex);
  return m;
}

void *debug_calloc(size_t a, size_t b, LOCATION location)
{
  void *m=debug_malloc(a*b,location);
  if(m)
    MEMSET(m, 0, a*b);

  if(verbose_debug_malloc)
    fprintf(stderr, "calloc(%ld, %ld) => %p  (%s)\n",
	    DO_NOT_WARN((long)a),
	    DO_NOT_WARN((long)b),
	    m, LOCATION_NAME(location));

  return m;
}

void *debug_realloc(void *p, size_t s, LOCATION location)
{
  char *m,*base;
  struct memhdr *mh = 0;
  mt_lock(&debug_malloc_mutex);

  if (p && (mh = my_find_memhdr(p,0))) {
    base = (char *) p - DEBUG_MALLOC_PAD;
    PIKE_MEM_RW_RANGE(base, mh->size + 2 * DEBUG_MALLOC_PAD);
  }
  else
    base = p;
  m=fake_realloc(base, s+DEBUG_MALLOC_PAD*2);

  if(m) {
    m=do_pad(m, s);
    if (mh) {
      mh->size = s;
      add_location(mh, location);
      move_memhdr(mh, m);
    }
    else
      low_make_memhdr(m, s, location)->flags|=MEM_PADDED;
  }
  if(verbose_debug_malloc)
    fprintf(stderr, "realloc(%p, %ld) => %p  (%s)\n",
	    p,
	    DO_NOT_WARN((long)s),
	    m, LOCATION_NAME(location));
  mt_unlock(&debug_malloc_mutex);
  return m;
}

void debug_free(void *p, LOCATION location, int mustfind)
{
  struct memhdr *mh;
  if(!p) return;
  mt_lock(&debug_malloc_mutex);

#ifdef WRAP
  mustfind=1;
#endif

  mh=my_find_memhdr(p,0);

  if(verbose_debug_malloc || (mh && (mh->flags & MEM_WARN_ON_FREE)))
    fprintf(stderr, "free(%p) (%s)\n", p, LOCATION_NAME(location));

  if(!mh && mustfind && p)
  {
    fprintf(stderr,"Lost track of a mustfind memory block: %p!\n",p);
    abort();
  }

  if(!exiting && mh)
  {
    void *p2;
    if (PIKE_MEM_CHECKER())
      PIKE_MEM_NA_RANGE(p, mh->size);
    else
      MEMSET(p, 0x55, mh->size);
    if(mh->size < MAX_UNFREE_MEM/FREE_DELAY)
    {
      add_location(mh, location);
      mh->size = ~mh->size;
      mh->flags|=MEM_FREE | MEM_IGNORE_LEAK;
      blocks_to_free_ptr++;
      blocks_to_free_ptr%=FREE_DELAY;
      p2=blocks_to_free[blocks_to_free_ptr];
      blocks_to_free[blocks_to_free_ptr]=p;
      if((p=p2))
      {
	mh=my_find_memhdr(p,1);
	if(!mh)
	{
	  fprintf(stderr,"Lost track of a freed memory block: %p!\n",p);
	  abort();
	}
      }else{
	mh=0;
      }
    }
  }
  
  if(mh)
  {
    PIKE_MEM_RW_RANGE((char *) p - DEBUG_MALLOC_PAD,
		      (mh->size > 0 ? mh->size : ~mh->size) + 2 * DEBUG_MALLOC_PAD);
    unregister_memhdr(mh,0);
    real_free( ((char *)p) - DEBUG_MALLOC_PAD );
  }
  else
  {
    fake_free(p); /* may be a fakemalloc block */
  }
  mt_unlock(&debug_malloc_mutex);
}

void dmalloc_check_block_free(void *p, char *location)
{
  struct memhdr *mh;
  mt_lock(&debug_malloc_mutex);
  mh=my_find_memhdr(p,0);

  if(mh && mh->size>=0)
  {
    if(!(mh->flags & MEM_IGNORE_LEAK))
    {
      fprintf(stderr,"Freeing storage for small block still in use %p at %s.\n",p,LOCATION_NAME(location));
      debug_malloc_dump_references(p,0,2,0);
    }
    mh->flags |= MEM_FREE | MEM_IGNORE_LEAK;
    mh->size = ~mh->size;
  }

  mt_unlock(&debug_malloc_mutex);
}

void dmalloc_free(void *p)
{
  debug_free(p, DMALLOC_LOCATION(), 0);
}

char *debug_strdup(const char *s, LOCATION location)
{
  char *m;
  long length;
  length=strlen(s);
  m=(char *)debug_malloc(length+1,location);
  MEMCPY(m,s,length+1);

  if(verbose_debug_malloc)
    fprintf(stderr, "strdup(\"%s\") => %p  (%s)\n", s, m, LOCATION_NAME(location));

  return m;
}

#ifdef WRAP
void *__wrap_malloc(size_t size)
{
  return debug_malloc(size, "Smalloc");
}

void *__wrap_realloc(void *m, size_t size)
{
  return debug_realloc(m, size, "Srealloc");
}

void *__wrap_calloc(size_t size,size_t num)
{
  return debug_calloc(size,num,"Scalloc");
}

void __wrap_free(void * size)
{
  return debug_free(size, "Sfree", 0);
}

void *__wrap_strdup(const char *s)
{
  return debug_strdup(s, "Sstrdup");
}
#endif


void dump_memhdr_locations(struct memhdr *from,
			   struct memhdr *notfrom,
			   int indent)
{
  struct memloc *l;
  if(!from) return;
#ifdef DMALLOC_AD_HOC
  merge_location_list(from);
#endif

  fprintf(stderr,"%*s gc generation: %d/%d  gc pass: %d/%d\n",
	  indent,"",
	  from->gc_generation / 1000,
	  gc_generation,
	  from->gc_generation % 1000,
	  Pike_in_gc);
	  

  for(l=from->locations;l;l=l->next)
  {
    if(notfrom && find_location(notfrom, l->location))
      continue;

    
    fprintf(stderr,"%*s %s %s (%d times) %s\n",
	    indent,"",
	    LOCATION_IS_DYNAMIC(l->location) ? "-->" : "***",
	    LOCATION_NAME(l->location),
	    l->times,
	    find_location(&no_leak_memlocs, l->location) ? "" :
	    ( from->flags & MEM_REFERENCED ? "*" : "!*!")
	    );

    /* Allow linked memhdrs */
/*    dump_memhdr_locations(my_find_memhdr(l,0),notfrom,indent+2); */
  }
}

static void low_dmalloc_describe_location(struct memhdr *mh, int offset, int indent);


static void find_references_to(void *block, int indent, int depth, int flags)
{
  unsigned long h;
  struct memhdr *m;
  int warned=0;

  for(h=0;h<(unsigned long)memhdr_hash_table_size;h++)
  {
    /* Avoid infinite recursion */
    ptrdiff_t num_to_check=0;
    void *to_check[1000];
    for(m=memhdr_hash_table[h];m;m=m->next)
    {
      if(num_to_check >= (ptrdiff_t) NELEM(to_check))
      {
	warned=1;
	fprintf(stderr,"  <We might miss some references!!>\n");
	break;
      }
      to_check[num_to_check++]=m->data;
    }

    while(--num_to_check >= 0)
    {
      unsigned int e;
      struct memhdr *tmp;
      void **p;

      p=to_check[num_to_check];
      m=find_memhdr(p);
      if(!m)
      {
	if(!warned)
	{
	  warned=1;
	  fprintf(stderr,"  <We might miss some references!!>\n");
	}
	continue;
      }
      
      if( ! ((sizeof(void *)-1) & (long) p ))
      {
	if(m->size > 0)
	{
	  for(e=0;e<m->size/sizeof(void *);e++)
	  {
	    if(p[e] == block)
	    {
/*	      fprintf(stderr,"  <from %p word %d>\n",p,e); */
	      describe_location(p,PIKE_T_UNKNOWN,p+e, indent,depth,flags);

/*	      low_dmalloc_describe_location(m, e * sizeof(void *), indent); */

	      m->flags |= MEM_WARN_ON_FREE;
	    }
	  }
	}
      }
    }
  }

  memheader_references_located=1;
}

void dmalloc_find_references_to(void *block)
{
  mt_lock(&debug_malloc_mutex);
  find_references_to(block, 2, 1, 0);
  mt_unlock(&debug_malloc_mutex);
}

void *dmalloc_find_memblock_base(void *ptr)
{
  unsigned long h;
  struct memhdr *m;
  char *lookfor=(char *)ptr;

  for(h=0;h<(unsigned long)memhdr_hash_table_size;h++)
  {
    for(m=memhdr_hash_table[h];m;m=m->next)
    {
      unsigned int e;
      struct memhdr *tmp;
      char *p=(char *)m->data;
      
      if( ! ((sizeof(void *)-1) & (long) p ))
      {
	if( p <= lookfor && lookfor < p + m->size)
	{
	  mt_unlock(&debug_malloc_mutex);
	  return m->data;
	}
      }
    }
  }

  return 0;
}

/* FIXME: lock the mutex */
void debug_malloc_dump_references(void *x, int indent, int depth, int flags)
{
  struct memhdr *mh=my_find_memhdr(x,0);
  if(!mh) return;
  if(memheader_references_located)
  {
    if(mh->flags & MEM_IGNORE_LEAK)
    {
      fprintf(stderr,"%*s<<<This leak has been ignored>>>\n",indent,"");
    }
    else if(mh->flags & MEM_REFERENCED)
    {
      fprintf(stderr,"%*s<<<Possibly referenced>>>\n",indent,"");
      if(!(flags & 2))
	find_references_to(x,indent+2,depth-1,flags);
    }
    else
    {
      fprintf(stderr,"%*s<<<=- No known references to this block -=>>>\n",indent,"");
    }
  }
  dump_memhdr_locations(mh,0, indent+2);
}

void list_open_fds(void)
{
  unsigned long h;
  mt_lock(&debug_malloc_mutex);

  for(h=0;h<(unsigned long)memhdr_hash_table_size;h++)
  {
    struct memhdr *m;
    for(m=memhdr_hash_table[h];m;m=m->next)
    {
      struct memhdr *tmp;
      struct memloc *l;
      void *p=m->data;
      if(m->flags & MEM_FREE) continue;
      
      if( 1 & (long) p )
      {
	if( FD2PTR( PTR2FD(p) ) == p)
	{
	  fprintf(stderr,"Filedescriptor %ld\n", (long) PTR2FD(p));

	  dump_memhdr_locations(m, 0, 0);
	}
      }
    }
  }
  mt_unlock(&debug_malloc_mutex);
}

static void low_search_all_memheaders_for_references(void)
{
  unsigned long h;
  struct memhdr *m;

  if (PIKE_MEM_CHECKER()) return;

  for(h=0;h<(unsigned long)memhdr_hash_table_size;h++)
    for(m=memhdr_hash_table[h];m;m=m->next)
      m->flags &=~ MEM_REFERENCED;

  for(h=0;h<(unsigned long)memhdr_hash_table_size;h++)
  {
    for(m=memhdr_hash_table[h];m;m=m->next)
    {
      unsigned int e;
      struct memhdr *tmp;
      void **p=m->data;

      if( ! ((sizeof(void *)-1) & (long) p ))
      {
	if(m->size > 0)
	{
#ifdef __NT__
	  __try {
#endif
	    for(e=0;e<m->size/sizeof(void *);e++)
	      if((tmp=just_find_memhdr(p[e])))
		tmp->flags |= MEM_REFERENCED;
#ifdef __NT__
	  }
	  __except( 1 ) {
	    fprintf(stderr,"*** DMALLOC memory access error ***\n");
	    fprintf(stderr,"Failed to access this memory block:\n");
	    fprintf(stderr,"Location: %p, size=%ld, gc_generation=%d, flags=%d\n",
		    m->data,
		    m->size,
		    m->gc_generation,
		    m->flags);
	    dump_memhdr_locations(m, 0, 0);
	    fprintf(stderr,"-----------------------------------\n");
	  }
#endif
	}
      }
    }
  }

  memheader_references_located=1;
}

void search_all_memheaders_for_references(void)
{
  mt_lock(&debug_malloc_mutex);
  low_search_all_memheaders_for_references();
  mt_unlock(&debug_malloc_mutex);
}

void cleanup_memhdrs(void)
{
  unsigned long h;
  mt_lock(&debug_malloc_mutex);
  for(h=0;h<FREE_DELAY;h++)
  {
    void *p;
    if((p=blocks_to_free[h]))
    {
      struct memhdr *mh = find_memhdr(p);
      if(mh)
      {
	PIKE_MEM_RW_RANGE((char *) p - DEBUG_MALLOC_PAD,
			  ~mh->size + 2 * DEBUG_MALLOC_PAD);
	unregister_memhdr(mh,0);
	real_free( ((char *)p) - DEBUG_MALLOC_PAD );
      }else{
	fake_free(p);
      }
      blocks_to_free[h]=0;
    }
  }
  exiting=1;

  if(verbose_debug_exit)
  {
    int first=1;
    low_search_all_memheaders_for_references();

    for(h=0;h<(unsigned long)memhdr_hash_table_size;h++)
    {
      struct memhdr *m;
      for(m=memhdr_hash_table[h];m;m=m->next)
      {
	int referenced=0;
	struct memhdr *tmp;
	void *p=m->data;

	if(m->flags & MEM_IGNORE_LEAK) continue;

	mt_unlock(&debug_malloc_mutex);
	if(first)
	{
	  fprintf(stderr,"\n");
	  first=0;
	}

	if(m->flags & MEM_REFERENCED)
	  fprintf(stderr, "possibly referenced memory: (%p) %ld bytes\n",p, m->size);
	else
	  fprintf(stderr, "==LEAK==: (%p) %ld bytes\n",p, m->size);
	  
	if( 1 & (long) p )
	{
	  if( FD2PTR( PTR2FD(p) ) == p)
	  {
	    fprintf(stderr," Filedescriptor %ld\n", (long) PTR2FD(p));
	  }
	}else{
#ifdef PIKE_DEBUG
	  describe_something(p, attempt_to_identify(p, NULL), 0,2,8, NULL);
#endif
	}
	mt_lock(&debug_malloc_mutex);

	/* Now we must reassure 'm' */
	for(tmp=memhdr_hash_table[h];tmp;tmp=tmp->next)
	  if(m==tmp)
	    break;

	if(!tmp)
	{
	  fprintf(stderr,"**BZOT: Memory header was freed in mid-flight.\n");
	  fprintf(stderr,"**BZOT: Restarting hash bin, some entries might be duplicated.\n");
	  h--;
	  break;
	}
	
	find_references_to(p,0,0,0);
	dump_memhdr_locations(m, 0,0);
      }
    }


#ifdef DMALLOC_PROFILE
    {
      INT32 num,mem;
      long free_memhdr=0, ignored_memhdr=0, total_memhdr=0;
      
      fprintf(stderr,"add_location %d cache %d (%3.3f%%) new: %d (%3.3f%%)\n",
	      add_location_calls,
	      add_location_cache_hits,
	      add_location_cache_hits*100.0/add_location_calls,
	      add_location_new,
	      add_location_new*100.0/add_location_calls);
      fprintf(stderr,"           seek: %d  %3.7f  duplicates: %d\n",
	      add_location_seek,
	      ((double)add_location_seek)/(add_location_calls-add_location_cache_hits),
	      add_location_duplicate);

      count_memory_in_memhdrs(&num,&mem);
      fprintf(stderr,"memhdrs:  %8ld, %10ld bytes\n",(long)num,(long)mem);
      
      for(h=0;h<(unsigned long)memhdr_hash_table_size;h++)
      {
	struct memhdr *m;
	for(m=memhdr_hash_table[h];m;m=m->next)
	{
	  total_memhdr++;
	  if(m->flags & MEM_FREE)
	    free_memhdr++;
	  else if(m->flags & MEM_IGNORE_LEAK)
	    ignored_memhdr++;
	}
      }
      fprintf(stderr,"memhdrs, hashed %ld, free %ld, ignored %ld, other %ld\n",
	      total_memhdr,
	      free_memhdr,
	      ignored_memhdr,
	      total_memhdr - free_memhdr - ignored_memhdr);
    }
#endif

  }
  mt_unlock(&debug_malloc_mutex);
  mt_destroy(&debug_malloc_mutex);
}

#ifdef _REENTRANT
static void lock_da_lock(void)
{
  mt_lock(&debug_malloc_mutex);
}

static void unlock_da_lock(void)
{
  mt_unlock(&debug_malloc_mutex);
}
#endif

static void initialize_dmalloc(void)
{
  long e;
  static int initialized=0;
  if(!initialized)
  {
    initialized=1;
#ifdef DMALLOC_REMEMBER_LAST_LOCATION
    th_key_create(&dmalloc_last_seen_location, 0);
#endif
    init_memloc_blocks();
    init_memory_map_blocks();
    init_memory_map_entry_blocks();
    init_memhdr_hash();

    for(e=0;e<(long)NELEM(rndbuf);e++) rndbuf[e]= (rand() % 511) | 1;
    
#if DEBUG_MALLOC_PAD & 3
    fprintf(stderr,"DEBUG_MALLOC_PAD not dividable by four!\n");
    exit(99);
#endif
    
#ifdef _REENTRANT
#ifdef mt_init_recursive
    mt_init_recursive(&debug_malloc_mutex);
#else
    mt_init(&debug_malloc_mutex);
#endif

    /* NOTE: th_atfork() may be a simulated function, which
     *       utilizes callbacks. We thus need to initialize
     *       the callback blocks before we perform the call
     *       to th_atfork().
     */
    {
      extern void init_callback_blocks(void);
      init_callback_blocks();
    }

    th_atfork(lock_da_lock, unlock_da_lock,  unlock_da_lock);
#endif
#ifdef DMALLOC_USING_DLOPEN
    {
      real_free=dlsym(RTLD_NEXT,"free");
      real_realloc=dlsym(RTLD_NEXT,"realloc");
      real_malloc=dlsym(RTLD_NEXT,"malloc");
      real_calloc=dlsym(RTLD_NEXT,"calloc");
    }
#endif
  }
}

int main(int argc, char *argv[])
{
  extern int dbm_main(int, char **);
  initialize_dmalloc();
  return dbm_main(argc, argv);
}

void * debug_malloc_update_location(void *p,LOCATION location)
{
  if(p)
  {
    struct memhdr *mh;
    mt_lock(&debug_malloc_mutex);
#ifdef DMALLOC_REMEMBER_LAST_LOCATION
    th_setspecific(dmalloc_last_seen_location, location);
#endif
    if((mh=my_find_memhdr(p,0)))
      add_location(mh, location);

    mt_unlock(&debug_malloc_mutex);
  }
  return p;
}

void * debug_malloc_update_location_ptr(void *p,
					ptrdiff_t offset,
					LOCATION location)
{
  if(p)
    debug_malloc_update_location(*(void **)(((char *)p)+offset), location);
  return p;
}


/* another shared-string table... */
struct dmalloc_string
{
  struct dmalloc_string *next;
  unsigned long hval;
  char str[1];
};

static struct dmalloc_string *dstrhash[DSTRHSIZE];

static LOCATION low_dynamic_location(char type, const char *file, int line)
{
  struct dmalloc_string **prev, *str;
  int len=strlen(file);
  unsigned long h,hval=hashmem(file,len,64)+line;
  h=hval % DSTRHSIZE;

  mt_lock(&debug_malloc_mutex);

  for(prev = dstrhash + h; (str=*prev); prev = &str->next)
  {
    if(hval == str->hval &&
       str->str[len+1]==':' &&
       !MEMCMP(str->str+1, file, len) &&
       str->str[0]==type &&
       atoi(str->str+len+2) == line)
    {
      *prev=str->next;
      str->next=dstrhash[h];
      dstrhash[h]=str;
      break;
    }
  }
  
  if(!str)
  {
    str=malloc( sizeof(struct dmalloc_string) + len + 20);
    sprintf(str->str, "%c%s:%d", type, file, line);
    str->hval=hval;
    str->next=dstrhash[h];
    dstrhash[h]=str;
  }

  mt_unlock(&debug_malloc_mutex);

  return str->str;
}

LOCATION dynamic_location(const char *file, int line)
{
  return low_dynamic_location('D',file,line);
}


void * debug_malloc_name(void *p,const char *file, int line)
{
  if(p)
  {
    struct memhdr *mh;
    LOCATION loc=dynamic_location(file, line);

    mt_lock(&debug_malloc_mutex);
      
    if((mh=my_find_memhdr(p,0)))
      add_location(mh, loc);

    mt_unlock(&debug_malloc_mutex);
  }
  return p;
}

/*
 * This copies all dynamically assigned names from
 * one pointer to another. Used by clone() to copy
 * the name(s) of the program.
 */
int debug_malloc_copy_names(void *p, void *p2)
{
  int names=0;
  if(p)
  {
    struct memhdr *mh,*from;
    mt_lock(&debug_malloc_mutex);

    if((from=my_find_memhdr(p2,0)) && (mh=my_find_memhdr(p,0)))
    {
      struct memloc *l;
      for(l=from->locations;l;l=l->next)
      {
	if(LOCATION_IS_DYNAMIC(l->location))
	{
	  add_location(mh, l->location);
	  names++;
	}
      }
    }

    mt_unlock(&debug_malloc_mutex);
  }
  return names;
}

char *dmalloc_find_name(void *p)
{
  char *name=0;
  if(p)
  {
    struct memhdr *mh;
    mt_lock(&debug_malloc_mutex);

    if((mh=my_find_memhdr(p,0)))
    {
      struct memloc *l;
      for(l=mh->locations;l;l=l->next)
      {
	if(LOCATION_IS_DYNAMIC(l->location))
	{
	  name=LOCATION_NAME(l->location);
	  break;
	}
      }
    }

    mt_unlock(&debug_malloc_mutex);
  }
  return name;
}

int debug_malloc_touch_fd(int fd, LOCATION location)
{
  if(fd==-1) return fd;
  debug_malloc_update_location( FD2PTR(fd), location);
  return fd;
}

int debug_malloc_register_fd(int fd, LOCATION location)
{
  if(fd==-1) return fd;
  dmalloc_unregister( FD2PTR(fd), 1);
  dmalloc_register( FD2PTR(fd), 0, location);
  return fd;
}

int debug_malloc_close_fd(int fd, LOCATION location)
{
  if(fd==-1) return fd;
  dmalloc_mark_as_free( FD2PTR(fd), 1 );
  return fd;
}

void debug_malloc_dump_fd(int fd)
{
  fprintf(stderr,"DMALLOC dumping locations for fd %d\n",fd);
  if(fd==-1) return;
  debug_malloc_dump_references(FD2PTR(fd),2,0,0);
}

void reset_debug_malloc(void)
{
  unsigned long h;
  for(h=0;h<(unsigned long)memhdr_hash_table_size;h++)
  {
    struct memhdr *m;
    for(m=memhdr_hash_table[h];m;m=m->next)
    {
      struct memloc *l;
      for(l=m->locations;l;l=l->next)
      {
	l->times=0;
      }
    }
  }
}

struct memory_map
{
  char name[128];
  INT32 refs;
  struct memory_map *next;
  struct memory_map_entry *entries;
};

struct memory_map_entry
{
  struct memory_map_entry *next;
  char name[128];
  int offset;
  int size;
  int count;
  int recur_offset;
  struct memory_map *recur;
};

BLOCK_ALLOC_FILL_PAGES(memory_map, 8)
BLOCK_ALLOC_FILL_PAGES(memory_map_entry, 16)

void dmalloc_set_mmap(void *ptr, struct memory_map *m)
{
  m->refs++;
  debug_malloc_update_location(ptr, m->name+1);
}

void dmalloc_set_mmap_template(void *ptr, struct memory_map *m)
{
  m->refs++;
  debug_malloc_update_location(ptr,m->name);
}

void dmalloc_set_mmap_from_template(void *p, void *p2)
{
  int names=0;
  if(p)
  {
    struct memhdr *mh,*from;
    mt_lock(&debug_malloc_mutex);

    if((from=my_find_memhdr(p2,0)) && (mh=my_find_memhdr(p,0)))
    {
      struct memloc *l;
      for(l=from->locations;l;l=l->next)
      {
	if(l->location[0]=='T')
	{
	  add_location(mh, l->location+1);
	  names++;
	}
      }
    }

    mt_unlock(&debug_malloc_mutex);
  }
}

static void very_low_dmalloc_describe_location(struct memory_map *m,
					       int offset,
					       int indent)
{
  struct memory_map_entry *e;
  fprintf(stderr,"%*s ** In memory description %s:\n",indent,"",m->name+2);
  for(e=m->entries;e;e=e->next)
  {
    if(e->offset <= offset && e->offset + e->size * e->count > offset)
    {
      int num=(offset - e->offset)/e->size;
      int off=offset-e->size*num-e->offset;
      fprintf(stderr,"%*s    Found in member: %s[%d] + %d (offset=%d size=%d)\n",
	      indent,"",
	      e->name,
	      num,
	      off,
	      e->offset,
	      e->size);

      if(e->recur)
      {
	very_low_dmalloc_describe_location(e->recur,
					   off-e->recur_offset,
					   indent+2);
      }
    }
  }
}

static void low_dmalloc_describe_location(struct memhdr *mh, int offset, int indent)
{
  struct memloc *l;
  for(l=mh->locations;l;l=l->next)
  {
    if(l->location[0]=='M')
    {
      struct memory_map *m = (struct memory_map *)(l->location - 1);
      very_low_dmalloc_describe_location(m, offset, indent);
    }
  }
}

void dmalloc_describe_location(void *p, int offset, int indent)
{
  if(p)
  {
    struct memhdr *mh;

    if((mh=my_find_memhdr(p,0)))
      low_dmalloc_describe_location(mh, offset, indent);
  }
}

struct memory_map *dmalloc_alloc_mmap(char *name, int line)
{
  struct memory_map *m;
  mt_lock(&debug_malloc_mutex);
  m=alloc_memory_map();
  strncpy(m->name+2,name,sizeof(m->name)-2);
  m->name[sizeof(m->name)-1]=0;
  m->name[0]='T';
  m->name[1]='M';
  m->refs=0;

  if(strlen(m->name)+12<sizeof(m->name))
    sprintf(m->name+strlen(m->name),":%d",line);

  m->entries=0;
  mt_unlock(&debug_malloc_mutex);
  return m;
}

void dmalloc_add_mmap_entry(struct memory_map *m,
			    char *name,
			    int offset,
			    int size,
			    int count,
			    struct memory_map *recur,
			    int recur_offset)
{
  struct memory_map_entry *e;
  mt_lock(&debug_malloc_mutex);
  e=alloc_memory_map_entry();
  strncpy(e->name,name,sizeof(e->name));
  e->name[sizeof(e->name)-1]=0;
  e->offset=offset;
  e->size=size;
  e->count=count?count:1;
  e->next=m->entries;
  e->recur=recur;
  e->recur_offset=recur_offset;
  m->entries=e;
  mt_unlock(&debug_malloc_mutex);
}

int dmalloc_is_invalid_memory_block(void *block)
{
  struct memhdr *mh=my_find_memhdr(block,1);
  if(!mh) return -1; /* no such known block */
  if(mh->size < 0) return -2; /* block has been freed */
  return 0; /* block is valid */
}

void cleanup_debug_malloc(void)
{
  size_t i;

  free_all_memloc_blocks();
  exit_memhdr_hash();
  free_all_memory_map_blocks();
  free_all_memory_map_entry_blocks();

  for (i = 0; i < DSTRHSIZE; i++) {
    struct dmalloc_string *str = dstrhash[i], *next;
    while (str) {
      next = str->next;
      free(str);
      str = next;
    }
  }
}

#endif
