package com.ivanna.omega.spatial.sofa

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.Inflater

/** El archivo no es HDF5 válido o usa una característica que este lector no implementa (el mensaje lo dice). */
class Hdf5FormatException(message: String) : Exception(message)

/**
 * Lector HDF5 mínimo y SIN dependencias (solo JDK) para los .sofa del proyecto. Lógica pura: se prueba en JVM
 * contra valores obtenidos con h5py (ver Hdf5FileTest), no contra la propia lectura.
 *
 * Soporta lo que usan los .sofa reales: superbloque v0/v1/v2/v3, cabeceras de objeto v1 y v2 (con bloques de
 * continuación), grupos con tabla de símbolos (B-tree v1 + heap local) y con mensajes de enlace, datasets compactos,
 * contiguos y troceados (layout v3, índice B-tree v1) con filtros shuffle, deflate y fletcher32, y atributos de
 * cadena. NO soporta (lanza [Hdf5FormatException] con el motivo, nunca devuelve datos falsos): almacenamiento denso
 * de enlaces (heap fractal), layout v4 y tipos que no sean flotante 32/64.
 */
class Hdf5File private constructor(private val b: ByteBuffer) {
    private var oSize = 8
    private var lSize = 8
    private var rootAddr = -1L

    class Dataset internal constructor(
        val dims: IntArray,
        internal val typeClass: Int,
        val elementSize: Int,
        internal val bigEndian: Boolean,
        internal val layoutClass: Int,
        internal val dataAddr: Long,
        internal val dataSize: Long,
        internal val chunkDims: IntArray?,
        internal val filters: List<Filter>,
        val attributes: Map<String, String>,
    ) {
        val rank: Int get() = dims.size
        val elementCount: Long get() = dims.fold(1L) { a, d -> a * d }
    }

    internal class Filter(val id: Int, val clientValues: IntArray)
    private class Msg(val type: Int, val pos: Int, val size: Int)

    companion object {
        private const val UNDEF = -1L

        fun parse(bytes: ByteArray): Hdf5File {
            val sig = byteArrayOf(0x89.toByte(), 'H'.code.toByte(), 'D'.code.toByte(), 'F'.code.toByte(), 13, 10, 26, 10)
            if (bytes.size < 64 || !bytes.copyOfRange(0, 8).contentEquals(sig)) throw Hdf5FormatException("no es HDF5 (firma ausente)")
            val f = Hdf5File(ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN))
            f.readSuperblock()
            return f
        }
    }

    // ---- lectura de bajo nivel -------------------------------------------------------------------------------
    private fun u8(p: Int): Int = b.get(p).toInt() and 0xFF
    private fun u16(p: Int): Int = u8(p) or (u8(p + 1) shl 8)
    private fun u32(p: Int): Long = (u16(p).toLong()) or (u16(p + 2).toLong() shl 16)
    private fun uint(p: Int, n: Int): Long {
        var v = 0L
        for (i in n - 1 downTo 0) v = (v shl 8) or u8(p + i).toLong()
        return if (n == 4 && v == 0xFFFFFFFFL) UNDEF else v
    }
    private fun off(p: Int): Long = uint(p, oSize)
    private fun len(p: Int): Long = uint(p, lSize)
    private fun ck(addr: Long, what: String): Int {
        if (addr < 0 || addr >= b.limit()) throw Hdf5FormatException("dirección fuera de archivo en $what: $addr")
        return addr.toInt()
    }
    private fun sigAt(p: Int, s: String): Boolean {
        if (p < 0 || p + s.length > b.limit()) return false
        for (i in s.indices) if (u8(p + i) != s[i].code) return false
        return true
    }

    private fun readSuperblock() {
        val ver = u8(8)
        when (ver) {
            0, 1 -> {
                oSize = u8(13); lSize = u8(14)
                var p = 24 + if (ver == 1) 4 else 0
                val base = off(p); if (base != 0L) throw Hdf5FormatException("userblock con base != 0 no soportado")
                p += 4 * oSize                       // base, free space, EOF, driver info
                rootAddr = off(p + oSize)            // entrada de la tabla de símbolos raíz: [nombre][cabecera de objeto]
            }
            2, 3 -> {
                oSize = u8(9); lSize = u8(10)
                val base = off(12); if (base != 0L) throw Hdf5FormatException("userblock con base != 0 no soportado")
                rootAddr = off(12 + 3 * oSize)
            }
            else -> throw Hdf5FormatException("versión de superbloque $ver no soportada")
        }
        if ((oSize != 4 && oSize != 8) || (lSize != 4 && lSize != 8)) throw Hdf5FormatException("tamaños de offset/length no soportados")
    }

    // ---- cabeceras de objeto -----------------------------------------------------------------------------------
    private fun messages(addr: Long): List<Msg> {
        val a = ck(addr, "cabecera de objeto")
        val out = ArrayList<Msg>()
        if (sigAt(a, "OHDR")) {
            val flags = u8(a + 5)
            var p = a + 6
            if (flags and 0x20 != 0) p += 16
            if (flags and 0x10 != 0) p += 4
            val szLen = 1 shl (flags and 3)
            val chunk0 = uint(p, szLen).toInt(); p += szLen
            val queue = ArrayDeque<IntArray>()
            queue.add(intArrayOf(p, p + chunk0))
            val trackOrder = flags and 0x04 != 0
            var guard = 0
            while (queue.isNotEmpty()) {
                if (++guard > 1000) throw Hdf5FormatException("demasiados bloques de continuación")
                val (s, e) = queue.removeFirst()
                var q = s
                while (q + 4 <= e) {
                    val type = u8(q); val size = u16(q + 1)
                    q += 4; if (trackOrder) q += 2
                    if (q + size > e) break
                    if (type == 0x10) {
                        val co = ck(off(q), "continuación"); val cl = len(q + oSize).toInt()
                        if (!sigAt(co, "OCHK")) throw Hdf5FormatException("bloque de continuación sin OCHK")
                        queue.add(intArrayOf(co + 4, co + cl - 4))
                    } else if (type != 0) out.add(Msg(type, q, size))
                    q += size
                }
            }
        } else {
            if (u8(a) != 1) throw Hdf5FormatException("cabecera de objeto v${u8(a)} no soportada")
            val queue = ArrayDeque<IntArray>()
            queue.add(intArrayOf(a + 16, a + 16 + u32(a + 8).toInt()))
            var guard = 0
            while (queue.isNotEmpty()) {
                if (++guard > 1000) throw Hdf5FormatException("demasiados bloques de continuación")
                val (s, e) = queue.removeFirst()
                var q = s
                while (q + 8 <= e) {
                    val type = u16(q); val size = u16(q + 2)
                    val data = q + 8
                    if (data + size > e) break
                    if (type == 0x10) {
                        val co = ck(off(data), "continuación"); val cl = len(data + oSize).toInt()
                        queue.add(intArrayOf(co, co + cl))
                    } else if (type != 0) out.add(Msg(type, data, size))
                    q = data + size
                }
            }
        }
        return out
    }

    // ---- grupos ----------------------------------------------------------------------------------------------------
    private fun children(addr: Long): Map<String, Long> {
        val res = LinkedHashMap<String, Long>()
        for (m in messages(addr)) when (m.type) {
            0x11 -> {                                   // tabla de símbolos
                val tree = off(m.pos); val heap = off(m.pos + oSize)
                val h = ck(heap, "heap local")
                if (!sigAt(h, "HEAP")) throw Hdf5FormatException("heap local sin firma")
                val dataSeg = ck(off(h + 8 + 2 * lSize), "segmento del heap")
                walkGroupTree(tree, dataSeg, res, 0)
            }
            0x06 -> readLink(m, res)
            0x02 -> {                                   // información de enlaces: heap fractal => almacenamiento denso
                val flags = u8(m.pos + 1)
                var p = m.pos + 2
                if (flags and 1 != 0) p += 8
                val heap = off(p); val bt = off(p + oSize)
                if (heap != UNDEF) {                    // enlaces densos: B-tree v2 (tipo 5) -> IDs de heap fractal -> mensajes de enlace
                    for (pos in denseObjects(heap, bt, 5, 4)) readLink(Msg(6, pos, 0), res)
                }
            }
        }
        return res
    }

    private fun walkGroupTree(addr: Long, dataSeg: Int, out: MutableMap<String, Long>, depth: Int) {
        if (depth > 16) throw Hdf5FormatException("B-tree de grupo demasiado profundo")
        val a = ck(addr, "B-tree de grupo")
        if (!sigAt(a, "TREE")) throw Hdf5FormatException("B-tree de grupo sin firma")
        val level = u8(a + 5); val n = u16(a + 6)
        val p0 = a + 8 + 2 * oSize
        for (i in 0 until n) {
            val child = off(p0 + lSize + i * (lSize + oSize))
            if (level > 0) walkGroupTree(child, dataSeg, out, depth + 1)
            else {
                val s = ck(child, "nodo de símbolos")
                if (!sigAt(s, "SNOD")) throw Hdf5FormatException("nodo de símbolos sin firma")
                val ns = u16(s + 6)
                for (k in 0 until ns) {
                    val e = s + 8 + k * (2 * oSize + 24)
                    out[cstr(dataSeg + off(e).toInt())] = off(e + oSize)
                }
            }
        }
    }

    private fun readLink(m: Msg, out: MutableMap<String, Long>) {
        var p = m.pos
        if (u8(p) != 1) return
        val flags = u8(p + 1); p += 2
        var type = 0
        if (flags and 0x08 != 0) { type = u8(p); p += 1 }
        if (flags and 0x04 != 0) p += 8
        if (flags and 0x10 != 0) p += 1
        val nl = uint(p, 1 shl (flags and 3)).toInt(); p += 1 shl (flags and 3)
        val name = String(ByteArray(nl) { b.get(p + it) }, Charsets.UTF_8); p += nl
        if (type == 0) out[name] = off(p)               // enlaces blandos/externos se ignoran
    }

    private fun cstr(p: Int): String {
        var e = p
        while (e < b.limit() && b.get(e).toInt() != 0) e++
        return String(ByteArray(e - p) { b.get(p + it) }, Charsets.UTF_8)
    }

    // ---- API pública -------------------------------------------------------------------------------------------
    /** Nombres de los objetos hijos de la raíz. */
    fun rootNames(): List<String> = children(rootAddr).keys.toList()

    /** Atributos de cadena del grupo raíz (Conventions, SOFAConventions, ...). */
    fun rootAttributes(): Map<String, String> = attributes(messages(rootAddr))

    fun hasDataset(name: String): Boolean = children(rootAddr).containsKey(name)

    fun dataset(name: String): Dataset {
        val addr = children(rootAddr)[name] ?: throw Hdf5FormatException("no existe el dataset '$name'")
        val msgs = messages(addr)
        var dims: IntArray? = null
        var tClass = -1; var tSize = 0; var big = false
        var lClass = -1; var dAddr = UNDEF; var dSize = 0L; var cDims: IntArray? = null
        val filters = ArrayList<Filter>()
        for (m in msgs) when (m.type) {
            0x01 -> {
                val ver = u8(m.pos); val rank = u8(m.pos + 1)
                val start = if (ver == 1) m.pos + 8 else m.pos + 4
                dims = IntArray(rank) { uint(start + it * lSize, lSize).toInt() }
            }
            0x03 -> {
                tClass = u8(m.pos) and 0x0F; big = (u8(m.pos + 1) and 1) != 0
                tSize = u32(m.pos + 4).toInt()
            }
            0x08 -> {
                val ver = u8(m.pos)
                if (ver != 3) throw Hdf5FormatException("layout v$ver no soportado (solo v3)")
                lClass = u8(m.pos + 1)
                when (lClass) {
                    0 -> { dSize = u16(m.pos + 2).toLong(); dAddr = (m.pos + 4).toLong() }
                    1 -> { dAddr = off(m.pos + 2); dSize = len(m.pos + 2 + oSize) }
                    2 -> {
                        val nd = u8(m.pos + 2)
                        dAddr = off(m.pos + 3)
                        cDims = IntArray(nd - 1) { u32(m.pos + 3 + oSize + 4 * it).toInt() }
                        val es = u32(m.pos + 3 + oSize + 4 * (nd - 1)).toInt()
                        if (tSize != 0 && es != tSize) throw Hdf5FormatException("tamaño de elemento del layout ($es) != tipo ($tSize)")
                    }
                    else -> throw Hdf5FormatException("clase de layout $lClass no soportada")
                }
            }
            0x0B -> readFilters(m, filters)
        }
        if (dims == null || tClass < 0 || lClass < 0) throw Hdf5FormatException("dataset '$name' incompleto (dataspace/tipo/layout)")
        return Dataset(dims, tClass, tSize, big, lClass, dAddr, dSize, cDims, filters, attributes(msgs))
    }

    private fun readFilters(m: Msg, out: MutableList<Filter>) {
        val ver = u8(m.pos); val n = u8(m.pos + 1)
        var p = m.pos + if (ver == 1) 8 else 2
        for (i in 0 until n) {
            val id = u16(p); p += 2
            var nameLen = 0
            if (ver == 1 || id >= 256) { nameLen = u16(p); p += 2 }
            p += 2                                     // flags
            val nv = u16(p); p += 2
            if (ver == 1) p += (nameLen + 7) / 8 * 8 else p += nameLen
            val cv = IntArray(nv) { u32(p + 4 * it).toInt() }
            p += 4 * nv
            if (ver == 1 && nv % 2 == 1) p += 4
            out.add(Filter(id, cv))
        }
    }

    private fun attributes(msgs: List<Msg>): Map<String, String> {
        val res = LinkedHashMap<String, String>()
        for (m in msgs) {
            if (m.type == 0x0C) parseAttribute(m.pos, res)
            else if (m.type == 0x15) {                   // información de atributos: densos si hay heap fractal
                val flags = u8(m.pos + 1)
                var p = m.pos + 2
                if (flags and 1 != 0) p += 2
                val heap = off(p); val bt = off(p + oSize)
                if (heap != UNDEF && bt != UNDEF) for (pos in denseObjects(heap, bt, 8, 0)) parseAttribute(pos, res)
            }
        }
        return res
    }

    private fun parseAttribute(mpos: Int, res: MutableMap<String, String>) {
        runCatching {
            val ver = u8(mpos)
            if (ver !in 1..3) return@runCatching
            val nameSize = u16(mpos + 2); val dtSize = u16(mpos + 4); val dsSize = u16(mpos + 6)
            var p = mpos + 8 + if (ver == 3) 1 else 0
            val pad = ver == 1
            fun adv(n: Int) = if (pad) (n + 7) / 8 * 8 else n
            val name = cstr(p); p += adv(nameSize)
            val dtPos = p; p += adv(dtSize)
            p += adv(dsSize)
            val cls = u8(dtPos) and 0x0F
            if (cls == 3) {                              // cadena de longitud fija
                val sz = u32(dtPos + 4).toInt()
                val bytes = ByteArray(sz) { b.get(p + it) }
                res[name] = String(bytes, Charsets.UTF_8).trimEnd('\u0000', ' ')
            }
        }
    }

    // ---- almacenamiento denso: B-tree v2 + heap fractal ---------------------------------------------------------
    /**
     * Posiciones absolutas de los objetos del heap fractal referenciados por un B-tree v2 de nombres.
     * Solo soporta B-tree de profundidad 0 (una sola hoja) y heaps con raíz directa o indirecta de bloques directos;
     * cualquier otra cosa lanza [Hdf5FormatException].
     */
    private fun denseObjects(heapAddr: Long, treeAddr: Long, recordType: Int, idOffsetInRecord: Int): List<Int> {
        val t = ck(treeAddr, "B-tree v2")
        if (!sigAt(t, "BTHD")) throw Hdf5FormatException("B-tree v2 sin firma BTHD")
        if (u8(t + 5) != recordType) throw Hdf5FormatException("B-tree v2 de tipo ${u8(t + 5)}, se esperaba $recordType")
        val nodeSize = u32(t + 6).toInt(); val recSize = u16(t + 10); val depth = u16(t + 12)
        val root = off(t + 16); val nRec = u16(t + 16 + oSize)
        if (nRec == 0) return emptyList()
        val h = ck(heapAddr, "heap fractal")
        if (!sigAt(h, "FRHP")) throw Hdf5FormatException("heap fractal sin firma FRHP")
        val out = ArrayList<Int>(nRec)
        for (rp in btree2Records(root, nRec, depth, nodeSize, recSize)) out.add(heapObjectPos(h, rp + idOffsetInRecord))
        return out
    }

    private fun encSize(v: Long): Int = (63 - java.lang.Long.numberOfLeadingZeros(maxOf(v, 1L)) + 8) / 8

    /** Posiciones de TODOS los registros de un B-tree v2 (hojas y nodos internos), de cualquier profundidad. */
    private fun btree2Records(root: Long, nRoot: Int, depth: Int, nodeSize: Int, recSize: Int): List<Int> {
        if (depth > 8) throw Hdf5FormatException("B-tree v2 demasiado profundo: $depth")
        val maxRec = IntArray(depth + 1); val nrecSz = IntArray(depth + 1)
        val cumMax = LongArray(depth + 1); val cumSz = IntArray(depth + 1)
        maxRec[0] = (nodeSize - 10) / recSize; nrecSz[0] = encSize(maxRec[0].toLong())
        cumMax[0] = maxRec[0].toLong(); cumSz[0] = nrecSz[0]
        for (d in 1..depth) {
            val ptr = oSize + nrecSz[d - 1] + (if (d - 1 > 0) cumSz[d - 1] else 0)
            maxRec[d] = (nodeSize - 10 - ptr) / (recSize + ptr); nrecSz[d] = encSize(maxRec[d].toLong())
            cumMax[d] = (maxRec[d] + 1) * cumMax[d - 1] + maxRec[d]; cumSz[d] = encSize(cumMax[d])
        }
        val out = ArrayList<Int>()
        fun walk(addr: Long, n: Int, d: Int) {
            val a = ck(addr, "nodo B-tree v2")
            if (d == 0) {
                if (!sigAt(a, "BTLF")) throw Hdf5FormatException("hoja B-tree v2 sin firma BTLF")
                for (i in 0 until n) out.add(a + 6 + i * recSize)
            } else {
                if (!sigAt(a, "BTIN")) throw Hdf5FormatException("nodo interno B-tree v2 sin firma BTIN")
                for (i in 0 until n) out.add(a + 6 + i * recSize)
                val ptrSize = oSize + nrecSz[d - 1] + (if (d - 1 > 0) cumSz[d - 1] else 0)
                val p0 = a + 6 + n * recSize
                for (i in 0..n) {
                    val pp = p0 + i * ptrSize
                    walk(off(pp), uint(pp + oSize, nrecSz[d - 1]).toInt(), d - 1)
                }
            }
        }
        walk(root, nRoot, depth)
        return out
    }

    /** Posición absoluta de un objeto gestionado del heap fractal a partir de su ID (en [idPos]). */
    private fun heapObjectPos(h: Int, idPos: Int): Int {
        val maxHeapBits = u16(h + 4 + 1 + 2 + 2 + 1 + 4 + lSize + oSize + lSize + oSize + lSize * 8 + 2 + lSize + lSize)
        val tableWidth = u16(h + 4 + 1 + 2 + 2 + 1 + 4 + lSize + oSize + lSize + oSize + lSize * 8)
        val startBlock = len(h + 4 + 1 + 2 + 2 + 1 + 4 + lSize + oSize + lSize + oSize + lSize * 8 + 2).toInt()
        val maxDirect = len(h + 4 + 1 + 2 + 2 + 1 + 4 + lSize + oSize + lSize + oSize + lSize * 8 + 2 + lSize).toInt()
        val afterBits = h + 4 + 1 + 2 + 2 + 1 + 4 + lSize + oSize + lSize + oSize + lSize * 8 + 2 + lSize + lSize + 2
        val rootRows = u16(afterBits + 2 + oSize)
        val rootAddr = off(afterBits + 2)
        val maxManaged = u32(h + 4 + 1 + 2 + 2 + 1).toInt()
        val ioFilterLen = u16(h + 4 + 1 + 2)
        if (ioFilterLen != 0) throw Hdf5FormatException("heap fractal con filtros no soportado")

        val b0 = u8(idPos)
        if ((b0 shr 4) and 3 != 0) throw Hdf5FormatException("objeto huge/tiny del heap fractal no soportado")
        val offBytes = (maxHeapBits + 7) / 8
        val objOff = uint(idPos + 1, offBytes).toInt()

        if (rootRows == 0) {                            // raíz = un bloque directo
            val d = ck(rootAddr, "bloque directo"); if (!sigAt(d, "FHDB")) throw Hdf5FormatException("bloque directo sin FHDB")
            if (objOff >= startBlock) throw Hdf5FormatException("objeto fuera del bloque directo raíz")
            return d + objOff
        }
        // raíz indirecta: localizar fila/columna en la tabla de doblado
        val ib = ck(rootAddr, "bloque indirecto"); if (!sigAt(ib, "FHIB")) throw Hdf5FormatException("bloque indirecto sin FHIB")
        var rowStart = 0L
        for (r in 0 until rootRows) {
            val size = if (r < 2) startBlock.toLong() else startBlock.toLong() shl (r - 1)
            if (size > maxDirect) throw Hdf5FormatException("fila indirecta anidada del heap no soportada")
            val rowEnd = rowStart + size * tableWidth
            if (objOff < rowEnd) {
                val col = ((objOff - rowStart) / size).toInt()
                val entry = ib + 5 + oSize + offBytes + (r * tableWidth + col) * oSize
                val da = ck(off(entry), "bloque directo hijo")
                if (!sigAt(da, "FHDB")) throw Hdf5FormatException("bloque directo hijo sin FHDB")
                return da + (objOff - (rowStart + col * size)).toInt()
            }
            rowStart = rowEnd
        }
        throw Hdf5FormatException("objeto del heap fuera de la tabla de doblado (offset $objOff, maxManaged $maxManaged)")
    }

    // ---- lectura de datos --------------------------------------------------------------------------------------
    /** Lee el dataset completo como doubles (flotante de 32 o 64 bits, cualquier endianness). */
    fun readDoubles(ds: Dataset): DoubleArray {
        if (ds.typeClass != 1 || (ds.elementSize != 4 && ds.elementSize != 8))
            throw Hdf5FormatException("tipo no soportado (clase ${ds.typeClass}, ${ds.elementSize} bytes): solo flotante 32/64")
        val raw = readRaw(ds)
        val n = ds.elementCount.toInt()
        val bb = ByteBuffer.wrap(raw).order(if (ds.bigEndian) ByteOrder.BIG_ENDIAN else ByteOrder.LITTLE_ENDIAN)
        return DoubleArray(n) { if (ds.elementSize == 8) bb.getDouble(it * 8) else bb.getFloat(it * 4).toDouble() }
    }

    private fun readRaw(ds: Dataset): ByteArray {
        val total = ds.elementCount * ds.elementSize
        if (total > Int.MAX_VALUE) throw Hdf5FormatException("dataset demasiado grande")
        val out = ByteArray(total.toInt())
        when (ds.layoutClass) {
            0 -> System.arraycopy(b.array(), ds.dataAddr.toInt(), out, 0, minOf(out.size, ds.dataSize.toInt()))
            1 -> if (ds.dataAddr != UNDEF) {
                val a = ck(ds.dataAddr, "datos contiguos")
                if (a.toLong() + minOf(total, ds.dataSize) > b.limit()) throw Hdf5FormatException("datos contiguos fuera de archivo")
                System.arraycopy(b.array(), a, out, 0, minOf(out.size.toLong(), ds.dataSize).toInt())
            }
            2 -> if (ds.dataAddr != UNDEF) readChunked(ds, out, ds.dataAddr, 0)
        }
        return out
    }

    private fun readChunked(ds: Dataset, out: ByteArray, addr: Long, depth: Int) {
        if (depth > 16) throw Hdf5FormatException("B-tree de chunks demasiado profundo")
        val a = ck(addr, "B-tree de chunks")
        if (!sigAt(a, "TREE") || u8(a + 4) != 1) throw Hdf5FormatException("B-tree de chunks inválido")
        val level = u8(a + 5); val n = u16(a + 6)
        val nd = ds.rank + 1
        val keySize = 8 + 8 * nd
        val p0 = a + 8 + 2 * oSize
        for (i in 0 until n) {
            val kp = p0 + i * (keySize + oSize)
            val child = off(kp + keySize)
            if (level > 0) { readChunked(ds, out, child, depth + 1); continue }
            val size = u32(kp).toInt(); val mask = u32(kp + 4).toInt()
            val co = IntArray(ds.rank) { uint(kp + 8 + 8 * it, 8).toInt() }
            val ca = ck(child, "chunk")
            if (ca.toLong() + size > b.limit()) throw Hdf5FormatException("chunk fuera de archivo")
            var data = ByteArray(size).also { System.arraycopy(b.array(), ca, it, 0, size) }
            val chunkBytes = ds.chunkDims!!.fold(1L) { x, d -> x * d }.toInt() * ds.elementSize
            for (fi in ds.filters.indices.reversed()) {
                if ((mask shr fi) and 1 == 1) continue
                val f = ds.filters[fi]
                data = when (f.id) {
                    1 -> inflate(data, chunkBytes)
                    2 -> unshuffle(data, if (f.clientValues.isNotEmpty()) f.clientValues[0] else ds.elementSize)
                    3 -> data.copyOf(maxOf(0, data.size - 4))
                    else -> throw Hdf5FormatException("filtro ${f.id} no soportado")
                }
            }
            if (data.size < chunkBytes) throw Hdf5FormatException("chunk descomprimido más corto de lo esperado")
            copyChunk(ds, data, co, out)
        }
    }

    private fun inflate(src: ByteArray, expected: Int): ByteArray {
        val inf = Inflater()
        try {
            inf.setInput(src)
            val out = ByteArray(expected)
            var got = 0
            while (got < expected) {
                val r = inf.inflate(out, got, expected - got)
                if (r == 0 && (inf.finished() || inf.needsInput() || inf.needsDictionary())) break
                got += r
            }
            if (got < expected) throw Hdf5FormatException("deflate: $got de $expected bytes")
            return out
        } catch (e: java.util.zip.DataFormatException) {
            throw Hdf5FormatException("deflate corrupto: ${e.message}")
        } finally { inf.end() }
    }

    private fun unshuffle(src: ByteArray, elem: Int): ByteArray {
        if (elem <= 1) return src
        val n = src.size / elem
        val out = ByteArray(src.size)
        for (j in 0 until elem) for (e in 0 until n) out[e * elem + j] = src[j * n + e]
        for (i in n * elem until src.size) out[i] = src[i]
        return out
    }

    private fun copyChunk(ds: Dataset, chunk: ByteArray, co: IntArray, out: ByteArray) {
        val rank = ds.rank; val cd = ds.chunkDims!!; val dd = ds.dims; val es = ds.elementSize
        for (d in 0 until rank) if (co[d] >= dd[d]) return
        val lastLen = minOf(cd[rank - 1], dd[rank - 1] - co[rank - 1])
        val idx = IntArray(rank)                          // índice dentro del chunk, salvo la última dimensión (siempre 0)
        while (true) {
            var inOff = 0L; var outOff = 0L
            var ok = true
            for (d in 0 until rank - 1) if (co[d] + idx[d] >= dd[d]) { ok = false; break }
            if (ok) {
                var cStride = 1L; var dStride = 1L
                for (d in rank - 1 downTo 0) {
                    inOff += idx[d] * cStride; outOff += (co[d] + idx[d]).toLong() * dStride
                    cStride *= cd[d]; dStride *= dd[d]
                }
                System.arraycopy(chunk, (inOff * es).toInt(), out, (outOff * es).toInt(), lastLen * es)
            }
            var d = rank - 2
            while (d >= 0) { idx[d]++; if (idx[d] < cd[d]) break; idx[d] = 0; d-- }
            if (d < 0) break
        }
    }
}
