package slate.diag

/** Percentile / mean helpers for benchmark samples (milliseconds or microseconds). */
object BenchStats {
    data class Summary(
        val n: Int,
        val mean: Double,
        val p50: Double,
        val p95: Double,
        val p99: Double,
        val min: Double,
        val max: Double,
    ) {
        fun format(unit: String = "ms"): String =
            "n=$n mean=${fmt(mean)} $unit  p50=${fmt(p50)}  p95=${fmt(p95)}  " +
                "p99=${fmt(p99)}  min=${fmt(min)}  max=${fmt(max)}"

        private fun fmt(v: Double): String = "%.2f".format(v)
    }

    fun summarize(samples: List<Double>): Summary {
        require(samples.isNotEmpty()) { "empty samples" }
        val sorted = samples.sorted()
        val mean = samples.sum() / samples.size
        return Summary(
            n = samples.size,
            mean = mean,
            p50 = percentile(sorted, 0.50),
            p95 = percentile(sorted, 0.95),
            p99 = percentile(sorted, 0.99),
            min = sorted.first(),
            max = sorted.last(),
        )
    }

    /** Nearest-rank percentile on a sorted list. */
    fun percentile(sorted: List<Double>, p: Double): Double {
        if (sorted.isEmpty()) return Double.NaN
        if (sorted.size == 1) return sorted[0]
        val rank = (p * (sorted.size - 1)).coerceIn(0.0, (sorted.size - 1).toDouble())
        val lo = rank.toInt()
        val hi = (lo + 1).coerceAtMost(sorted.lastIndex)
        val frac = rank - lo
        return sorted[lo] * (1.0 - frac) + sorted[hi] * frac
    }

    fun usToMs(us: Long): Double = us / 1000.0

    fun kbpsFromBytesAndUs(bytes: Long, elapsedUs: Long): Double {
        if (elapsedUs <= 0L) return 0.0
        return (bytes * 100000.0) / elapsedUs / 100.0
    }

    fun gatePass(label: String, ok: Boolean, detail: String): String =
        "${if (ok) "PASS" else "FAIL"} $label — $detail"
}
