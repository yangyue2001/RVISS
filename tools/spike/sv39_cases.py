"""Priv-v1.12 Sv39 shared input fixtures, independent of either CPU execution."""
import itertools


def sv39_cases(case_text, base):
    root, l1, l0, data = [base + x for x in [0x8000, 0x9000, 0xa000, 0x6000]]
    code, va = 0x4000, 0x5000
    satp = (8 << 60) | (root >> 12)
    isa = 'rv64imac_zicsr_zifencei_zicntr'
    old = 0x1122334455667788
    def pte(pa, flags):
        return ((pa >> 12) << 10) | flags
    def make(raw=0x00033283, flags=0xc7, priv=1, address=va, pc=code,
             status=0, entries=None, regs=None, steps=1, code_flags=None, extra=None):
        values = {root:pte(l1,1), l1:pte(l0,1), l0+4*8:pte(base,0xcb|(16 if priv==0 else 0)),
                  l0+5*8:pte(data,flags), data:old}
        if code_flags is not None: values[l0+4*8]=pte(base,code_flags)
        values.update(entries or {})
        mem=[(addr,8,v) for addr,v in values.items()] + (extra or [])
        gpr={5:0xdeadbeef,6:address,7:42};gpr.update(regs or {})
        return case_text([raw],steps,gpr,pc,priv,65536,mem,isa)+f'mmu sv39\ncsr 384 {satp:#x}\ncsr 768 {0xa00000000|status:#x}\n'

    ops={'load':0x00033283,'store':0x00733023,'lr':0x100332af,'amo':0x007332af}
    # Valid and invalid permission encodings, U/S, immediate SUM/MXR, A/D combinations.
    for priv, user, permission, sum_, mxr, op in itertools.product([0,1],[0,1],range(8),[0,1],[0,1],ops):
        flags=1|permission*2|user*16|0xc0
        yield f'sv39-perm-{priv}-{user}-{permission}-{sum_}-{mxr}-{op}', make(
            ops[op],flags,priv,status=(sum_<<18)|(mxr<<19))
    for op, ad in itertools.product(ops, [0,64,128,192]):
        yield f'sv39-ad-{op}-{ad}',make(ops[op],7|ad)
    yield 'sv39-ad-sc-failed',make(0x187332af,7)
    yield 'sv39-ad-lr-sc',make(ops['lr'],7,steps=2,extra=[(base+4,4,0x187333af)])
    for type_, raw in [('fetch',0x00700293), *ops.items()]:
        for bit in range(54,64):
            entry=l0+4*8 if type_=='fetch' else l0+5*8
            target=base if type_=='fetch' else data
            yield f'sv39-reserved-{type_}-{bit}',make(raw,entries={entry:pte(target,0xcf)|(1<<bit)})
        for flags in [0,4,5,13,1,17,65,129,193]:
            entry=l0+4*8 if type_=='fetch' else l0+5*8
            target=base if type_=='fetch' else data
            yield f'sv39-invalid-leaf-{type_}-{flags}',make(raw,entries={entry:pte(target,flags)})
        for level,flags in itertools.product([root,l1],[17,65,129,193,0,5]):
            yield f'sv39-invalid-nonleaf-{type_}-{level:x}-{flags}',make(raw,entries={level:pte(l0,flags)})
        for address in [1<<38,1<<39,0xffffff8000005000]:
            yield f'sv39-noncanonical-{type_}-{address:x}',make(raw,address=address,pc=address if type_=='fetch' else code)
    for priv in [0,1]:
        high=0xffffffc000000000
        yield f'sv39-high-canonical-{priv}',make(priv=priv,address=high+va,pc=high+code,flags=0xc7|priv*0+ (16 if priv==0 else 0),
            entries={root+256*8:pte(l1,1)})
    for level in [1,2]:
        address=0x6000 if level==1 else 0x40006000
        entry=l1 if level==1 else root+8
        for op in ops:
            yield f'sv39-superpage-{level}-{op}',make(ops[op],address=address,priv=3,pc=base,status=(1<<17)|(1<<11),
                entries={entry:pte(base,7)})
        for offset in [4096,0x10000,0x200000] if level==2 else [4096,0x10000]:
            yield f'sv39-superpage-misaligned-{level}-{offset:x}',make(address=address,priv=3,pc=base,
                status=(1<<17)|(1<<11),entries={entry:pte(base+offset,0xc7)})
    for type_, raw in [('fetch',0x00700293), *ops.items()]:
        for level in [root,l1,l0+5*8]:
            entry=l0+4*8 if type_=='fetch' and level==l0+5*8 else level
            leaf=level==l0+5*8
            yield f'sv39-unmapped-table-{type_}-{level:x}',make(raw,entries={entry:pte(base+65536,7 if leaf else 1)})
    for priv, origin in itertools.product([0,1,3],[3]):
        for op in ops:
            yield f'sv39-mprv-{priv}-{op}',make(ops[op],flags=0xc7|(16 if priv==0 else 0),priv=origin,pc=base,
                status=(1<<17)|(priv<<11))
    # Actual fetch with clean A: successful decoding, forbidden X, U/S and AD-write protection.
    for priv,flags in itertools.product([0,1],[0,1,3,5,9,11,15,0x19,0x1b,0x1f]):
        yield f'sv39-fetch-flags-{priv}-{flags}',make(0x00700293,priv=priv,code_flags=flags)
    for priv,tvm,rs in itertools.product([0,1,3],[0,1],[0,6]):
        text=case_text([0x12000073|(rs<<15)|(rs<<20)],1,pc=base,priv=priv,isa=isa)
        yield f'sv39-sfence-{priv}-{tvm}-{rs}',text+f'mmu sv39\ncsr 768 {0xa00000000|(tvm<<20):#x}\n'
    for mode,ppn in itertools.product(range(16),[0,root>>12,(1<<44)-1]):
        vals=(mode<<60)|(0xffff<<44)|ppn
        yield f'sv39-satp-warl-{mode}-{ppn:x}',make(0x180312f3,priv=3,pc=base,regs={6:vals},steps=2,
            extra=[(base+4,4,0x180023f3)])
    # Page table physical permissions: each walk level R, leaf A/D W, final explicit W.
    for type_,raw in [('fetch',0x00700293),*ops.items()]:
        for page,permissions in itertools.product([root,l1,l0,data],[0,1,3]):
            text=make(raw,flags=7,code_flags=11)
            text+='pmp 16\n'
            for i in range(16): text+=f'csr {0x3b0+i} 0\n'
            text+=f'csr 944 {(page>>2)|511:#x}\ncsr 959 {(1<<54)-1:#x}\n'
            text+=f'csr 928 {0x18|permissions:#x}\ncsr 930 0x1f00000000000000\n'
            yield f'sv39-ptw-pmp-{type_}-{page:x}-{permissions}',text
    # A/D from the first page survives failure of the second halfword's walk.
    for second in [0,1,3,11]:
        text=make(0,pc=code+4094,code_flags=11,entries={l0+5*8:pte(data,second)},
                  extra=[(base+4094,2,0x0293),(data,2,0x0070)])
        yield f'sv39-cross-fetch-{second}',text
    # Change leaf mapping with SFENCE, then use the new physical page.
    newpte=pte(data+4096,0xc7)
    text=make(0x00733023,steps=4,regs={6:0x7000,7:newpte,8:va},entries={l0+7*8:pte(l0,0xc7)},
              extra=[(base+4,4,0x12000073),(base+8,4,0x00043283),(base+12,4,0x00000013),
                     (data+4096,8,42)])
    # Store to VA 0x7000 + slot5*8 writes exactly the leaf being changed.
    text=text.replace('gpr 6 0x7000','gpr 6 0x7028')
    yield 'sv39-change-leaf-sfence',text
    # Replace the root with a different full tree, maintaining executable mapping.
    root2,l12,l02=base+0xb000,base+0xc000,base+0xd000
    yield 'sv39-switch-root-sfence',make(0x18051073,steps=4,regs={10:(8<<60)|(root2>>12)},
        extra=[(base+4,4,0x12000073),(base+8,4,0x00033283),(base+12,4,0x00000013),
               (root2,8,pte(l12,1)),(l12,8,pte(l02,1)),
               (l02+4*8,8,pte(base,11)),(l02+5*8,8,pte(data+4096,7)),(data+4096,8,42)])
    # MRET enters paged U; fault delegates S; SRET uses virtual sepc and stack.
    text=make(0x30200073,priv=3,pc=base,code_flags=0x1b,flags=0,
              regs={6:va,8:code+516},steps=5,status=0,
              extra=[(base+512,4,0x00033283),(base+516,4,0x00700293),
                     (base+256,4,0x14141073),(base+260,4,0x10200073)],
              entries={l0+4*8:pte(base,0x1b)})
    # S cannot execute U page: handler is another supervisor page at VA0x8000.
    text+=f'csr 833 {code+512:#x}\ncsr 770 0x2000\ncsr 261 0x8000\n'
    text+=f'mem {l0+8*8:#x} 8 {pte(base+0x1000,11):#x}\n'
    text+=f'mem {base+0x1000:#x} 4 0x14141073\nmem {base+0x1004:#x} 4 0x10200073\n'
    # Handler writes sepc from x8, then returns to the successful U-mode addi.
    yield 'sv39-mret-delegate-sret',text
