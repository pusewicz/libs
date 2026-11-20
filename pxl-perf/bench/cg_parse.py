import glob, os, re, subprocess, sys
d = sys.argv[1]
frames = 3
sizes = {}
for line in open(os.path.join(d, "bench.txt")):
    f = line.split()
    if f and f[0] != "scene":
        sizes[f[0]] = (int(f[1]), f[6])
print(f"{'scene':15} {'record':>8} {'end':>8} {'end:pxl':>8} {'end:SDL+drv':>11}   (Ir per item)  draws")
for path in sorted(glob.glob(os.path.join(d, "cg.out.*")), key=lambda p: int(p.rsplit(".", 1)[1])):
    out = subprocess.run(["callgrind_annotate", "--inclusive=yes", "--threshold=100", path],
                         capture_output=True, text=True).stdout
    incl = {}
    for line in out.splitlines():
        m = re.match(r"\s*([\d,]+) \([\s\d.]+%\)\s+\S*:(\S+) \[", line)
        if m:
            incl.setdefault(m.group(2), int(m.group(1).replace(",", "")))
    scene = next((k[6:] for k in incl if k.startswith("scene_")), None)
    if scene is None:
        continue
    n, draws = sizes[scene]
    k = frames * n
    rec = incl[f"scene_{scene}"] / k
    end = incl.get("pxl_end_frame", 0) / k
    sdl = sum(v for name, v in incl.items() if re.fullmatch(r"SDL_\w*GPU\w*", name)
              and "_REAL" not in name and name != "SDL_WaitForGPUIdle") / k
    print(f"{scene:15} {rec:8.1f} {end:8.1f} {end - sdl:8.1f} {sdl:11.1f}                  {draws}")
