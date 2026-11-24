"""Inspect the actual PiPL resource in a compiled AEX, not just its existence."""
import ctypes, struct, sys
from pathlib import Path

dll=Path(sys.argv[1])
k=ctypes.WinDLL('kernel32',use_last_error=True)
k.LoadLibraryExW.argtypes=[ctypes.c_wchar_p,ctypes.c_void_p,ctypes.c_uint32]
k.LoadLibraryExW.restype=ctypes.c_void_p
k.FindResourceW.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_wchar_p]
k.FindResourceW.restype=ctypes.c_void_p
k.SizeofResource.argtypes=[ctypes.c_void_p,ctypes.c_void_p]
k.SizeofResource.restype=ctypes.c_uint32
k.LoadResource.argtypes=[ctypes.c_void_p,ctypes.c_void_p]
k.LoadResource.restype=ctypes.c_void_p
k.LockResource.argtypes=[ctypes.c_void_p]
k.LockResource.restype=ctypes.c_void_p
k.FreeLibrary.argtypes=[ctypes.c_void_p]
k.FreeLibrary.restype=ctypes.c_int
h=k.LoadLibraryExW(str(dll),None,2) # no DLL code executed
if not h:raise OSError(f'LoadLibraryEx(DATAFILE) failed: {ctypes.get_last_error()}')
try:
 r=k.FindResourceW(h,ctypes.c_void_p(16000),'PiPL')
 if not r:raise OSError(f'PiPL resource 16000 absent: {ctypes.get_last_error()}')
 size=k.SizeofResource(h,r)
 data=ctypes.string_at(k.LockResource(k.LoadResource(h,r)),size)
 assert len(data)>=10,'PiPL header truncated'
 version,reserved,n=struct.unpack_from('<HII',data,0)
 assert (version,reserved,n)==(1,0,13),f'PiPL header invalid {(version,reserved,n)}'
 expected=[
    (b'dnik',4),(b'eman',12),(b'gtac',12),(b'4668',12),
    (b'RVPe',4),(b'RVSe',4),(b'REVe',4),(b'FNIe',2),
    (b'OLGe',4),(b'2LGe',4),(b'ANMe',16),(b'LFea',4),(b'LRUe',24)
 ]
 properties={}
 offset=10
 for i,(key,expected_len) in enumerate(expected):
  assert offset%4==2,f'PiPL record alignment broken at #{i+1}: {offset}'
  assert offset+16<=size,f'PiPL record #{i+1} header truncated'
  vendor,actual_key,ident,length=struct.unpack_from('4s4sII',data,offset)
  assert vendor==b'MIB8',f'PiPL sync lost at record #{i+1}, offset={offset}, vendor={vendor!r}'
  assert ident==0,f'PiPL property #{i+1} has incorrect identifier {ident}'
  assert actual_key==key,f'PiPL wrong property at #{i+1}: {actual_key!r}, want {key!r}'
  assert length==expected_len,f'PiPL {actual_key!r} payload size={length}, expected={expected_len}'
  start=offset+16
  assert start+length<=size,f'PiPL {key!r} payload truncated'
  properties[key]=data[start:start+length]
  offset=start+((length+3)&~3)
 assert offset==size,f'PiPL trailing/unaccounted bytes offset={offset} size={size}'
 assert properties[b'dnik']==b'TKFe','Effect kind mismatch'
 assert properties[b'eman'][:10]==b'\x09Gradation','Effect display name mismatch'
 assert properties[b'gtac']==b'\x08Generate\x00\x00\x00','Category padding mismatch'
 assert properties[b'4668'].startswith(b'EffectMain\x00'),'EffectMain entry mismatch'
 assert struct.unpack('<I',properties[b'RVPe'])[0]==2,'PiPL version'
 assert struct.unpack('<HH',properties[b'RVSe'])==(13,28),'SDK effect spec'
 assert struct.unpack('<I',properties[b'REVe'])[0]==525828,'Effect version PF_VERSION'
 assert struct.unpack('<I',properties[b'OLGe'])[0]==0x06008400,'GLOBAL out_flags'
 assert struct.unpack('<I',properties[b'2LGe'])[0]==0x0A001480,'GLOBAL out_flags2'
 assert properties[b'ANMe']==b'\x0eADBE Gradation\x00','Match name'
 assert properties[b'LRUe'].startswith(b'\x15https://www.adobe.com'),'URL'
 print(f'PASS: canonical Adobe PiPL 13/13 properties, aligned offsets, {size} bytes, SDK13.28, matching flags and effect version')
finally:
 k.FreeLibrary(h)
