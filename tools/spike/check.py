#!/usr/bin/env python3
"""独立进程逐事件比较；首次差异保存可重放输入与双方近期事件。"""
from pathlib import Path
import argparse
import hashlib
import itertools
import json
import random
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
BASE = 0x80000000
ISA = 'rv64i_zicsr_zifencei'
OUT = ROOT / 'build/difftest'


def first_difference(dut, reference):
    for index, (actual, expected) in enumerate(zip(dut, reference)):
        if actual != expected:
            fields = [key for key in sorted(set(actual) | set(expected))
                      if actual.get(key) != expected.get(key)]
            return index, fields
    if len(dut) != len(reference):
        return min(len(dut), len(reference)), ['event_count']
    return None


def case_text(words, steps=1, regs=None, pc=BASE, priv=3, size=4096, extra_mem=None, isa=ISA):
    handler = BASE + size - 8 if size >= 264 else BASE + 256
    lines = [f'isa {isa}', f'base {BASE:#x}', f'size {size}',
             f'pc {pc:#x}', f'priv {priv}', f'steps {steps}',
             'csr 768 0xa00000000', f'csr 773 {handler:#x}']
    for index, value in (regs or {}).items():
        lines.append(f'gpr {index} {value & ((1<<64)-1):#x}')
    for address, width, value in [(BASE+4*i,4,w) for i,w in enumerate(words)]:
        lines.append(f'mem {address:#x} {width} {value:#x}')
    if size >= 264:
        lines.extend([f'mem {handler:#x} 4 0x00000013', f'mem {handler+4:#x} 4 0x0000006f'])
    for address, width, value in (extra_mem or []):
        lines.append(f'mem {address:#x} {width} {value:#x}')
    return '\n'.join(lines) + '\n'


def asm_case(name, source, steps, regs=None, isa=ISA):
    assembly, elf, binary = [OUT / (name + suffix) for suffix in ['.S', '.elf', '.bin']]
    assembly.write_text('.option norvc\n.section .text.start,"ax"\n.globl _start\n_start:\n' + source)
    subprocess.run(['riscv64-linux-gnu-gcc', '-nostdlib', '-nostartfiles', '-no-pie',
                    '-march=' + isa, '-mabi=lp64', '-mcmodel=medany', '-Wl,--build-id=none',
                    '-Wl,-T' + str(ROOT / 'tests/smoke/link.ld'), '-o', str(elf), str(assembly)], check=True)
    subprocess.run(['riscv64-linux-gnu-objcopy', '-O', 'binary', str(elf), str(binary)], check=True)
    data = binary.read_bytes()
    mem = [(BASE+i, 1, byte) for i, byte in enumerate(data)]
    return case_text([], steps, regs, extra_mem=mem, isa=isa)


def cases():
    yield 'two-adds', case_text([0x00700293, 0xffd28313], 2)
    arithmetic = '''li x1,-1
li x2,63
add x3,x1,x2
sub x4,x2,x1
sll x5,x1,x2
slt x6,x1,x2
sltu x7,x1,x2
xor x8,x1,x2
srl x9,x1,x2
sra x10,x1,x2
or x11,x1,x2
and x12,x1,x2
addi x13,x1,-2048
slti x14,x1,0
sltiu x15,x1,-1
xori x16,x1,-1
ori x17,x2,-2048
andi x18,x1,2047
slli x19,x1,63
srli x20,x1,63
srai x21,x1,63
lui x22,0x80000
auipc x23,0
addw x24,x1,x2
subw x25,x2,x1
sllw x26,x1,x2
srlw x27,x1,x2
sraw x28,x1,x2
addiw x29,x1,-2048
slliw x30,x1,31
srliw x31,x1,31
sraiw x0,x1,31
fence
fence.i
1: j 1b
'''
    yield 'integer-groups', asm_case('integer-groups', arithmetic, 40)
    control = '''beq x1,x2,1f
bne x1,x2,2f
1: addi x3,x0,99
2: blt x1,x2,3f
addi x3,x0,98
3: bge x2,x1,4f
addi x3,x0,97
4: bltu x2,x1,5f
addi x3,x0,96
5: bgeu x1,x2,6f
addi x3,x0,95
6: jal x4,7f
addi x3,x0,94
7: la x5,8f
addi x5,x5,1
jalr x5,0(x5)
addi x3,x0,93
8: addi x3,x0,42
9: j 9b
'''
    yield 'control', asm_case('control', control, 20, {1:(1<<64)-1,2:1})
    memory = '''lb x1,0(x6)
lh x2,0(x6)
lw x3,0(x6)
ld x4,0(x6)
lbu x5,0(x6)
lhu x7,0(x6)
lwu x8,0(x6)
sb x4,8(x6)
sh x4,10(x6)
sw x4,12(x6)
sd x4,16(x6)
ld x9,8(x6)
ld x10,16(x6)
ld x0,0(x6)
ld x6,0(x6)
1: j 1b
'''
    text = asm_case('memory', memory, 18, {6:BASE+1024})
    yield 'memory', text + f'mem {BASE+1024:#x} 8 0x8877665580008080\n'
    yield 'illegal-32', case_text([0x0000000b], 3)
    yield 'illegal-16', case_text([0x00000001], 3)
    for priv in [0,1,3]:
        yield f'ecall-{priv}', case_text([0x00000073], 3, priv=priv)
    yield 'ebreak', case_text([0x00100073], 3)
    yield 'bad-csr', case_text([0xfff312f3], 3, {5:0xdeadbeef,6:0x1234})
    yield 'readonly-csr', case_text([0xf14312f3], 3, {5:0xdeadbeef,6:0x1234})
    yield 'priv-csr', case_text([0x300322f3], 3, {5:0xdeadbeef}, priv=1)
    for name, instruction, address in [
        ('load-unmapped',0x00033283,0x40000000),('load-zero',0x00033003,0x40000000),
        ('store-unmapped',0x00533023,0x40000000),('load-misaligned',0x00033283,BASE+1025),
        ('store-misaligned',0x00533023,BASE+1025),('misaligned-priority',0x00033283,0x40000001),
    ]:
        yield name, case_text([instruction],3,{5:0xdeadbeef,6:address})
    yield 'fetch-unmapped', case_text([],3,pc=0x40000000)
    yield 'jal-misaligned', case_text([0x002002ef],3,{5:0xdeadbeef})
    yield 'jalr-misaligned', case_text([0x000282e7],3,{5:BASE+3})
    yield 'branch-taken-misaligned', case_text([0x00000163],3)
    yield 'branch-untaken-misaligned', case_text([0x00001163],1)
    yield 'fetch-second-halfword', case_text([],1,pc=BASE+124,size=126,
                                            extra_mem=[(BASE+124,2,0x0013)])
    yield '16-bit-at-boundary', case_text([],1,pc=BASE+124,size=126,
                                         extra_mem=[(BASE+124,2,0x0001)])
    for name, instruction in [('load-at-boundary',0x00033283),('store-at-boundary',0x00533023)]:
        yield name, case_text([instruction],1,{5:0xdeadbeef,6:BASE+120},size=126)
    rng = random.Random(0x52495649)
    ops = ['add','sub','sll','slt','sltu','xor','srl','sra','or','and',
           'addw','subw','sllw','srlw','sraw']
    source = '\n'.join(f'{rng.choice(ops)} x{rng.randrange(32)},x{rng.randrange(32)},x{rng.randrange(32)}'
                       for _ in range(200)) + '\n1: j 1b\n'
    registers = {i:rng.getrandbits(64) for i in range(1,32)}
    yield 'seeded-integer', asm_case('seeded-integer',source,201,registers)


def execute(binary, path, inject=None):
    command = [str(binary),str(path)] + ([inject] if inject else [])
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, timeout=20)
    if completed.returncode:
        raise RuntimeError(f'{command}: exit {completed.returncode}\n{completed.stderr}')
    return [json.loads(line) for line in completed.stdout.splitlines()], command


def m_cases():
    isa = 'rv64im_zicsr_zifencei'
    ops = [0x0033,0x1033,0x2033,0x3033,0x4033,0x5033,0x6033,0x7033,
           0x003b,0x403b,0x503b,0x603b,0x703b]
    def encode(op, rd, rs1=6, rs2=7):
        return 0x02000000 | (rs2 << 20) | (rs1 << 15) | (rd << 7) | op
    values = [0,1,2,3,7,(1<<64)-1,(1<<64)-2,1<<63,(1<<63)-1,
              0x80000000,0x7fffffff,0xffffffff,0xffffffff80000000,
              0x1234567880000000,0xaaaaaaaa00000000]
    for i,a in enumerate(values):
        for j,b in enumerate(values):
            words = [encode(op,8+n) for n,op in enumerate(ops)]
            yield f'm-boundary-{i}-{j}', case_text(words,len(words),{6:a,7:b},isa=isa)
    for n,op in enumerate(ops):
        for rd in [0,6,7]:
            yield f'm-alias-{n}-{rd}', case_text([encode(op,rd)],1,
                                                {6:0x8000000000000000,7:(1<<64)-1},isa=isa)
        for rs1,rs2 in [(0,7),(6,0),(6,6),(0,0)]:
            yield f'm-source-{n}-{rs1}-{rs2}', case_text([encode(op,5,rs1,rs2)],1,
                                                       {6:(1<<64)-1,7:0},isa=isa)
    rng = random.Random(0x4d524956)
    words = [encode(rng.choice(ops),rng.randrange(32),rng.randrange(32),rng.randrange(32))
             for _ in range(1000)]
    yield 'm-seeded', case_text(words,1000,{i:rng.getrandbits(64) for i in range(1,32)},isa=isa)


def a_cases():
    isa='rv64ima_zicsr_zifencei'
    def encode(op, length=8, rd=5, order=0, rs1=6, rs2=7):
        return (op << 27) | (order << 25) | ((0 if op == 2 else rs2) << 20) | \
               (rs1 << 15) | ((2 if length == 4 else 3) << 12) | (rd << 7) | 0x2f
    ops=[0,1,4,8,12,16,20,24,28]
    values=[0,1,2,(1<<64)-1,1<<63,(1<<63)-1,0x80000000,0x7fffffff,
            0xffffffff80000000,0x1234567880000003]
    for length in [4,8]:
        for i,a in enumerate(values):
            for j,b in enumerate(values):
                words=[]; memory=[]
                for n,op in enumerate(ops):
                    # 每个 AMO 获得独立已初始化 word；addi 设置相应地址。
                    words.extend([((16*n) << 20) | (31 << 15) | (6 << 7) | 0x13,
                                  encode(op,length,8+n,(i+j+n)%4)])
                    memory.append((BASE+1024+16*n,length,a & ((1<<(8*length))-1)))
                yield f'a-boundary-{length}-{i}-{j}',case_text(words,len(words),
                       {7:b,31:BASE+1024},extra_mem=memory,isa=isa)
        for op in ops:
            for order in range(4):
                for rd in [0,5,6,7]:
                    yield f'a-alias-{length}-{op}-{order}-{rd}',case_text(
                          [encode(op,length,rd,order)],1,{6:BASE+1024,7:(1<<64)-1},
                          extra_mem=[(BASE+1024,length,0x80000003)],isa=isa)
        for order in range(4):
            initial={6:BASE+1024,7:0x1234567887654321}
            memory=[(BASE+1024,8,0x8000000000000003),(BASE+1032,8,99)]
            sequences={
                'lr-sc':[encode(2,length,5,order),encode(3,length,5,order),encode(3,length,5)],
                'lr-zero-sc':[encode(2,length,0,order),encode(3,length,7,order)],
                'lr-rd-address':[encode(2,length,6,order),encode(3,length,5,order,rs1=10)],
                'sc-alone':[encode(3,length,5,order)],
                'different-address':[encode(2,length),0x00830313,encode(3,length)],
                'lr-replaced':[encode(2,length),0x00830313,encode(2,length),
                               0xff830313,encode(3,length)],
                'own-store':[encode(2,length),0x00732023 if length==4 else 0x00733023,
                             encode(3,length)],
            }
            for name,words in sequences.items():
                yield f'a-{name}-{length}-{order}',case_text(words,len(words),
                      initial | {10:BASE+1024},extra_mem=memory,isa=isa)
        for op in ops+[2,3]:
            for name,addr in [('misaligned',BASE+1025),('unmapped',0x40000000),
                              ('boundary',BASE+4096),('mmio',0x10000000)]:
                yield f'a-fault-{length}-{op}-{name}',case_text([encode(op,length)],3,
                       {5:0xdeadbeef,6:addr,7:0x1234},isa=isa)
    for name,word in [('lr-rs2',encode(2,4)|(7<<20)),('unknown',encode(5)),
                      ('bad-width',(encode(0)&~0x7000)|0x1000)]:
        yield 'a-illegal-'+name,case_text([word],3,isa=isa)
    rng=random.Random(0x41524956)
    destinations=[i for i in range(32) if i!=6]
    words=[encode(rng.choice(ops),rng.choice([4,8]),rng.choice(destinations),rng.randrange(4),
                  rs2=rng.randrange(32)) for _ in range(500)]
    regs={i:rng.getrandbits(64) for i in range(1,32)};regs[6]=BASE+3072
    yield 'a-seeded',case_text(words,500,regs,extra_mem=[(BASE+3072,8,0x8000000080000000)],isa=isa)


def c_cases():
    isa='rv64ic_zicsr_zifencei'
    registers={i:BASE+1024+16*i for i in range(1,32)}
    words=[0x0001,0x0285,0x0281,0x3281,0x52fd,0x72fd,0x717d,0x0800,
           0x907d,0x947d,0x987d,0x8c05,0x8c25,0x8c45,0x8c65,0x9c05,0x9c25,
           0x12fe,0x829a,0x929a,0x5c64,0x7c64,0xdc64,0xfc64,0x52fe,0x72fe,
           0xdf96,0xff96,0x8282,0x9082,0x9002,0xa009,0xc009,0xe009,
           0,0x2001,0x6001,0x6101,0x4002,0x6002,0x8002,0x9c41,0x9c61,
           0x2000,0xa000,0x2002,0xa002,0x0005,0x4001,0x6005,0x8006,0x9006,
           0x0002,0x8001,0x8401]
    for word in words:
        yield f'c-single-{word:04x}',case_text([],1,registers,isa=isa,
                                             extra_mem=[(BASE,2,word)])
    for word in [0xc009,0xe009]:
        for value in [0,1]:
            yield f'c-branch-{word:04x}-{value}',case_text([],1,{8:value},isa=isa,
                                                         extra_mem=[(BASE,2,word)])
    for word in [0x5c64,0x7c64,0xdc64,0xfc64,0x52fe,0x72fe,0xdf96,0xff96]:
        for address in [0x40000000,BASE+1025]:
            yield f'c-memory-fault-{word:04x}-{address:x}',case_text([],1,
                   {2:address,8:address,5:0xdeadbeef,9:0xdeadbeef},isa=isa,
                   extra_mem=[(BASE,2,word)])
    yield 'c-mixed-halfword-pc',case_text([],1,{5:42},pc=BASE+2,isa=isa,
                                        extra_mem=[(BASE+2,4,0x00128293)])
    yield 'c-fetch-last-halfword',case_text([],1,pc=BASE+124,size=126,isa=isa,
                                         extra_mem=[(BASE+124,2,0x0001)])
    yield 'c-fetch-second-halfword',case_text([],1,pc=BASE+126,size=128,isa=isa,
                                           extra_mem=[(BASE+126,2,0x0013)])
    for pc in [BASE,BASE+2]:
        yield f'c-misa-clear-{pc:x}',case_text([],2,{5:4},pc=pc,isa=isa,
               extra_mem=[(pc,4,0x3012b073),(pc+4,2,0x0001)])
    rng=random.Random(0x43524956)
    operations=['c.addi x5,1','c.addiw x6,-32','c.li x7,-1','c.lui x9,0xfffff',
                'c.srli x10,63','c.srai x11,63','c.andi x12,-1',
                'c.sub x13,x14','c.xor x14,x15','c.or x8,x9','c.and x10,x11',
                'c.subw x12,x13','c.addw x14,x15','c.slli x16,63',
                'c.mv x17,x18','c.add x19,x20','addi x21,x21,-2048']
    source='.option rvc\n'+ '\n'.join(rng.choice(operations) for _ in range(1000))+'\n'
    yield 'c-seeded-mixed',asm_case('c-seeded-mixed',source,1000,
                                   {i:rng.getrandbits(64) for i in range(1,32)},isa=isa)


def c_exhaustive():
    registers={i:BASE+1024+16*i for i in range(1,32)}
    for word in range(65536):
        if word & 3 != 3:
            yield f'c-exhaustive-{word:04x}',case_text([],1,registers,
                   isa='rv64ic_zicsr_zifencei',extra_mem=[(BASE,2,word)])


def csr_cases():
    isa='rv64i_zicsr_zifencei_zicntr'
    def encode(address,function,rd=5,rs1=6):
        return (address<<20)|(rs1<<15)|(function<<12)|(rd<<7)|0x73
    addresses=[0x300,0x302,0x303,0x304,0x344,0x305,0x341,0x342,0x343,0x340,
               0x306,0x105,0x141,0x142,0x143,0x140,0x180,0x106,0x100,0x104,0x144]
    for address in addresses:
        for function in [1,2,3,5,6,7]:
            for value in [0,(1<<64)-1,0x5555555555555555,0xaaaaaaaaaaaaaaaa]:
                words=[encode(address,function),encode(address,2,7,0)]
                yield f'csr-value-{address:x}-{function}-{value:x}',case_text(words,2,{6:value},isa=isa)
    for priv in [0,1,3]:
        for address in [0x340,0x140,0xf14,0xfff]:
            for function in [1,2,3,5,6,7]:
                for rs1 in [0,6]:
                    yield f'csr-permission-{priv}-{address:x}-{function}-{rs1}',case_text(
                        [encode(address,function,5,rs1)],1,{5:0xdeadbeef,6:0},priv=priv,isa=isa)
    for function in [1,2,3,5,6,7]:
        for rd in [0,5,6]:
            yield f'csr-alias-{function}-{rd}',case_text([encode(0x340,function,rd)],1,
                    {5:0xdeadbeef,6:0x1234},isa=isa)+'csr 832 7\n'
    for delegation in [0,2,0x222]:
        for address in [0x104,0x144]:
            words=[encode(address,1),encode(address,2,7,0)]
            yield f'csr-view-{address:x}-{delegation:x}',case_text(words,2,{6:(1<<64)-1},isa=isa)+ \
                f'csr 771 {delegation}\ncsr 772 0xaaa\ncsr 836 0x222\n'
    for function in [1,2,3,5,6,7]:
        yield f'csr-tvm-{function}',case_text([encode(0x180,function)],1,
                {5:0xdeadbeef},priv=1,isa=isa)+'csr 768 0xa00100000\n'


def priv_cases(compressed_capable=False):
    isa='rv64i_zicsr_zifencei_zicntr'
    for priv in [0,1,3]:
        for delegated in [0,0xb3ff]:
            for status in [0,2,8,10,0x200aa,0x4600aa]:
                for vector in [0,1]:
                    for name,raw in [('ecall',0x73),('break',0x100073),('illegal',0xb),
                                     ('load',0x33283),('store',0x533023)]:
                        text=case_text([raw],1,{5:0xdeadbeef,6:0x40000000},priv=priv,isa=isa)
                        yield f'priv-trap-{priv}-{delegated:x}-{status:x}-{vector}-{name}',text+ \
                            f'csr 768 {0xa00000000|status:#x}\ncsr 770 {delegated}\n'+ \
                            f'csr 773 {BASE+3072+vector:#x}\ncsr 261 {BASE+2048+vector:#x}\n'+ \
                            f'csr 833 {BASE+64:#x}\ncsr 321 {BASE+96:#x}\n'
    for name,raw in [('mret',0x30200073),('sret',0x10200073),('wfi',0x10500073),
                     ('sfence',0x12000073),('sfence-regs',0x12730073)]:
        for priv in [0,1,3]:
            for control in [0,0x100000,0x200000,0x400000]:
                yield f'priv-guard-{name}-{priv}-{control:x}',case_text([raw],1,
                        {5:0xdeadbeef},priv=priv,isa=isa)+ \
                        f'csr 768 {0xa00000000|control:#x}\ncsr 833 {BASE+64:#x}\ncsr 321 {BASE+96:#x}\n'
    for previous in [0,1,3]:
        for pie in [0,1]:
            for mprv in [0,1]:
                for compressed in ([False,True] if compressed_capable else [False]):
                    status=(previous<<11)|(0x80 if pie else 0)|(0x20000 if mprv else 0)|0x22
                    yield f'priv-mret-{previous}-{pie}-{mprv}-{compressed}',case_text([0x30200073],1,
                        isa=isa.replace('rv64i','rv64ic') if compressed else isa)+ \
                        f'csr 768 {0xa00000000|status:#x}\ncsr 833 {BASE+67:#x}\n'
    for current in [1,3]:
        for previous in [0,1]:
            for pie in [0,1]:
                status=0x20088|(previous<<8)|(0x20 if pie else 0)|(0x400000 if current==3 else 0)
                yield f'priv-sret-{current}-{previous}-{pie}',case_text([0x10200073],1,priv=current,isa=isa)+ \
                    f'csr 768 {0xa00000000|status:#x}\ncsr 321 {BASE+96:#x}\n'
    handler=BASE+2048
    code=[word for _ in range(100) for word in [0x00108093,0x00000073]]
    handler_words=[0x141022f3,0x00428293,0x14129073,0x10200073]
    yield 'priv-repeat-delegated-return',case_text(code,600,priv=0,isa=isa,
            extra_mem=[(handler+4*i,4,w) for i,w in enumerate(handler_words)])+ \
            f'csr 770 256\ncsr 261 {handler:#x}\n'
    supervisor=[0x73,*handler_words]
    machine=[0x34102373,0x00430313,0x34131073,0x30200073]
    yield 'priv-nested-m-s-u-return',case_text([0x73,0x00108093,0x6f],11,priv=0,isa=isa,
            extra_mem=[(handler+4*i,4,w) for i,w in enumerate(supervisor)]+ \
                      [(BASE+3072+4*i,4,w) for i,w in enumerate(machine)])+ \
            f'csr 770 256\ncsr 261 {handler:#x}\ncsr 773 {BASE+3072:#x}\n'


def irq_cases():
    isa='rv64i_zicsr_zifencei_zicntr'
    def make(words,steps=2,priv=3,status=0,enable=0,delegation=0,events=(),pc=BASE,mem=()):
        extra=[(BASE+2048+4*i,4,0x13) for i in range(32)]+ \
              [(BASE+3072+4*i,4,0x13) for i in range(32)]+list(mem)
        text=case_text(words,steps,{5:0xdeadbeef,6:8},priv=priv,pc=pc,isa=isa,extra_mem=extra)
        text+=f'csr 768 {0xa00000000|status:#x}\ncsr 772 {enable}\ncsr 771 {delegation}\n'
        text+=f'csr 773 {BASE+2048:#x}\ncsr 261 {BASE+3072:#x}\n'
        return text+''.join(f'irq {event} {bit} {level}\n' for event,bit,level in events)
    bits=[1,3,5,7,9,11]
    for priv in [0,1,3]:
        for bit in bits:
            for status in [0,2,8,10]:
                for enabled in [False,True]:
                    for delegated in [False,True]:
                        yield f'irq-matrix-{priv}-{bit}-{status}-{enabled}-{delegated}',make(
                            [0x13,0x13],priv=priv,status=status,enable=(1<<bit) if enabled else 0,
                            delegation=(1<<bit) if delegated else 0,events=[(1,bit,1),(2,bit,0)])
    for priv in [0,1,3]:
        for a,b in itertools.combinations(bits,2):
            mask=(1<<a)|(1<<b)
            for delegation in sorted(set([0,0x222,mask&0x222,1<<a&0x222,1<<b&0x222])):
                yield f'irq-priority-{priv}-{a}-{b}-{delegation}',make([0x13,0x13],priv=priv,
                    status=10,enable=mask,delegation=delegation,
                    events=[(1,a,1),(1,b,1),(2,a,0),(2,b,0)])
    yield 'irq-fetch-preempted',make([],steps=1,priv=0,enable=0x80,pc=0x40000000,events=[(1,7,1)])
    for bit in bits:
        for priv in [0,1,3]:
            for delegated in [0,0x222]:
                text=make([0x13],steps=1,priv=priv,status=10,enable=1<<bit,delegation=delegated)
                if bit in [1,5,9]:
                    yield f'irq-software-{bit}-{priv}-{delegated}',text+f'csr 836 {1<<bit}\n'
    yield 'irq-target-priority',make([0x13],steps=1,priv=1,status=2,enable=0x202,delegation=0x200,
                                     events=[(1,1,1),(1,9,1)])
    yield 'irq-enable-next-boundary',make([0x30032073,0x00108093],steps=3,enable=0x80,
                                         events=[(1,7,1),(3,7,0)])
    yield 'irq-cancel-before-enabled',make([0x30032073,0x00108093],steps=2,enable=0x80,
                                          events=[(1,7,1),(2,7,0)])
    yield 'irq-mret-boundary',make([0x30200073,0x00108093],steps=3,enable=0x80,
                                  events=[(1,7,1),(3,7,0)])+f'csr 833 {BASE+4:#x}\n'
    yield 'irq-nested-return',make([0x00108093],steps=5,priv=0,enable=0x280,delegation=0x200,
        events=[(1,9,1),(2,9,0),(2,7,1),(3,7,0)],
        mem=[(BASE+2048,4,0x30200073),(BASE+3072,4,0x10200073)])
    yield 'irq-wfi-wake',make([0x10500073,0x00108093],steps=3,enable=0x80,status=8,
                             events=[(2,7,1),(3,7,0)])


def counter_cases():
    isa='rv64i_zicsr_zifencei_zicntr'
    def encode(address,function=2,rd=5,rs1=0):
        return (address<<20)|(rs1<<15)|(function<<12)|(rd<<7)|0x73
    for address,shadow in [(0xb00,0xc00),(0xb02,0xc02)]:
        for value in [0,1,(1<<64)-1,1<<63,0x5555555555555555]:
            for function in [1,2,3,5,6,7]:
                for rd in [0,5,6]:
                    words=[encode(address,function,rd,6),encode(shadow,2,7),encode(address,2,8),encode(0xc01,2,9)]
                    yield f'counter-write-{address:x}-{value:x}-{function}-{rd}',case_text(words,4,
                        {6:value},isa=isa)+ 'csr 2816 23\ncsr 2818 17\ntime 0 42\n'
        for function in [2,3,6,7]:
            for source in [0,6]:
                yield f'counter-write-suppression-{address:x}-{function}-{source}',case_text(
                    [encode(address,function,5,source),encode(shadow,2,7)],2,{6:0},isa=isa)+ \
                    'csr 2816 23\ncsr 2818 17\n'
    for priv in [0,1,3]:
        for m in [0,1,2,4,7]:
            for s in [0,1,2,4,7]:
                for address in [0xc00,0xc01,0xc02]:
                    yield f'counter-permission-{priv}-{m}-{s}-{address:x}',case_text(
                        [encode(address)],1,{5:0xdeadbeef},priv=priv,isa=isa)+ \
                        f'csr 774 {m}\ncsr 262 {s}\ncsr 2816 23\ncsr 2818 17\ntime 0 42\n'
    for priv in [0,1,3]:
        for address in [0xc00,0xc01,0xc02]:
            for function in [1,2,3,5,6,7]:
                for source in [0,6]:
                    yield f'counter-readonly-{priv}-{address:x}-{function}-{source}',case_text(
                        [encode(address,function,5,source)],1,{5:0xdeadbeef,6:0},priv=priv,isa=isa)+ \
                        'csr 774 7\ncsr 262 7\ntime 0 42\n'
    for address in [0xb80,0xb82,0xc80,0xc81,0xc82,0xc03,0xc1f]:
        yield f'counter-unavailable-{address:x}',case_text([encode(address)],1,{5:0xdeadbeef},isa=isa)
    for address in [0x320]+list(range(0xb03,0xb20))+list(range(0x323,0x340)):
        words=[encode(address,1,5,6),encode(address,2,7),encode(0xc02,2,8)]
        yield f'counter-zero-{address:x}',case_text(words,3,{6:(1<<64)-1},isa=isa)
    words=[word for _ in range(250) for word in [encode(0xb02,1,0,6),encode(0xc02,2,7)]]
    yield 'counter-seeded-writes',case_text(words,500,{6:0},isa=isa)
    words=[encode(0xc01,2,5),encode(0xc01,2,6),encode(0xc01,2,7),encode(0xc01,2,8)]
    yield 'counter-time-input',case_text(words,4,isa=isa)+ \
            'time 0 42\ntime 2 0x123456789abcdef0\ntime 3 0xffffffffffffffff\ntime 4 0\n'


def save_failure(name, path, dut, ref, difference, commands):
    index, fields = difference
    packet = dict(case=name, input=path.read_text(), commands=commands,
                  first_event=index, differing_fields=fields,
                  dut=dut[max(0,index-16):index+1], ref=ref[max(0,index-16):index+1],
                  spike_manifest=json.loads((ROOT/'build/spike/manifest.json').read_text()),
                  dut_sha256=hashlib.sha256(Path(commands[0][0]).read_bytes()).hexdigest())
    destination = OUT / (name + '-failure.json')
    destination.write_text(json.dumps(packet,indent=2)+'\n')
    return destination


def main():
    global OUT
    parser=argparse.ArgumentParser()
    parser.add_argument('--dut',type=Path,default=ROOT/'build/rviss_dut')
    parser.add_argument('--ref',type=Path,default=ROOT/'build/spike/bridge')
    parser.add_argument('--suite',choices=['i','m','a','ma','c','mc','ac','mac'],default='i')
    parser.add_argument('--exhaustive-c',action='store_true')
    parser.add_argument('--csr',action='store_true')
    parser.add_argument('--priv',action='store_true')
    parser.add_argument('--irq',action='store_true')
    parser.add_argument('--counters',action='store_true')
    parser.add_argument('--out',type=Path,default=OUT)
    args=parser.parse_args()
    OUT=args.out.resolve()
    args.dut=args.dut.resolve(); args.ref=args.ref.resolve()
    OUT.mkdir(parents=True,exist_ok=True)
    records=[]; event_count=0; started=time.monotonic()
    suite = list(cases()) + (list(m_cases()) if 'm' in args.suite else [])
    if 'a' in args.suite: suite += list(a_cases())
    if 'c' in args.suite: suite += list(c_cases())
    if args.csr: suite += list(csr_cases())
    if args.priv: suite += list(priv_cases('c' in args.suite))
    if args.irq: suite += list(irq_cases())
    if args.counters: suite += list(counter_cases())
    if args.exhaustive_c:
        assert 'c' in args.suite
        suite=itertools.chain(suite,c_exhaustive())
    for name,text in suite:
        path=OUT/(name+'.case');path.write_text(text)
        dut,dc=execute(args.dut,path); ref,rc=execute(args.ref,path)
        difference=first_difference(dut,ref)
        if difference:
            packet=save_failure(name,path,dut,ref,difference,[dc,rc])
            raise AssertionError(f'{name}: first event {difference}; {packet}')
        if name in ['seeded-integer', 'm-seeded', 'a-seeded'] or name.startswith('a-boundary-'):
            # 程序的 200 条 R/W 指令必须全部顺序执行，然后到达唯一的尾部循环。
            assert all(int(event['pc_before'], 16) == BASE + 4*i
                       and event['retired'] for i, event in enumerate(dut[1:])), name
        if name == 'c-seeded-mixed':
            pc=BASE
            for event in dut[1:]:
                assert event['retired'] and int(event['pc_before'],16)==pc,name
                pc+=event['length']
        if name == 'priv-repeat-delegated-return':
            assert dut[-1]['privilege']==0 and int(dut[-1]['pc'],16)==BASE+800
            assert int(dut[-1]['gpr'][1],16)==100 and dut[-1]['retired_total']==500
            assert sum(e['outcome']=='synchronous_trap' for e in dut[1:])==100
        if name == 'priv-nested-m-s-u-return':
            assert dut[-1]['privilege']==0 and int(dut[-1]['gpr'][1],16)==1
            assert dut[-1]['retired_total']==9
        if name == 'irq-target-priority':
            assert dut[1]['privilege']==3 and int(dut[1]['csrs']['342'],16)==(1<<63)|1
        if name == 'irq-nested-return':
            assert [e['outcome'] for e in dut[1:]]==['interrupt','interrupt','retired','retired','retired']
            assert dut[-1]['privilege']==0 and int(dut[-1]['gpr'][1],16)==1
            assert dut[-1]['retired_total']==3
        if name == 'irq-cancel-before-enabled':
            assert dut[-1]['retired_total']==2 and int(dut[-1]['gpr'][1],16)==1
        if name == 'counter-seeded-writes':
            assert all(e['retired'] and int(e['pc_before'],16)==BASE+4*i for i,e in enumerate(dut[1:]))
            assert dut[-1]['retired_total']==500 and int(dut[-1]['csrs']['b02'],16)==1
        if name == 'counter-time-input':
            assert [int(dut[-1]['gpr'][i],16) for i in [5,6,7,8]]==[42,0x123456789abcdef0,(1<<64)-1,0]
        # 新进程重复执行，比较完整初始状态和全部事件，不能复用缓存状态。
        again,_=execute(args.dut,path); ref_again,_=execute(args.ref,path)
        assert again==dut and ref_again==ref
        event_count+=len(dut)-1
        records.append(dict(case=name,events=len(dut)-1,input_sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
        print(f'{name}: {len(dut)-1} events matched',flush=True)
    injections=[]
    for kind,name in [('gpr','two-adds'),('store','memory'),('trap','illegal-32')]:
        path=OUT/(name+'.case')
        dut,dc=execute(args.dut,path,kind);ref,rc=execute(args.ref,path)
        difference=first_difference(dut,ref)
        assert difference is not None,kind
        expected={'gpr':'gpr','store':'ram_hash','trap':'csrs'}[kind]
        assert expected in difference[1],difference
        packet=save_failure('injected-'+kind,path,dut,ref,difference,[dc,rc])
        injections.append(dict(kind=kind,first_event=difference[0],fields=difference[1],packet=str(packet)))
        print('injected',kind,'captured at event',difference[0],flush=True)
    summary=dict(status='verified',suite=args.suite,cases=records,event_count=event_count,reset_replay=True,
                 exhaustive_c=args.exhaustive_c,csr_rules=args.csr,priv_rules=args.priv,irq_rules=args.irq,counter_rules=args.counters,
                 injections=injections,seconds=time.monotonic()-started,
                 spike_manifest=json.loads((ROOT/'build/spike/manifest.json').read_text()),
                 compared=['PC/raw/length/priv','full GPR','23 trap/priv CSR','store','full RAM hash','retirement'],
                 excluded=['misa capability identity','mcycle timing policy','cycle values across trap/IRQ (different permitted timing policies)','full platform devices',
                           '/'.join(ext.upper() for ext in 'mac' if ext not in args.suite) + '/F/D/Sv39/PMP'])
    (OUT/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(f'Spike difftest: {len(records)} cases / {event_count} events passed; 3 injected differences captured.')


if __name__=='__main__':
    main()
