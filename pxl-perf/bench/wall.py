# wall.py ROUNDS FRAMES binary... : runs the binaries in turn, ROUNDS times,
# and prints the median record and total microseconds of each scene.
import os, statistics, subprocess, sys
rounds, frames = int(sys.argv[1]), sys.argv[2]
bins = sys.argv[3:]
env = dict(os.environ)
res = {b: {} for b in bins}
scenes = []
for r in range(rounds):
    order = bins if r % 2 == 0 else list(reversed(bins))
    for b in order:
        out = subprocess.run([b, "-f", frames] + os.environ.get("SCENES", "").split(), capture_output=True,
                             text=True, env=env, check=True).stdout
        for line in out.splitlines()[1:]:
            f = line.split()
            if f[0] not in scenes:
                scenes.append(f[0])
            res[b].setdefault(f[0], {"rec": [], "end": [], "tot": [], "gpu": []})
            res[b][f[0]]["gpu"].append(float(f[9]))
            res[b][f[0]]["rec"].append(float(f[2]))
            res[b][f[0]]["end"].append(float(f[3]))
            res[b][f[0]]["tot"].append(float(f[4]))
names = [os.path.basename(b).replace("bench-", "").replace("-clang-20", "") for b in bins]
print(f"{'scene':15}" + "".join(f" {n[:22]:>22}" for n in names) + "   (median total us; record/end; x = vs first)")
for sc in scenes:
    base = statistics.median(res[bins[0]][sc]["tot"])
    row = f"{sc:15}"
    for b in bins:
        d = res[b][sc]
        tot = statistics.median(d["tot"])
        cell = f"{tot:.0f} ({statistics.median(d['rec']):.0f}/{statistics.median(d['end']):.0f})"
        if b != bins[0]:
            cell += f" {base / tot:.2f}x"
        row += f" {cell:>22}"
    print(row)

print()
print(f"{'scene':15}" + "".join(f" {n[:22]:>22}" for n in names) + "   (median lavapipe GPU us; x = vs first)")
for sc in scenes:
    base = statistics.median(res[bins[0]][sc]["gpu"])
    row = f"{sc:15}"
    for b in bins:
        g = statistics.median(res[b][sc]["gpu"])
        cell = f"{g:.0f}" + (f" {base / g:.2f}x" if b != bins[0] else "")
        row += f" {cell:>22}"
    print(row)
