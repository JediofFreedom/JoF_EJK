"""Run the supplied x86 routine with deterministic host callbacks."""
from pathlib import Path
import argparse, hashlib, math, struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_EAX

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
args = parser.parse_args()
expected_hash = '1521db9b7bae7d358019c0b72ae240305c801386a0473e0d9506129a32a91fde'
if hashlib.sha256(args.binary.read_bytes()).hexdigest() != expected_hash:
    parser.error('The binary does not match the supplied R22.3.01 reference')

u = Uc(UC_ARCH_X86, UC_MODE_32)
u.mem_map(0, 0x4000000)
with args.binary.open('rb') as f:
    elf = ELFFile(f)
    for seg in elf.iter_segments():
        if seg['p_type'] == 'PT_LOAD':
            u.mem_write(seg['p_vaddr'], seg.data())
    symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
    dyn = elf.get_section_by_name('.dynsym')
    for rel in elf.get_section_by_name('.rel.dyn').iter_relocations():
        if rel['r_info_type'] in [6, 7]:
            sym = dyn.get_symbol(rel['r_info_sym'])
            u.mem_write(rel['r_offset'], struct.pack('<I', sym['st_value']))

def ints(p, n=1): return struct.unpack('<'+'I'*n, u.mem_read(p,n*4))
def floats(p,n=3): return struct.unpack('<'+'f'*n,u.mem_read(p,n*4))
def writeints(p,*v): u.mem_write(p,struct.pack('<'+'I'*len(v),*(i&0xffffffff for i in v)))
def writefloats(p,*v): u.mem_write(p,struct.pack('<'+'f'*len(v),*v))
def f32(v):return struct.unpack('<f',struct.pack('<f',v))[0]

cent=0x3000000; origin=0x3001000; forward=0x3001100
stack=0x3ff0000; stop=0x3fff000
output=[]; rcount=0; qcount=0; traces=0; hitmask=0
random_values=[0,8192,16384,24576,32767,1234,30000,10000,22222]
def event(name,*args): output.append(name+' '+' '.join(format(x,'.9g') if isinstance(x,float) else str(x) for x in args))

def hook(uc, addr, size, data):
    global rcount,qcount,traces
    sp=uc.reg_read(UC_X86_REG_ESP)
    a=ints(sp+4,7)
    result=0
    if addr==0x32770:
        result=random_values[rcount%len(random_values)];rcount+=1
    elif addr==0x31cc0:
        result=a[0]+(qcount*137+73)%(a[1]-a[0]+1);qcount+=1
        event('IRAND',a[0],a[1],result)
    elif addr==0x2fdb0:
        angles=floats(a[0]);event('ANGLES',*angles)
        yaw=f32(angles[1]*(math.pi*2/360));pitch=f32(angles[0]*(math.pi*2/360))
        cp=f32(math.cos(pitch))
        writefloats(a[1],f32(cp*f32(math.cos(yaw))),f32(cp*f32(math.sin(yaw))),-f32(math.sin(pitch)))
    elif addr==0x34038:
        assert a[2]==0 and a[3]==0 and a[5]==0xffffffff and a[6]==0x1001
        event('TRACE',*floats(a[1]),*floats(a[4]))
        u.mem_write(a[0],bytes(64));writefloats(a[0]+4,0.25 if hitmask&(1<<traces) else 1.0)
        traces+=1
    elif addr==0x32620:
        assert a[0]==42 and a[3]==a[4]==0xffffffff
        event('FX',*floats(a[1]),*floats(a[2]))
    elif addr==0x337d0:
        event('SOUND',*floats(a[0]),a[1],a[2],a[3])
    else: return
    uc.reg_write(UC_X86_REG_EAX,result)
    uc.reg_write(UC_X86_REG_ESP,sp+4)
    uc.reg_write(UC_X86_REG_EIP,ints(sp)[0])

for addr in [0x32770,0x31cc0,0x2fdb0,0x34038,0x32620,0x337d0]:
    u.hook_add(UC_HOOK_CODE,hook,begin=addr,end=addr)
writeints(symbols['cgs']+0x685fc,42)
writeints(symbols['cgs']+0x6796c,11,12,13)

# Includes misses, mixed hits, cached retargeting, moving hands, pause/high FPS,
# low FPS, exact timer equality, expiration, time reversal and narrow/wide changes.
cases=[
 (1,0,1000,16,3,(10,20,30),(1,0,0)),
 (0,0,1016,16,3,(12,24,31),(0,1,0)),
 (0,0,1025,16,1,(12,24,31),(0,1,0)),
 (0,0,1050,0,3,(12,24,31),(0,1,0)),
 (0,0,1573,50,3,(50,20,0),(0,0,1)),
 (0,0,1574,51,3,(50,20,0),(0,0,1)),
 (0,1,2010,10,31,(0,0,0),(0.6,0.8,0)),
 (0,1,2060,9,10,(-13,17,60),(0,0.6,0.8)),
 (0,1,5000,100,31,(-13,17,60),(0,0.6,0.8)),
 (0,0,100,16,3,(1,2,3),(1,0,0)),
 (1,1,1000,16,0,(10,20,30),(1,0,0)),
 (0,1,1010,10,21,(10,20,30),(1,0,0)),
 (0,1,1030,30,31,(10,20,30),(1,0,0)),
 (1,1,0,16,31,(0,0,0),(1,0,0)),
]
for reset,wide,time,frametime,hitmask,org,fwd in cases:
    event('STEP',reset,wide,time,frametime,hitmask,*org,*fwd)
    if reset:
        u.mem_write(cent,bytes(0x91c));rcount=qcount=0
        writeints(cent,7)
    writeints(symbols['cg']+0x440f8,time)
    writefloats(symbols['cg']+0x440ec,float(frametime))
    writefloats(origin,*org);writefloats(forward,*fwd)
    writeints(stack,stop,cent,origin,forward,3,wide,0)
    u.reg_write(UC_X86_REG_ESP,stack);traces=0
    u.emu_start(symbols['CG_DoLightningArcs'],stop,count=200000)
    assert u.reg_read(UC_X86_REG_EIP)==stop
    for i in range(5):
        event('STATE',i,ints(cent+0x7e0+4*i)[0],*floats(cent+0x7f4+12*i),ints(cent+0x830+4*i)[0])
    event('RANDOM',rcount,qcount)
Path(__file__).with_name('reference.txt').write_text('\n'.join(output)+'\n')
print('Recorded',len(cases),'frames and',len(output),'events from CG_DoLightningArcs at',hex(symbols['CG_DoLightningArcs']))
