/*
** $Id: ldump.c,v 2.17.1.1 2013/04/12 18:48:47 roberto Exp $
** save precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#include <stddef.h>
#include <limits.h>
#include <math.h>
#include <float.h>

#define ldump_c
#define LUA_CORE

#include "lua.h"

#include "lobject.h"
#include "lstate.h"
#include "lundump.h"
#include "ldo.h"

extern const lua_Number LUA_NAN;
extern const lua_Number LUA_INFINITY;

/* MSVC does not have the C99 trunc() function. */
#ifdef _MSC_VER
static double trunc(double x)
{
	return (x > 0 ? floor(x) : ceil(x));
}
#endif

typedef struct {
 lua_State* L;
 lua_Writer writer;
 void* data;
 int strip;
 int status;
} DumpState;

static l_noret error(DumpState* D, const char* why)
{
 luaO_pushfstring(D->L,"unable to write precompiled chunk: %s",why);
 luaD_throw(D->L,LUA_ERRSYNTAX);
}

#define DumpMem(b,n,size,D)	DumpBlock(b,(n)*(size),D)
#define DumpVar(x,D)		DumpMem(&x,1,sizeof(x),D)

static void DumpBlock(const void* b, size_t size, DumpState* D)
{
 if (D->status==0)
 {
  lua_unlock(D->L);
  D->status=(*D->writer)(D->L,b,size,D->data);
  lua_lock(D->L);
 }
}

static void DumpChar(int y, DumpState* D)
{
 char x=(char)y;
 DumpVar(x,D);
}

static void DumpInt(int x, DumpState* D)
{
 unsigned char y[4];
 if(x<0)
   error(D,"negative int value");
 y[0]=(unsigned char)(x&0xFF);
 y[1]=(unsigned char)((x>>8)&0xFF);
 y[2]=(unsigned char)((x>>16)&0xFF);
 y[3]=(unsigned char)((x>>24)&0xFF);
 DumpMem(y,1,4,D);
}

#if CHAR_BIT != 8
#error currently supported only CHAR_BIT = 8
#endif

#if FLT_RADIX != 2
#error currently supported only FLT_RADIX = 2
#endif

static void DumpNumber(lua_Number x, DumpState* D)
{
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
 double y=(double)x,m;
 unsigned char z[8];
 long long im;
 int ie;
 if (luai_numisnan(NULL,x)) { DumpChar(0,D); /* NaN */ return; }
 else if (x == LUA_INFINITY) { DumpChar(1,D); /* +inf */ return; }
 else if (x == -LUA_INFINITY) { DumpChar(2,D); /* -inf */ return; }
 else if (x == 0) { DumpChar(3,D); /* 0 */ return; }
 /* Split double into normalized mantissa (range: (-1, -0.5], 0, [+0.5, +1)) and base-2 exponent */
 m = frexp(y, &ie); /* y = m * 2^ie exactly for FLT_RADIX=2, frexp() can't fail */
 /* Extract most significant 53 bits of mantissa as integer */
 m = ldexp(m, 53); /* can't overflow because DBL_MAX_10_EXP >= 37 equivalent to DBL_MAX_2_EXP >= 122 */
 im = (long long)trunc(m);    /* exact unless DBL_MANT_DIG > 53 */
 /* If the exponent is too small or too big, reduce the number to 0 or +/- infinity */
 if (ie>0x7FFE)
 {
   if (im<0) DumpChar(2,D); /* -inf */ else DumpChar(1,D); /* +inf */
   return;
 }
 else if (ie<-0x7FFE) { DumpChar(3,D); /* 0 */ return; }
 DumpChar(4,D); /* encoded */
 /* Store im as signed 64-bit little-endian integer */
 z[0]=(unsigned char)(im&0xFF);
 z[1]=(unsigned char)((im>>8)&0xFF);
 z[2]=(unsigned char)((im>>16)&0xFF);
 z[3]=(unsigned char)((im>>24)&0xFF);
 z[4]=(unsigned char)((im>>32)&0xFF);
 z[5]=(unsigned char)((im>>40)&0xFF);
 z[6]=(unsigned char)((im>>48)&0xFF);
 z[7]=(unsigned char)((im>>56)&0xFF);
 DumpMem(z,1,8,D);
 /* Store ie as signed 16-bit little-endian integer */
 z[0]=(unsigned char)(ie&0xFF);
 z[1]=(unsigned char)((ie>>8)&0xFF);
 DumpMem(z,1,2,D);
}

static void DumpVector(const void* b, int n, size_t size, DumpState* D)
{
 DumpInt(n,D);
 DumpMem(b,((size_t)n),size,D);
}

static void DumpUInt(lu_int32 x, DumpState* D)
{
 unsigned char y[4];
 y[0]=(unsigned char)(x&0xFF);
 y[1]=(unsigned char)((x>>8)&0xFF);
 y[2]=(unsigned char)((x>>16)&0xFF);
 y[3]=(unsigned char)((x>>24)&0xFF);
 DumpMem(y,1,4,D);
}

static void DumpString(const TString* s, DumpState* D)
{
 if (s==NULL)
 {
  lu_int32 size=0;
  DumpUInt(size,D);
 }
 else
 {
  size_t size=s->tsv.len+1;		/* include trailing '\0' */
  if (size>0xFFFFFFFFUL)
    error(D,"string is too long");
  DumpUInt((lu_int32)size,D);
  DumpBlock(getstr(s),size*sizeof(char),D);
 }
}

#define DumpCode(f,D)	 DumpVector(f->code,f->sizecode,sizeof(lua_Instruction),D)

static void DumpFunction(const Proto* f, DumpState* D);

static void DumpConstants(const Proto* f, DumpState* D)
{
 int i,n=f->sizek;
 DumpInt(n,D);
 for (i=0; i<n; i++)
 {
  const TValue* o=&f->k[i];
  DumpChar(ttypenv(o),D);
  switch (ttypenv(o))
  {
   case LUA_TNIL:
	break;
   case LUA_TBOOLEAN:
	DumpChar(bvalue(o),D);
	break;
   case LUA_TNUMBER:
	DumpNumber(nvalue(o),D);
	break;
   case LUA_TSTRING:
	DumpString(rawtsvalue(o),D);
	break;
    default: lua_assert(0);
  }
 }
 n=f->sizep;
 DumpInt(n,D);
 for (i=0; i<n; i++) DumpFunction(f->p[i],D);
}

static void DumpUpvalues(const Proto* f, DumpState* D)
{
 int i,n=f->sizeupvalues;
 DumpInt(n,D);
 for (i=0; i<n; i++)
 {
  DumpChar(f->upvalues[i].instack,D);
  DumpChar(f->upvalues[i].idx,D);
 }
}

static void DumpDebug(const Proto* f, DumpState* D)
{
 int i,n;
 DumpString((D->strip) ? NULL : f->source,D);
 n= (D->strip) ? 0 : f->sizelineinfo;
 DumpInt(n,D);
 for (i=0; i<n; i++) DumpInt(f->lineinfo[i],D);
 n= (D->strip) ? 0 : f->sizelocvars;
 DumpInt(n,D);
 for (i=0; i<n; i++)
 {
  DumpString(f->locvars[i].varname,D);
  DumpInt(f->locvars[i].startpc,D);
  DumpInt(f->locvars[i].endpc,D);
 }
 n= (D->strip) ? 0 : f->sizeupvalues;
 DumpInt(n,D);
 for (i=0; i<n; i++) DumpString(f->upvalues[i].name,D);
}

static void DumpFunction(const Proto* f, DumpState* D)
{
 int i;
 DumpInt(f->linedefined,D);
 DumpInt(f->lastlinedefined,D);
 DumpChar(f->numparams,D);
 DumpChar(f->is_vararg,D);
 DumpChar(f->maxstacksize,D);
 DumpInt(f->sizecode,D);
 for (i=0; i<f->sizecode; i++) DumpUInt(f->code[i],D);
 DumpConstants(f,D);
 DumpUpvalues(f,D);
 DumpDebug(f,D);
}

static void DumpHeader(DumpState* D)
{
 lu_byte h[LUAC_HEADERSIZE];
 luaU_header(h);
 DumpBlock(h,LUAC_HEADERSIZE,D);
}

/*
** dump Lua function as precompiled chunk
*/
int luaU_dump (lua_State* L, const Proto* f, lua_Writer w, void* data, int strip)
{
 DumpState D;
 D.L=L;
 D.writer=w;
 D.data=data;
 D.strip=strip;
 D.status=0;
 DumpHeader(&D);
 DumpFunction(f,&D);
 (void)&DumpVector;
 return D.status;
}
