"""F/D common inputs: bit patterns, not a duplicate arithmetic oracle."""
import itertools
import random


def fp_cases(case_text, base, double=True, compressed_exhaustive=False):
    isa='rv64imafdc_zicsr_zifencei_zicntr' if double else 'rv64imafc_zicsr_zifencei_zicntr'
    box=0xffffffff00000000
    vals_s=[0,0x80000000,0x3f800000,0xbf800000,0x3fc00000,1,0x800001,0x7fffff,
            0x7f7fffff,0x7f800000,0xff800000,0x7fc12345,0x7f812345]
    vals_d=[0,0x8000000000000000,0x3ff0000000000000,0xbff0000000000000,0x3ff8000000000000,
            1,0x10000000000001,0xfffffffffffff,0x7fefffffffffffff,
            0x7ff0000000000000,0xfff0000000000000,0x7ff8123456789abc,0x7ff0123456789abc]
    def op(fn,d=0,rm=0,rs2=2,rd=5,rs1=1):
        return (fn<<27)|(d<<25)|(rs2<<20)|(rs1<<15)|(rm<<12)|(rd<<7)|0x53
    def make(words, fpr=None, regs=None, status=1<<13, fcsr=0, priv=3, pc=None, extra=None, steps=None):
        gpr={1:0x8000000080000001,6:base+1024};gpr.update(regs or {})
        text=case_text(words,len(words) if steps is None else steps,gpr,base if pc is None else pc,
                       priv,4096,extra,isa)
        text+=f'csr 768 {0xa00000000|status:#x}\ncsr 3 {fcsr:#x}\n'
        for i,v in (fpr or {}).items():text+=f'fpr {i} {v:#x}\n'
        return text
    def pack(v,d):return v if d else box|v
    precisions=[(0,vals_s)]+([(1,vals_d)] if double else [])
    for d,values in precisions:
        for n,(a,b,rm) in enumerate(itertools.product(values,values,[0,1,2,3,4,7])):
            words=[op(fn,d,rm) for fn in [0,1,2,3]]+[op(11,d,rm,0)]
            words += [op(4,d,x) for x in [0,1,2]]+[op(5,d,x) for x in [0,1]]
            words += [op(20,d,x) for x in [0,1,2]]+[op(28,d,1,0),op(28,d,0,0)]
            words += [op(24,d,rm,x) for x in range(4)]+[op(26,d,rm,x) for x in range(4)]
            if double:words += [op(8,1-d,rm,d)]
            yield f'fp-group-{d}-{n}',make(words,{1:pack(a,d),2:pack(b,d)},fcsr=2<<5)
        triples=[(values[0],values[9],values[11]),(values[1],values[2],values[0]),
                 (values[2],values[3],values[2]),(values[8],values[8],values[10])]
        rng=random.Random(0x13+d)
        triples += [tuple(rng.getrandbits(64 if d else 32) for _ in range(3)) for _ in range(80)]
        for n,(a,b,c) in enumerate(triples):
            words=[(3<<27)|(d<<25)|(2<<20)|(1<<15)|(rm<<12)|(5<<7)|opcode
                   for opcode,rm in itertools.product([0x43,0x47,0x4b,0x4f],[0,1,2,3,4,7])]
            yield f'fp-fma-{d}-{n}',make(words,{1:pack(a,d),2:pack(b,d),3:pack(c,d)},fcsr=4<<5)
        for fn,rm,frm in itertools.product([0,1,2,3,11,24,26]+([8] if double else []),[5,6,7],range(8)):
            if rm==7 and frm<5:continue
            rs2=0 if fn in [11,24,26] else 1-d if fn==8 else 2
            yield f'fp-reserved-rm-{d}-{fn}-{rm}-{frm}',make([op(fn,d,rm,rs2)],{1:pack(values[4],d)},fcsr=frm<<5)
        for fs,fn in itertools.product(range(4),[0,4,5,20,24,26,28,30]):
            rs2=0 if fn in [24,26,28,30] else 2
            rm=1 if fn==28 else 0
            yield f'fp-fs-{d}-{fs}-{fn}',make([op(fn,d,rm,rs2)],{1:pack(values[2],d),2:pack(values[4],d)},status=fs<<13)
        for value in [0,1,0xffffffff,0x80000000,0xffffffffffffffff,1<<63,(1<<63)-1,(1<<53)+1]:
            words=[op(26,d,rm,t) for t,rm in itertools.product(range(4),range(5))]
            yield f'fp-int-boundary-{d}-{value:x}',make(words,regs={1:value})
        # Source/destination aliases, writable f0, discarded x0, invalid boxed input.
        for rd,rs1,rs2 in itertools.product([0,1,2,5],[0,1,2],[0,1,2]):
            yield f'fp-alias-{d}-{rd}-{rs1}-{rs2}',make([op(0,d,0,rs2,rd,rs1)],
                {0:pack(values[2],d),1:pack(values[4],d),2:pack(values[3],d)})
        for a,b in [(0x3fc00000,box|0x40100000),(0x123456783fc00000,0),(box|0x3fc00000,0x40100000)]:
            words=[op(0,0),op(4,0),op(5,0),op(20,0,2),op(28,0,1,0),op(28,0,0,0)]
            yield f'fp-boxing-{d}-{a:x}-{b:x}',make(words,{1:a,2:b})
        width=3 if d else 2
        for fs,address in itertools.product(range(4),[base+1024,base+1025,base+4096,0]):
            raw=(6<<15)|(width<<12)|(5<<7)|7
            yield f'fp-load-{d}-{fs}-{address:x}',make([raw],regs={6:address},status=fs<<13,
                extra=[(base+1024,8,0x7ff123456789abcd)])
            raw=(1<<20)|(6<<15)|(width<<12)|0x27
            yield f'fp-store-{d}-{fs}-{address:x}',make([raw],{1:0x7ff123456789abcd},regs={6:address},status=fs<<13)
    for addr,val in itertools.product([1,2,3],range(256)):
        words=[(addr<<20)|(6<<15)|(1<<12)|(5<<7)|0x73,
               (3<<20)|(2<<12)|(8<<7)|0x73,(1<<20)|(2<<12)|(9<<7)|0x73,
               (2<<20)|(2<<12)|(10<<7)|0x73]
        yield f'fp-csr-warl-{addr}-{val}',make(words,regs={6:val},fcsr=0xa5)
    for fs,addr,fun,source in itertools.product(range(4),[1,2,3],range(1,8),[0,1]):
        if fun==4:continue
        raw=(addr<<20)|(source<<15)|(fun<<12)|(5<<7)|0x73
        yield f'fp-csr-rules-{fs}-{addr}-{fun}-{source}',make([raw],status=fs<<13,regs={1:0},fcsr=0xa5)
    # Reserved encodings, unsupported H/Q, and wrong unary/conversion subcodes.
    for raw in [op(4,0,3),op(5,0,2),op(20,0,3),op(28,0,2,0),op(30,0,1,0),
                op(11,0,0,1),op(24,0,0,4),op(26,0,0,4),op(0,0)|1<<26,
                op(0,1)|1<<26,0x00000043|1<<26,0x00000007|(1<<12)]:
        yield f'fp-illegal-{raw:x}',make([raw])
    if not double:
        for raw in [op(0,1),op(8,0,0,1),op(8,1,0,0),0x00003307,0x00003027,0x2002,0xa002]:
            yield f'fp-d-disabled-{raw:x}',make([raw])
    # Real memory/move sequence with payload bits preserved through stores/loads.
    for d in [0,1] if double else [0]:
        width=3 if d else 2
        words=[op(30,d,0,0), (5<<20)|(6<<15)|(width<<12)|0x27,
               (6<<15)|(width<<12)|(7<<7)|7,op(28,d,0,0,8,7)]
        yield f'fp-memory-roundtrip-{d}',make(words,regs={1:0x7ff123456789abcd})
    # Enable a paged S access with a PTE whose A/D must be updated.
    for d,permission,access in itertools.product([0,1] if double else [0], [0,1,3], ['load','store']):
        width=3 if d else 2
        raw=(6<<15)|(width<<12)|(5<<7)|7 if access=='load' else (5<<20)|(6<<15)|(width<<12)|0x27
        text=make([raw],{5:0x7ff123456789abcd},status=(1<<13)|(1<<17)|(1<<11))+'pmp 16\n'
        for i in range(16):text+=f'csr {0x3b0+i} 0\n'
        text+=f'csr 944 {((base+1024)>>2)|1:#x}\ncsr 959 {(1<<54)-1:#x}\n'
        text+=f'csr 928 {0x18|permission:#x}\ncsr 930 0x1f00000000000000\n'
        yield f'fp-pmp-{d}-{permission}-{access}',text
    if double:
        text=make([0x00033287],status=(1<<13)|(1<<17)|(1<<11))+'pmp 16\n'
        for i in range(16):text+=f'csr {0x3b0+i} 0\n'
        text+=f'csr 944 {(base+1024)>>2:#x}\ncsr 959 {(1<<54)-1:#x}\ncsr 928 0x11\ncsr 930 0x1f00000000000000\n'
        yield 'fp-pmp-partial-double',text
    # Enable a paged S access with a PTE whose A/D must be updated.
    for raw in [0x00032287,0x00532027]+([0x00033287,0x00533027] if double else []):
        for leaf in [0,1,3,7,11]:
            root,l1,l0,data=base+0x8000,base+0x9000,base+0xa000,base+0x6000
            pte=lambda pa,flags:((pa>>12)<<10)|flags
            text=case_text([raw],1,{6:0x5000},0x4000,1,65536,
                [(root,8,pte(l1,1)),(l1,8,pte(l0,1)),(l0+4*8,8,pte(base,0xcb)),
                 (l0+5*8,8,pte(data,leaf)),(data,8,0x1122334455667788)],isa)
            text+=f'mmu sv39\ncsr 384 {(8<<60)|(root>>12):#x}\ncsr 768 0xa00002000\nfpr 5 0x7ff123456789abcd\n'
            yield f'fp-sv39-{raw:x}-{leaf}',text
    rng=random.Random(0x1300)
    regs={i:rng.getrandbits(64) for i in range(1,32)}
    fprs={i:rng.getrandbits(64) for i in range(32)}
    words=[]
    for _ in range(500):
        d=rng.randrange(2) if double else 0;fn=rng.choice([0,1,2,3,4,5,11,20,24,26,28,30])
        rm=rng.choice([0,1,2,3,4,7]) if fn in [0,1,2,3,11,24,26] else rng.randrange(3) if fn in [4,20] else rng.randrange(2) if fn in [5,28] else 0
        rs2=rng.randrange(4) if fn in [24,26] else 0 if fn in [11,28,30] else rng.randrange(32)
        words.append(op(fn,d,rm,rs2,rng.randrange(32),rng.randrange(32)))
    yield 'fp-seeded',make(words,fprs,regs,status=2<<13,fcsr=2<<5)
    if double and compressed_exhaustive:
        fprs={i:rng.getrandbits(64) for i in range(32)}
        regs={i:base+1024 for i in range(1,32)}
        for group in [0x2000,0xa000,0x2002,0xa002]:
            for bits in range(2048):
                # Mask e003 leaves 11 independently enumerated bits 2..12.
                raw=group|(bits<<2)
                yield f'fp-c-exhaust-{raw:04x}',make([raw],fprs,regs)
