/*
** $Id: lundump.c,v 2.22.1.1 2013/04/12 18:48:47 roberto Exp $
** load precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#include <string.h>
#include <limits.h>
#include <math.h>
#include <float.h>

#define lundump_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstring.h"
#include "lundump.h"
#include "lzio.h"

extern const lua_Number LUA_NAN;
extern const lua_Number LUA_INFINITY;

typedef struct {
 lua_State* L;
 ZIO* Z;
 Mbuffer* b;
 const char* name;
} LoadState;

static l_noret error(LoadState* S, const char* why)
{
 luaO_pushfstring(S->L,"%s: %s precompiled chunk",S->name,why);
 luaD_throw(S->L,LUA_ERRSYNTAX);
}

#define LoadMem(S,b,n,size)	LoadBlock(S,b,(n)*(size))
#define LoadByte(S)		(lu_byte)LoadChar(S)
#define LoadVar(S,x)		LoadMem(S,&x,1,sizeof(x))
#define LoadVector(S,b,n,size)	LoadMem(S,b,n,size)

#if !defined(luai_verifycode)
#define luai_verifycode(L,b,f)	/* empty */
#endif

static void LoadBlock(LoadState* S, void* b, size_t size)
{
 if (luaZ_read(S->Z,b,size)!=0) error(S,"truncated");
}

static int LoadChar(LoadState* S)
{
 char x;
 LoadVar(S,x);
 return x;
}

static lu_int32 LoadUInt(LoadState* S)
{
 lu_int32 x;
 unsigned char y[4];
 LoadMem(S,y,1,4);
 x=((lu_int32)y[0])|((lu_int32)y[1]<<8)|((lu_int32)y[2]<<16)|((lu_int32)y[3]<<24);
 return x;
}

static int LoadInt(LoadState* S)
{
 int x;
 lu_int32 ui=LoadUInt(S);
 if (ui<=0x7FFFFFFFUL)
 {
   if (ui>INT_MAX)
     error(S,"int value greater than INT_MAX in");
   x=(int)ui;
 }
 else
   error(S,"negative int value in");
 return x;
}

#if CHAR_BIT != 8
#error currently supported only CHAR_BIT = 8
#endif

#if FLT_RADIX != 2
#error currently supported only FLT_RADIX = 2
#endif

static lua_Number LoadNumber(LoadState* S)
{
 lua_Number x;
 /*
   http://stackoverflow.com/questions/14954088/how-do-i-handle-byte-order-differences-when-reading-writing-floating-point-types/14955046#14955046

   10-byte little-endian serialized format for double:
   - normalized mantissa stored as 64-bit (8-byte) signed integer:
       negative range: (-2^53, -2^52]
       zero: 0
       positive range: [+2^52, +2^53)
   - 16-bit (2-byte) signed exponent:
       range: [-0x7FFE, +0x7FFE]

   Represented value = mantissa * 2^(exponent - 53)

   Special cases:
   - +infinity: mantissa = 0x7FFFFFFFFFFFFFFF, exp = 0x7FFF
   - -infinity: mantissa = 0x8000000000000000, exp = 0x7FFF
   - NaN:       mantissa = 0x0000000000000000, exp = 0x7FFF
   - +/-0:      only one zero supported
 */
 unsigned char y[8];
 unsigned long long uim;
 long long im;
 unsigned int uie;
 int ie, maxe;
 int negative = 0;
 double m;
 y[0]=LoadByte(S);
 if (y[0]==0) return LUA_NAN;
 else if (y[0]==1) return LUA_INFINITY;
 else if (y[0]==2) return -LUA_INFINITY;
 else if (y[0]==3) return (lua_Number)0;
 else if (y[0]!=4) error(S,"invalid floating-point number in");
 LoadMem(S,y,1,8);
 /* Load im as signed 64-bit little-endian integer */
 uim=y[0]|((unsigned long long)y[1]<<8)|((unsigned long long)y[2]<<16)|((unsigned long long)y[3]<<24)
   |((unsigned long long)y[4]<<32)|((unsigned long long)y[5]<<40)|((unsigned long long)y[6]<<48)
   |((unsigned long long)y[7]<<56);
 if (uim<=0x7FFFFFFFFFFFFFFFULL)
   im=(long long)uim;
  else
   im=(long long)(uim-0x7FFFFFFFFFFFFFFFULL-1)-0x7FFFFFFFFFFFFFFFLL-1;
 /* Obtain the absolute value of the mantissa, make sure it's */
 /* normalized and fits into 53 bits, else the input is invalid */
 if (im>0)
 {
   if (im<(1LL<<52)||im>=(1LL<<53)) error(S,"invalid floating-point mantissa in");
 }
 else if (im<0)
 {
   if (im>-(1LL<<52)||im<=-(1LL<<53)) error(S,"invalid floating-point mantissa in");
   negative=1;
   im=-im;
 }
 /* Load ie as signed 16-bit little-endian integer */
 LoadMem(S,y,1,2);
 uie=y[0]|((unsigned)y[1]<<8);
 if (uie<=0x7FFF)
   ie=(int)uie;
 else
   ie=(int)(uie-0x7FFF-1)-0x7FFF-1;
 /* If DBL_MANT_DIG < 53, truncate the mantissa */
 im>>=(53>DBL_MANT_DIG)?(53-DBL_MANT_DIG):0;
 /* --- */
 m = (double)im;
 m = ldexp(m, (53>DBL_MANT_DIG)?-DBL_MANT_DIG:-53); /* can't overflow */
            /* because DBL_MAX_10_EXP >= 37 equivalent to DBL_MAX_2_EXP >= 122 */
 /* Find out the maximum base-2 exponent and if ours is greater, return +/- infinity */
 frexp(DBL_MAX, &maxe);
 if (ie>maxe)
   m = LUA_INFINITY;
  else
   m = ldexp(m, ie); /* underflow may cause a floating-point exception */
 x=(lua_Number)(negative?-m:m);
 return x;
}

static TString* LoadString(LoadState* S)
{
 size_t size;
 size=LoadUInt(S);
 if (size==0)
  return NULL;
 else
 {
  char* s=luaZ_openspace(S->L,S->b,size);
  LoadBlock(S,s,size*sizeof(char));
  return luaS_newlstr(S->L,s,size-1);		/* remove trailing '\0' */
 }
}

static void LoadCode(LoadState* S, Proto* f)
{
 int i;
 int n=LoadInt(S);
 f->code=luaM_newvector(S->L,((size_t)n),Instruction);
 f->sizecode=n;
 for (i=0; i<n; i++)
   f->code[i]=(Instruction)LoadUInt(S);
}

static void LoadFunction(LoadState* S, Proto* f);

static void LoadConstants(LoadState* S, Proto* f)
{
 int i,n;
 n=LoadInt(S);
 f->k=luaM_newvector(S->L,((size_t)n),TValue);
 f->sizek=n;
 for (i=0; i<n; i++) setnilvalue(&f->k[i]);
 for (i=0; i<n; i++)
 {
  TValue* o=&f->k[i];
  int t=LoadChar(S);
  switch (t)
  {
   case LUA_TNIL:
	setnilvalue(o);
	break;
   case LUA_TBOOLEAN:
	setbvalue(o,LoadChar(S));
	break;
   case LUA_TNUMBER:
	setnvalue(o,LoadNumber(S));
	break;
   case LUA_TSTRING:
	setsvalue2n(S->L,o,LoadString(S));
	break;
    default: lua_assert(0);
  }
 }
 n=LoadInt(S);
 f->p=luaM_newvector(S->L,((size_t)n),Proto*);
 f->sizep=n;
 for (i=0; i<n; i++) f->p[i]=NULL;
 for (i=0; i<n; i++)
 {
  f->p[i]=luaF_newproto(S->L);
  LoadFunction(S,f->p[i]);
 }
}

static void LoadUpvalues(LoadState* S, Proto* f)
{
 int i,n;
 n=LoadInt(S);
 f->upvalues=luaM_newvector(S->L,((size_t)n),Upvaldesc);
 f->sizeupvalues=n;
 for (i=0; i<n; i++) f->upvalues[i].name=NULL;
 for (i=0; i<n; i++)
 {
  f->upvalues[i].instack=LoadByte(S);
  f->upvalues[i].idx=LoadByte(S);
 }
}

static void LoadDebug(LoadState* S, Proto* f)
{
 int i,n;
 f->source=LoadString(S);
 n=LoadInt(S);
 f->lineinfo=luaM_newvector(S->L,((size_t)n),int);
 f->sizelineinfo=n;
 for (i=0; i<n; i++)
   f->lineinfo[i]=LoadInt(S);
 n=LoadInt(S);
 f->locvars=luaM_newvector(S->L,((size_t)n),LocVar);
 f->sizelocvars=n;
 for (i=0; i<n; i++) f->locvars[i].varname=NULL;
 for (i=0; i<n; i++)
 {
  f->locvars[i].varname=LoadString(S);
  f->locvars[i].startpc=LoadInt(S);
  f->locvars[i].endpc=LoadInt(S);
 }
 n=LoadInt(S);
 for (i=0; i<n; i++) f->upvalues[i].name=LoadString(S);
}

static void LoadFunction(LoadState* S, Proto* f)
{
 f->linedefined=LoadInt(S);
 f->lastlinedefined=LoadInt(S);
 f->numparams=LoadByte(S);
 f->is_vararg=LoadByte(S);
 f->maxstacksize=LoadByte(S);
 LoadCode(S,f);
 LoadConstants(S,f);
 LoadUpvalues(S,f);
 LoadDebug(S,f);
}

/* the code below must be consistent with the code in luaU_header */
#define N0	LUAC_HEADERSIZE
#define N1	(sizeof(LUA_SIGNATURE)-sizeof(char))
#define N2	N1+2
#define N3	N2+6

static void LoadHeader(LoadState* S)
{
 lu_byte h[LUAC_HEADERSIZE];
 lu_byte s[LUAC_HEADERSIZE];
 luaU_header(h);
 memcpy(s,h,sizeof(char));			/* first char already read */
 LoadBlock(S,s+sizeof(char),LUAC_HEADERSIZE-sizeof(char));
 if (memcmp(h,s,N0)==0) return;
 if (memcmp(h,s,N1)!=0) error(S,"not a");
 error(S,"corrupted");
}

/*
** load precompiled chunk
*/
Closure* luaU_undump (lua_State* L, ZIO* Z, Mbuffer* buff, const char* name)
{
 LoadState S;
 Closure* cl;
 if (*name=='@' || *name=='=')
  S.name=name+1;
 else if (*name==LUA_SIGNATURE[0])
  S.name="binary string";
 else
  S.name=name;
 S.L=L;
 S.Z=Z;
 S.b=buff;
 LoadHeader(&S);
 cl=luaF_newLclosure(L,1);
 setclLvalue(L,L->top,cl); incr_top(L);
 cl->l.p=luaF_newproto(L);
 LoadFunction(&S,cl->l.p);
 if (cl->l.p->sizeupvalues != 1)
 {
  Proto* p=cl->l.p;
  cl=luaF_newLclosure(L,cl->l.p->sizeupvalues);
  cl->l.p=p;
  setclLvalue(L,L->top-1,cl);
 }
 luai_verifycode(L,buff,cl->l.p);
 return cl;
}

#define MYINT(s)	(s[0]-'0')
#define VERSION		MYINT(LUA_VERSION_MAJOR)*16+MYINT(LUA_VERSION_MINOR)
#define FORMAT		0		/* this is the official format */

/*
* make header for precompiled chunks
* if you change the code below be sure to update LoadHeader and FORMAT above
* and LUAC_HEADERSIZE in lundump.h
*/
void luaU_header (lu_byte* h)
{
 memcpy(h,LUA_SIGNATURE,sizeof(LUA_SIGNATURE)-sizeof(char));
 h+=sizeof(LUA_SIGNATURE)-sizeof(char);
 memcpy(h,LUAC_TAIL,sizeof(LUAC_TAIL)-sizeof(char));
}
