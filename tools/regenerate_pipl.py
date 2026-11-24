"""Rebuild an Adobe PiPL with the OFFICIAL SDK tool and MSVC preprocessor."""
from pathlib import Path
import os,sys,subprocess
def run(args):
 p=subprocess.run([str(a) for a in args],capture_output=True)
 if p.returncode:
  raise RuntimeError(f'command {args}\nexit {p.returncode}\n{p.stdout.decode(errors="replace")}\n{p.stderr.decode(errors="replace")}')
 if p.stderr:
  print('stderr:',p.stderr.decode(errors="replace"))
 return p.stdout
def clpath():
 vsroot=Path(r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC')
 tools=sorted(vsroot.glob(r'*\bin\Hostx64\x64\cl.exe'),reverse=True)
 if not tools:raise FileNotFoundError('MSVC cl.exe')
 return tools[0]
def main():
 source=Path(sys.argv[1]);target=Path(sys.argv[2])
 headers=Path(sys.argv[3]);tool=Path(sys.argv[4])
 target.parent.mkdir(parents=True,exist_ok=True)
 rr=target.with_suffix('.rr')
 rrc=target.with_suffix('.rrc')
 cl=Path(sys.argv[5]) if len(sys.argv)>5 else clpath()
 # cl.exe preprocessor preserves the active SDK macros and controls the
 # right 64-bit platform entry point. /EP suppresses #line directives.
 rr.write_bytes(run([cl,'/nologo','/EP','/D_WIN32','/D_M_X64',f'/I{headers}',source]))
 if b'resource' not in rr.read_bytes() or b'CodeWin64X86' not in rr.read_bytes():
  raise RuntimeError('wrong platform/failed .r preprocessing')
 run([tool,rr,rrc])
 raw=run([cl,'/nologo','/EP','/DMSWindows',rrc])
 if b'16000' not in raw or b'PiPL' not in raw or b'Generate' not in raw:
  raise RuntimeError('invalid canonical RC output')
 tmp=target.with_suffix('.rc.tmp')
 tmp.write_bytes(raw)
 os.replace(tmp,target)
 print('Official Adobe PiPLTool generated:',target,'bytes',len(raw))
if __name__=='__main__': main()
