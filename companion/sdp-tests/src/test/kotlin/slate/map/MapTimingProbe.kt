package slate.map

/**
 * How long a reproject-and-render actually takes.
 *
 *     ./gradlew :sdp-tests:mapTiming
 *
 * It matters because [slate.app.map.MapAdapter] renders on the thread that
 * delivers the location fix — the main looper — once per fix. Guessing that it
 * is "probably fine" is exactly the habit this project keeps paying for, so
 * here is the number instead.
 *
 * Desktop JVM, not the phone: treat it as a lower bound and a way to catch an
 * order-of-magnitude regression, not as the Android figure.
 */
fun main() {
    val fixture = checkNotNull(
        MapTimingProbe::class.java.getResourceAsStream("/map/victoria.json"),
    ) { "missing /map/victoria.json" }.bufferedReader().readText()

    val victoria = GeoPoint(-4.6191, 55.4513)

    val parseStart = System.nanoTime()
    val ways = OverpassParser.parse(fixture)
    val parseMs = (System.nanoTime() - parseStart) / 1_000_000.0
    println("parse: ${ways.size} ways from ${fixture.length / 1024} KB in %.1f ms".format(parseMs))
    println("(parse happens once per refetch, off the main thread, in a coroutine)")
    println()

    for (radius in listOf(200.0, 400.0, 800.0, 2000.0)) {
        val data = MapData(victoria, radius, ways, 0L)
        // Warm up so this measures steady state rather than JIT.
        repeat(50) { MapRenderer.render(data, victoria, radius) }
        val samples = (0 until 200).map { i ->
            val viewer = GeoPoint(victoria.lat + i * 0.00001, victoria.lon)
            val t0 = System.nanoTime()
            MapRenderer.render(data, viewer, radius)
            (System.nanoTime() - t0) / 1_000_000.0
        }.sorted()
        val r = MapRenderer.render(data, victoria, radius)
        println(
            "render r=%5.0fm  median %.2f ms  p95 %.2f ms  max %.2f ms   (%d B, %d ways)".format(
                radius, samples[samples.size / 2], samples[(samples.size * 95) / 100],
                samples.last(), r.bytes.size, r.waysDrawn,
            ),
        )
    }
    println()
    println("Render runs on the location callback thread, once per fix (5 s apart).")
}

private object MapTimingProbe
