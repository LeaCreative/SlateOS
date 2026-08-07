package slate.map

import slate.image.PngExport
import slate.interpreter.DisplayListInterpreter
import java.io.File

/**
 * Render the map to PNGs, exactly as the watch would draw it.
 *
 *     ./gradlew :sdp-tests:mapPreview
 *
 * The watch is sealed and the agent cannot see it, so every map change would
 * otherwise cost a build, an install and a photograph to evaluate. This runs
 * the real pipeline — real captured OSM data, the real renderer, the real
 * display-list interpreter — and writes what the panel would show.
 *
 * It is a development tool, not a test. `MapFixtureTest` is what enforces the
 * invariants; this is for looking at.
 */
fun main() {
    val fixture = checkNotNull(
        MapPreviewGenerator::class.java.getResourceAsStream("/map/victoria.json"),
    ) { "missing /map/victoria.json" }.bufferedReader().readText()

    val coastJson = MapPreviewGenerator::class.java
        .getResourceAsStream("/map/victoria-coastline.json")?.bufferedReader()?.readText()
    val coast = coastJson?.let { OverpassParser.parse(it) } ?: emptyList()
    val ways = OverpassParser.parse(fixture) + coast
    val victoria = GeoPoint(-4.6191, 55.4513)
    val outDir = File("build/map-preview").apply { mkdirs() }
    val interpreter = DisplayListInterpreter()

    println("fixture: ${ways.size} ways (${coast.size} coastline) from ${fixture.length / 1024} KB")
    println()

    fun shot(name: String, viewer: GeoPoint, radiusM: Double) {
        val data = MapData(victoria, radiusM, ways, 0L)
        val r = MapRenderer.render(data, viewer, radiusM)
        val out = interpreter.render(r.bytes)
        PngExport.writePng(out.framebuffer, File(outDir, "$name.png"))
        println(
            "%-22s r=%5.0fm  %5d B  %3d ways drawn  %4d dropped  scale %dm  parser=%s".format(
                name, radiusM, r.bytes.size, r.waysDrawn, r.waysDropped,
                r.scaleBarMetres, out.status,
            ),
        )
    }

    // Same place, every radius the setting allows.
    for (radius in listOf(100.0, 200.0, 400.0, 800.0, 2000.0)) {
        shot("victoria-${radius.toInt()}m", victoria, radius)
    }
    println()

    // Walking north-east: the map must track the viewer between fetches, which
    // is the behaviour that makes it feel live rather than pinned.
    for ((i, step) in listOf(0.0, 0.0015, 0.0030, 0.0045).withIndex()) {
        shot(
            "walk-$i",
            GeoPoint(victoria.lat + step, victoria.lon + step),
            400.0,
        )
    }
    println()

    // Buildings, at and around the radius where they are actually fetched.
    // OverpassClient stops asking for them above BUILDING_MAX_RADIUS_M (250 m),
    // so anything wider here is showing what would NOT be requested on device —
    // included only to make the reason visible.
    val buildingsJson = MapPreviewGenerator::class.java
        .getResourceAsStream("/map/victoria-buildings.json")?.bufferedReader()?.readText()
    if (buildingsJson == null) {
        println("(no building fixture; skipping the buildings preview)")
        return
    }
    val buildings = OverpassParser.parse(buildingsJson)
    val combined = ways + buildings
    println("buildings fixture: ${buildings.size} outlines")
    for (radius in listOf(100.0, 150.0, 175.0, 200.0, 225.0, 250.0, 400.0)) {
        val data = MapData(victoria, radius, combined, 0L)
        val r = MapRenderer.render(data, victoria, radius)
        val out = interpreter.render(r.bytes)
        PngExport.writePng(out.framebuffer, File(outDir, "buildings-${radius.toInt()}m.png"))
        val note = if (radius > 250.0) "  <- above BUILDING_MAX_RADIUS_M; not fetched on device" else ""
        println(
            "%-22s r=%5.0fm  %5d B  %3d drawn  %4d dropped  parser=%s%s".format(
                "buildings-${radius.toInt()}m", radius, r.bytes.size,
                r.waysDrawn, r.waysDropped, out.status, note,
            ),
        )
    }
    println()
    println("wrote ${outDir.absolutePath}")
}

private object MapPreviewGenerator
