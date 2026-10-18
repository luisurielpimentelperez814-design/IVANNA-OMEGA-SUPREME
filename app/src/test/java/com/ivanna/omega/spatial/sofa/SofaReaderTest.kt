package com.ivanna.omega.spatial.sofa

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import java.io.File

/**
 * Prueba el parser contra .sofa REALES del proyecto y contra valores de referencia obtenidos con una
 * implementación independiente (h5py 3.x sobre libhdf5), NO contra el propio parser.
 * Un archivo es de superbloque v0 (netCDF4/tabla de símbolos), el otro v2 (cabeceras v2/enlaces).
 */
class SofaReaderTest {
    private fun asset(name: String): ByteArray {
        val c = listOf("src/main/assets/sofa/$name", "app/src/main/assets/sofa/$name").map(::File).firstOrNull { it.isFile }
            ?: fail("no se encuentra el asset $name desde ${File(".").absolutePath}").let { throw IllegalStateException() }
        return c.readBytes()
    }

    @Test fun `MIT KEMAR superbloque v0 coincide con h5py`() {
        val s = SofaReader.read(asset("MIT_KEMAR_normal_pinna.sofa"))
        assertEquals(710, s.measurements); assertEquals(2, s.receivers); assertEquals(512, s.samples)
        assertEquals(44100.0, s.sampleRate, 0.0)
        assertEquals("spherical", s.positionType)
        assertEquals("SimpleFreeFieldHRIR", s.conventions)
        assertEquals(6.103515625e-05, s.irAt(0, 0, 0), 0.0)
        assertEquals(-3.0517578125e-05, s.irAt(0, 1, 5), 0.0)
        assertEquals(0.00469970703125, s.irAt(355, 0, 170), 0.0)
        assertEquals(-0.001739501953125, s.irAt(709, 1, 511), 0.0)
        assertEquals(-25.36346435546875, s.ir.sum(), 1e-9)
        assertEquals(1430.3856104332954, s.ir.sumOf { it * it }, 1e-9)
        assertEquals(0.0, s.azimuthDeg(0), 0.0); assertEquals(-40.0, s.elevationDeg(0), 0.0); assertEquals(1.4, s.radius(0), 0.0)
        assertEquals(6.428571428571429, s.azimuthDeg(1), 0.0)
        assertEquals(115.0, s.azimuthDeg(355), 0.0); assertEquals(10.0, s.elevationDeg(355), 0.0)
        assertEquals(90.0, s.elevationDeg(709), 0.0)
    }

    @Test fun `Pulse superbloque v2 coincide con h5py`() {
        val s = SofaReader.read(asset("Pulse.sofa"))
        assertEquals(1250, s.measurements); assertEquals(2, s.receivers); assertEquals(256, s.samples)
        assertEquals(48000.0, s.sampleRate, 0.0)
        assertEquals(2500.0, s.ir.sum(), 0.0); assertEquals(2500.0, s.ir.sumOf { it * it }, 0.0)
        assertEquals(277.10707611044654, s.azimuthDeg(0), 0.0); assertEquals(-7.053022130283186, s.elevationDeg(0), 0.0)
        assertEquals(180.0, s.azimuthDeg(625), 0.0); assertEquals(84.375, s.elevationDeg(625), 0.0)
        assertEquals(96.38261007348962, s.azimuthDeg(1249), 0.0)
    }

    @Test fun `auriculares SimpleHeadphoneIR con SourcePosition 1x3 y atributos densos coincide con h5py`() {
        val s = SofaReader.read(asset("hpir_AKGK271MKII_nh719.sofa"))
        assertEquals(10, s.measurements); assertEquals(2, s.receivers); assertEquals(3504, s.samples)
        assertEquals(48000.0, s.sampleRate, 0.0)
        assertEquals("SimpleHeadphoneIR", s.conventions)
        assertEquals(-0.02980069899823065, s.ir.sum(), 1e-12)
        assertEquals(1.7765312302844825, s.ir.sumOf { it * it }, 1e-9)
        assertEquals(0.0002186293713748455, s.irAt(0, 0, 0), 0.0)
        assertEquals(4.488180638873018e-05, s.irAt(3, 1, 1000), 0.0)
        assertEquals(-4.638198788597947e-06, s.irAt(9, 0, 3503), 0.0)
        assertEquals(30, s.sourcePosition.size)            // 1x3 normalizado a Mx3
        assertEquals(0.0, s.azimuthDeg(7), 0.0)
    }

    @Test fun `el archivo mas grande del proyecto (10 MB, 360 x 2 x 2048) coincide con h5py`() {
        val s = SofaReader.read(asset("TU-Berlin_QU_KEMAR_anechoic_radius_0.5m.sofa"))
        assertEquals(360, s.measurements); assertEquals(2048, s.samples)
        assertEquals(5.210753875815572, s.ir.sum(), 1e-9)
        assertEquals(-0.00027366766489213886, s.irAt(180, 1, 2048 / 3), 0.0)
    }

    @Test fun `todos los SOFA HRIR del proyecto se leen enteros y finitos y los SOS se rechazan`() {
        val dir = listOf("src/main/assets/sofa", "app/src/main/assets/sofa").map(::File).first { it.isDirectory }
        var ok = 0; var rejected = 0
        for (f in dir.listFiles()!!.filter { it.name.endsWith(".sofa") }) {
            try {
                val s = SofaReader.read(f.readBytes())
                assertEquals(f.name, s.measurements * s.receivers * s.samples, s.ir.size)
                assertEquals(f.name, s.measurements * 3, s.sourcePosition.size)
                assertTrue(f.name, s.sampleRate in 8000.0..384000.0)
                ok++
            } catch (e: Hdf5FormatException) {
                assertTrue("${f.name}: ${e.message}", e.message!!.contains("Data.IR"))   // unico rechazo legitimo
                rejected++
            }
        }
        assertEquals(212, ok); assertEquals(4, rejected)   // contado con h5py: 212 HRIR + 4 SOS sin Data.IR
    }

    @Test fun `no es HDF5 se rechaza con mensaje claro`() {
        try { SofaReader.read(ByteArray(200)); fail() } catch (e: Hdf5FormatException) { assertTrue(e.message!!.contains("HDF5")) }
    }

    @Test fun `archivo truncado no devuelve datos falsos`() {
        val full = asset("MIT_KEMAR_normal_pinna.sofa")
        try { SofaReader.read(full.copyOf(full.size / 3)); fail("debia fallar") } catch (e: Exception) { /* cualquier fallo explicito vale */ }
    }

    @Test fun `un SOFA sin Data IR (SOS) se rechaza`() {
        try { SofaReader.read(asset("SimpleFreeFieldSOS.sofa")); fail() } catch (e: Hdf5FormatException) { assertTrue(e.message!!.contains("Data.IR")) }
    }
}
