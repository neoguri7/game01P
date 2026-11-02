"""dl.py <folder> <prefix> <job_id> <n>  : download n candidates of a PixelLab job into folder (no auth needed)."""
import sys, urllib.request, os
folder, prefix, job, n = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
os.makedirs(folder, exist_ok=True)
for i in range(n):
    url = f"https://api.pixellab.ai/mcp/images/{job}/download" + (f"?index={i}" if n > 1 else "")
    out = os.path.join(folder, f"{prefix}_{i}.png" if n > 1 else f"{prefix}.png")
    if os.path.exists(out): continue
    req = urllib.request.Request(url, headers={"User-Agent": "curl/8"})
    data = urllib.request.urlopen(req, timeout=60).read()
    tmp = out + ".tmp"; open(tmp, "wb").write(data); os.replace(tmp, out)
print(prefix, "ok", n)
