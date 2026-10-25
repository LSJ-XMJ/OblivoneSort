"""对 bench_grid 的 CSV 做加权期望分析。

用法：
    py -3 bench/weighted.py build/grid_2_128.csv [w_uniform w_normal w_exp w_few]

默认权重 1:1:1:1（没有可靠的真实分布概率时）。
当前实现取 leaf_sort=22；如果以后能分析出真实概率比例，
用本脚本加权重重新计算 T。

输出：
  - 每个 (k,T) 的加权胜者计数
  - 每个 k 的最优胜者区间
  - k=16..128 的平均时间与加速
  - k>=48 的最佳 T 中位数
  - 几个关键长度上的期望值
"""

import csv
import io
import statistics
import sys

DISTS = ["uniform", "normal", "exponential", "few"]


def main() -> None:
    path = sys.argv[1] if len(sys.argv) > 1 else "build/grid_2_128.csv"
    if len(sys.argv) >= 6:
        weights = [float(x) for x in sys.argv[2:6]]
        total = sum(weights)
        weights = [x / total for x in weights]
    else:
        weights = [0.25] * 4
    w = dict(zip(DISTS, weights))

    rows = [
        r
        for r in csv.DictReader(io.StringIO(open(path, encoding="utf-16").read()))
        if r["k"].isdigit()
    ]
    data = {}
    for r in rows:
        data.setdefault(int(r["k"]), {}).setdefault(r["dist"], {})[int(r["T"])] = (
            float(r["insertion_ns"]),
            float(r["quick_ns"]),
            float(r["oblivone_ns"]),
        )

    wins = {"insertion": 0, "quick": 0, "oblivone": 0}
    best_t = {}
    best_winner = {}
    mean_ins, mean_quick, mean_best = [], [], []

    for k in range(2, 129):
        any_t = min(data[k][DISTS[0]])
        e_ins = sum(w[d] * data[k][d][any_t][0] for d in DISTS)
        e_quick = sum(w[d] * data[k][d][any_t][1] for d in DISTS)
        best = min(e_ins, e_quick)
        bw = "insertion" if e_ins <= e_quick else "quick"
        bt = 0
        for t in range(2, min(64, k) + 1):
            e_ob = sum(w[d] * data[k][d][t][2] for d in DISTS)
            if e_ob < best:
                best, bw, bt = e_ob, "oblivone", t
            row_w = (
                "oblivone"
                if e_ob < min(e_ins, e_quick)
                else ("insertion" if e_ins <= e_quick else "quick")
            )
            wins[row_w] += 1
        best_t[k] = bt
        best_winner[k] = bw
        if k >= 16:
            mean_ins.append(e_ins)
            mean_quick.append(e_quick)
            mean_best.append(best)

    print("weights:", w)
    print("row wins (k,T):", wins, "total", sum(wins.values()))

    ranges = []
    start, cur = 2, best_winner[2]
    for k in range(3, 129):
        if best_winner[k] != cur:
            ranges.append((start, k - 1, cur))
            start, cur = k, best_winner[k]
    ranges.append((start, 128, cur))
    print("best winner ranges:", ranges)

    ts = [best_t[k] for k in range(48, 129) if best_t[k]]
    print("k>=48 best T median:", statistics.median(ts) if ts else 0)

    mi, mq, mb = (statistics.mean(x) for x in (mean_ins, mean_quick, mean_best))
    print(f"mean k=16..128: insertion {mi:.1f}, quick {mq:.1f}, oblivone {mb:.1f}")
    print(f"speedup vs quick {mq / mb:.2f}x, vs insertion {mi / mb:.2f}x")

    print("k,ins,quick,ob_best,bestT,winner")
    for k in [8, 16, 24, 32, 33, 48, 64, 96, 128]:
        any_t = min(data[k][DISTS[0]])
        e_ins = sum(w[d] * data[k][d][any_t][0] for d in DISTS)
        e_quick = sum(w[d] * data[k][d][any_t][1] for d in DISTS)
        best = min(e_ins, e_quick)
        bw = "insertion" if e_ins <= e_quick else "quick"
        bt = 0
        for t in range(2, min(64, k) + 1):
            e_ob = sum(w[d] * data[k][d][t][2] for d in DISTS)
            if e_ob < best:
                best, bw, bt = e_ob, "oblivone", t
        print(f"{k},{e_ins:.0f},{e_quick:.0f},{best:.0f},{bt},{bw}")


if __name__ == "__main__":
    main()
