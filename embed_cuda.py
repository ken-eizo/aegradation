from pathlib import Path
import sys
data=Path(sys.argv[1]).read_bytes()
lines=[','.join(str(x) for x in data[i:i+24])+',' for i in range(0,len(data),24)]
Path(sys.argv[2]).write_text('#pragma once\nnamespace gradation::cuda_render {\nalignas(16) inline constexpr unsigned char kCudaFatbin[]={\n'+'\n'.join(lines)+'\n};\n}\n')
