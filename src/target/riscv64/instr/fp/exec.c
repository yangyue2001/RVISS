#include <cpu/cpu.h>
#include <memory/vaddr.h>
#include "csr.h"
#include "encoding.h"
#include "fp.h"

bool riscv_fp_enabled(bool double_precision) {
#ifdef CONFIG_RV_F
  if(!(cpu.misa&MISA_F) || !(cpu.mstatus&MSTATUS_FS)) return false;
#ifdef CONFIG_RV_D
  return !double_precision || (cpu.misa&MISA_D);
#else
  return !double_precision;
#endif
#else
  (void)double_precision; return false;
#endif
}

void riscv_fp_dirty(void) {
  cpu.mstatus=csr_status_value(cpu.mstatus|MSTATUS_FS_DIRTY);
}

#ifdef CONFIG_RV_F
#include "platform.h"
#include "softfloat.h"

#define CANON_S UINT32_C(0x7fc00000)
#define CANON_D UINT64_C(0x7ff8000000000000)

static word_t source(unsigned reg, bool d) {
  word_t value=cpu.fpr[reg];
  if(d) return value;
  if((cpu.misa&MISA_D) && (value>>32)!=UINT32_MAX) return CANON_S;
  return (uint32_t)value;
}
static void write_fpr(Decode *s, unsigned reg, word_t value, bool d) {
  cpu.fpr[reg]=d?value:UINT64_C(0xffffffff00000000)|(uint32_t)value;
  s->fp_register_write=true;
  riscv_fp_dirty();
}
static void write_gpr(Decode *s, unsigned reg, word_t value) {
  if(reg) { cpu.gpr[reg]=value; s->dest.preg=&cpu.gpr[reg]; }
}
static bool fp_is_nan(word_t bits, bool d) {
  word_t exponent=d?UINT64_C(0x7ff0000000000000):UINT64_C(0x7f800000);
  word_t fraction=d?UINT64_C(0x000fffffffffffff):UINT64_C(0x007fffff);
  return (bits&exponent)==exponent && (bits&fraction);
}
static bool snan(word_t bits, bool d) {
  return fp_is_nan(bits,d) && !(bits&(d?UINT64_C(0x0008000000000000):UINT64_C(0x00400000)));
}
static word_t classify(word_t bits, bool d) {
  unsigned frac=d?52:23,exp=d?11:8;
  bool sign=(bits>>(frac+exp))&1;
  word_t exponent=(bits>>frac)&((1u<<exp)-1), fraction=bits&((UINT64_C(1)<<frac)-1);
  if(exponent==((1u<<exp)-1)) return 1u<<(fraction?(snan(bits,d)?8:9):(sign?0:7));
  if(!exponent) return 1u<<(fraction?(sign?2:5):(sign?3:4));
  return 1u<<(sign?1:6);
}
static bool less(word_t a, word_t b, bool d) {
  return d?f64_lt_quiet((float64_t){a},(float64_t){b}):
           f32_lt_quiet((float32_t){a},(float32_t){b});
}
static word_t minmax(word_t a, word_t b, bool d, bool max) {
  bool na=fp_is_nan(a,d),nb=fp_is_nan(b,d);
  if(snan(a,d)||snan(b,d)) softfloat_exceptionFlags|=softfloat_flag_invalid;
  if(na&&nb) return d?CANON_D:CANON_S;
  if(na) return b;
  if(nb) return a;
  word_t sign=d?UINT64_C(1)<<63:UINT64_C(1)<<31;
  if(!(a&~sign) && !(b&~sign)) return max?a&b:a|b;
  return less(a,b,d)!=max?a:b;
}
static bool rounding(Decode *s, unsigned rm) {
  if(rm==7) rm=(cpu.fcsr>>5)&7;
  if(rm>4) { riscv_exec_trap(s,EX_II,s->instr); return false; }
  softfloat_roundingMode=rm;
  softfloat_detectTininess=softfloat_tininess_afterRounding;
  return true;
}
static bool memory(Decode *s, unsigned opcode, bool d) {
  unsigned i=s->encoding,base=RS1(i),reg=opcode==7?RD(i):RS2(i);
  word_t address=cpu.gpr[base]+(opcode==7?IMM_I(i):IMM_S(i));
  MemAccess a=opcode==7?vaddr_read(address,d?8:4):vaddr_write(address,d?8:4,cpu.fpr[reg]);
  s->memory=a;
  if(a.status!=MEM_OK) {
    if(a.status==MEM_HOST_ERROR) s->host_error=true;
    else riscv_exec_trap(s,a.cause,a.tval);
    return false;
  }
  if(opcode==7) write_fpr(s,reg,a.data,d);
  return true;
}

void riscv_fp_execute(Decode *s) {
  uint32_t i=s->encoding;
  unsigned opcode=i&127,rd=RD(i),rs1=RS1(i),rs2=RS2(i),rm=BITS(i,14,12),fn=i>>27;
  bool d=opcode==7||opcode==0x27?rm==3:BIT(i,25);
  bool cross=opcode==0x53 && fn==8;
  if(!riscv_fp_enabled(d||cross)) { riscv_exec_trap(s,EX_II,s->instr); return; }
  if(opcode==7 || opcode==0x27) { memory(s,opcode,d); return; }
  softfloat_exceptionFlags=0;
  word_t a=source(rs1,d),b=source(rs2,d),value=0;
  bool to_gpr=false;
  if(opcode!=0x53) {
    if(!rounding(s,rm)) return;
    word_t c=source(BITS(i,31,27),d),sign=d?UINT64_C(1)<<63:UINT64_C(1)<<31;
    if(opcode==0x4b || opcode==0x4f) a^=sign;
    if(opcode==0x47 || opcode==0x4f) c^=sign;
    value=d?f64_mulAdd((float64_t){a},(float64_t){b},(float64_t){c}).v:
            f32_mulAdd((float32_t){a},(float32_t){b},(float32_t){c}).v;
  } else {
    switch(fn) {
      case 0: case 1: case 2: case 3: case 11:
        if(!rounding(s,rm)) return;
        if(d) {
          float64_t x={a},y={b};
          value=fn==0?f64_add(x,y).v:fn==1?f64_sub(x,y).v:fn==2?f64_mul(x,y).v:
                fn==3?f64_div(x,y).v:f64_sqrt(x).v;
        } else {
          float32_t x={a},y={b};
          value=fn==0?f32_add(x,y).v:fn==1?f32_sub(x,y).v:fn==2?f32_mul(x,y).v:
                fn==3?f32_div(x,y).v:f32_sqrt(x).v;
        }
        break;
      case 4: { /* sign injection preserves payload for correctly boxed inputs */
        word_t sign=d?UINT64_C(1)<<63:UINT64_C(1)<<31;
        word_t selected=rm==0?b:rm==1?~b:a^b;
        value=(a&~sign)|(selected&sign); break;
      }
      case 5: value=minmax(a,b,d,rm==1); break;
      case 8:
        if(!rounding(s,rm)) return;
        value=d?f32_to_f64((float32_t){source(rs1,false)}).v:
                f64_to_f32((float64_t){source(rs1,true)}).v; break;
      case 20:
        to_gpr=true;
        if(d) value=rm==0?f64_le((float64_t){a},(float64_t){b}):
                    rm==1?f64_lt((float64_t){a},(float64_t){b}):f64_eq((float64_t){a},(float64_t){b});
        else value=rm==0?f32_le((float32_t){a},(float32_t){b}):
                      rm==1?f32_lt((float32_t){a},(float32_t){b}):f32_eq((float32_t){a},(float32_t){b});
        break;
      case 24:
        if(!rounding(s,rm)) return;
        to_gpr=true;
        if(d) {
          float64_t x={a};
          value=rs2==0?(word_t)f64_to_i32(x,softfloat_roundingMode,true):
                rs2==1?f64_to_ui32(x,softfloat_roundingMode,true):
                rs2==2?(word_t)f64_to_i64(x,softfloat_roundingMode,true):f64_to_ui64(x,softfloat_roundingMode,true);
        } else {
          float32_t x={a};
          value=rs2==0?(word_t)f32_to_i32(x,softfloat_roundingMode,true):
                rs2==1?f32_to_ui32(x,softfloat_roundingMode,true):
                rs2==2?(word_t)f32_to_i64(x,softfloat_roundingMode,true):f32_to_ui64(x,softfloat_roundingMode,true);
        }
        if(rs2<2) value=(word_t)(int64_t)(int32_t)value;
        break;
      case 26:
        if(!rounding(s,rm)) return;
        a=cpu.gpr[rs1];
        if(d) value=rs2==0?i32_to_f64((int32_t)a).v:rs2==1?ui32_to_f64((uint32_t)a).v:
                    rs2==2?i64_to_f64((int64_t)a).v:ui64_to_f64(a).v;
        else value=rs2==0?i32_to_f32((int32_t)a).v:rs2==1?ui32_to_f32((uint32_t)a).v:
                      rs2==2?i64_to_f32((int64_t)a).v:ui64_to_f32(a).v;
        break;
      case 28:
        to_gpr=true;
        value=rm==1?classify(a,d):d?cpu.fpr[rs1]:(word_t)(int64_t)(int32_t)cpu.fpr[rs1];
        break;
      case 30: value=cpu.gpr[rs1]; break;
      default: riscv_exec_trap(s,EX_II,s->instr); return;
    }
  }
  if(to_gpr) write_gpr(s,rd,value); else write_fpr(s,rd,value,d);
  if(softfloat_exceptionFlags) {
    cpu.fcsr|=softfloat_exceptionFlags&31; riscv_fp_dirty();
  }
  softfloat_exceptionFlags=0;
}

#define INSTR(_n,_p,_f) void concat(exec_,_n)(Decode *s) { riscv_fp_execute(s); }
#include "instr.def"
#undef INSTR
#endif
