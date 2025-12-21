import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv("bench/results/latency_summary.csv")

sizes = df["ops"]          # uses actual data
p50 = df["p50_ns"]
p99 = df["p99_ns"]

plt.plot(sizes, p50, marker="o", label="p50")
plt.plot(sizes, p99, marker="o", label="p99")

plt.yscale("log")
plt.xlabel("Random Ops Count")
plt.ylabel("Latency (ns)")
plt.title("OME Latency vs Load (Random Prices)")
plt.legend()
plt.grid(True)

plt.savefig("analysis/latency_vs_size.png")
plt.show()