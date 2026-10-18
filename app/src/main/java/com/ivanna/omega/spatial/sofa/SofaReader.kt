package com.ivanna.omega.spatial.sofa

/**
 * HRIR leída DEL ARCHIVO .sofa (no reconstruida desde la base PCA).
 * Disposición de [ir]: [medición][receptor][muestra] (convención SOFA: M × R × N).
 * [sourcePosition]: [medición][3]; con [positionType] "spherical" son (azimut°, elevación°, radio m).
 */
class SofaHrir(
    val sampleRate: Double,
    val measurements: Int,
    val receivers: Int,
    val samples: Int,
    val ir: DoubleArray,
    val sourcePosition: DoubleArray,
    val positionType: String,
    val conventions: String,
) {
    fun irAt(m: Int, r: Int, n: Int): Double = ir[(m * receivers + r) * samples + n]
    fun azimuthDeg(m: Int): Double = sourcePosition[m * 3]
    fun elevationDeg(m: Int): Double = sourcePosition[m * 3 + 1]
    fun radius(m: Int): Double = sourcePosition[m * 3 + 2]
}

object SofaReader {
    /** Lee Data.IR, Data.SamplingRate y SourcePosition. Lanza [Hdf5FormatException] si algo falta o no cuadra. */
    fun read(bytes: ByteArray): SofaHrir {
        val h = Hdf5File.parse(bytes)
        val conv = h.rootAttributes()["Conventions"]
        if (conv != null && conv != "SOFA") throw Hdf5FormatException("Conventions='$conv', se esperaba 'SOFA'")
        if (!h.hasDataset("Data.IR")) throw Hdf5FormatException("no es una HRIR de FIR: falta Data.IR")
        val irDs = h.dataset("Data.IR")
        if (irDs.rank != 3) throw Hdf5FormatException("Data.IR debe ser M×R×N, rank=${irDs.rank}")
        val (m, r, n) = irDs.dims.let { Triple(it[0], it[1], it[2]) }
        if (m <= 0 || r <= 0 || n <= 0) throw Hdf5FormatException("Data.IR con dimensión vacía: ${irDs.dims.toList()}")

        val srDs = h.dataset("Data.SamplingRate")
        val sr = h.readDoubles(srDs).firstOrNull() ?: throw Hdf5FormatException("Data.SamplingRate vacío")
        if (!sr.isFinite() || sr < 8000.0 || sr > 384000.0) throw Hdf5FormatException("frecuencia de muestreo inverosímil: $sr")

        val spDs = h.dataset("SourcePosition")
        // SOFA permite 1×3 (posición única, p. ej. SimpleHeadphoneIR) o M×3; se normaliza siempre a M×3.
        if (spDs.rank != 2 || spDs.dims[1] != 3 || (spDs.dims[0] != m && spDs.dims[0] != 1))
            throw Hdf5FormatException("SourcePosition debe ser M×3 o 1×3 con M=$m, es ${spDs.dims.toList()}")

        val ir = h.readDoubles(irDs)
        if (ir.any { !it.isFinite() }) throw Hdf5FormatException("Data.IR contiene valores no finitos")
        val spRaw = h.readDoubles(spDs)
        val sp = if (spDs.dims[0] == m) spRaw else DoubleArray(m * 3) { spRaw[it % 3] }
        return SofaHrir(sr, m, r, n, ir, sp, spDs.attributes["Type"] ?: "", h.rootAttributes()["SOFAConventions"] ?: "")
    }
}
