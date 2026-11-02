import sys, subprocess
name=sys.argv[1]
fs=[f'ui/raw/i_{name}_{i}.png' for i in range(25)]
subprocess.run([sys.executable,'-I','env/scripts/sheet.py',f'C:/Users/marti/AppData/Local/Temp/art/ic_{name}.png','2','9','#26282c']+fs)
