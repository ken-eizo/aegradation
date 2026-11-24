import subprocess
import sys
if len(sys.argv)!=3:
 raise SystemExit("Provide baseline executable and optimized executable")
a=subprocess.run([sys.argv[1]],capture_output=True,text=True,check=True).stdout.strip()
b=subprocess.run([sys.argv[2]],capture_output=True,text=True,check=True).stdout.strip()
print("32bpc F32 all-depth-safe grayscale/alpha direct output equality")
print("OLD:",a)
print("NEW:",b)
if a!=b:
 raise SystemExit("REGRESSION! Gray path changed exact output bits.")
print("PASS baseline and specialized have the identical FNV64 over all render byte buffers")
