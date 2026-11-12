package com.ivanna.omega.tidal

import android.util.Base64
import android.util.Log
import android.util.Xml
import org.json.JSONObject
import org.xmlpull.v1.XmlPullParser
import java.io.File
import java.io.FileOutputStream

/**
 * TIDAL — resolvedor de streams (v2.5.416).
 *
 * Entrada: TidalPlaybackInfo de playbackinfopostpaywall (manifiesto en
 * base64 + calidad ENTREGADA por el servidor). Salida: algo que el
 * BridgePlayer puede reproducir.
 *
 * Dos rutas, verificadas contra tidalapi (media.py) y el sondeo en vivo de
 * cliamp PR #313 (ago 2026):
 * - BTS (manifestMimeType con vnd.tidal.bts): JSON {urls[], codecs,
 *   mimeType, encryptionType}. URL directa → se reproduce tal cual.
 * - DASH (manifestMimeType dash+xml): MPD con SegmentTemplate
 *   (initialization + media con $Number$) y SegmentTimeline (Ss d/r).
 *   Se descargan init + segmentos en orden a un archivo temporal
 *   (fMP4 válido) y se reproduce ese archivo. Streaming only: el archivo
 *   vive en cacheDir y se borra tras sonar (sin descarga para guardar).
 *
 * REGLA DURA: encryptionType != NONE (o ContentProtection en el MPD) →
 * NO se reproduce. Sin bypass de DRM, jamás. Se lanza
 * TidalEncryptedException y la UI lo dice claro.
 *
 * SEGURIDAD: las URLs de segmentos son URLs firmadas de CDN; NUNCA se les
 * envía el Bearer token (eso lo filtraría a un tercero). La descarga va
 * sin cabecera de autorización.
 */
class TidalEncryptedException(message: String) : Exception(message)

data class ResolvedStream(
    /** URI para BridgePlayer.play(): URL https (BTS) o file:// del temporal (DASH). */
    val playbackUri: String,
    /** Calidad ENTREGADA real, p. ej. "FLAC 24-bit/96 kHz" o "AAC 320 kbps". */
    val qualityLabel: String,
    /** Archivo temporal (solo DASH). El controlador lo borra tras sonar. */
    val downloadedFile: File? = null
)

open class TidalStreamResolver(private val http: TidalHttp) {

    companion object {
        private const val TAG = "IVANNA.TidalStream"
        private const val MAX_SEGMENT_BYTES = 64L * 1024 * 1024
        private const val MAX_TOTAL_BYTES = 512L * 1024 * 1024
    }

    open fun resolve(info: TidalPlaybackInfo, cacheDir: File): ResolvedStream {
        val manifestText = runCatching {
            String(Base64.decode(info.manifestBase64, Base64.DEFAULT), Charsets.UTF_8)
        }.getOrElse { throw TidalApiException("Manifiesto de Tidal ilegible") }
        return when {
            "bts" in info.manifestMimeType.lowercase() -> resolveBts(info, manifestText)
            "dash" in info.manifestMimeType.lowercase() -> resolveDash(info, manifestText, cacheDir)
            else -> throw TidalApiException("Formato de stream de Tidal no reconocido (${info.manifestMimeType})")
        }
    }

    // ── BTS: URL directa ──────────────────────────────────────────────────

    private fun resolveBts(info: TidalPlaybackInfo, manifestText: String): ResolvedStream {
        val json = runCatching { JSONObject(manifestText) }
            .getOrElse { throw TidalApiException("Manifiesto BTS de Tidal ilegible") }
        val encryption = json.optString("encryptionType", "NONE")
        if (encryption.isNotBlank() && encryption.uppercase() != "NONE") {
            throw TidalEncryptedException(
                "Esta pista está protegida y no se puede reproducir en IVANNA"
            )
        }
        val urls = json.optJSONArray("urls")
        val url = urls?.optString(0, "")?.takeIf { it.isNotBlank() }
            ?: throw TidalApiException("Tidal no dio URL de stream para esta pista")
        val codecs = json.optString("codecs", "")
        return ResolvedStream(url, qualityLabel(info, codecs), null)
    }

    // ── DASH: descargar segmentos a archivo temporal ─────────────────────

    private fun resolveDash(info: TidalPlaybackInfo, mpdText: String, cacheDir: File): ResolvedStream {
        val root = runCatching { parseXml(mpdText) }
            .getOrElse { throw TidalApiException("Manifiesto DASH de Tidal ilegible") }
        if (root.findAll("ContentProtection").isNotEmpty()) {
            throw TidalEncryptedException(
                "Esta pista está protegida y no se puede reproducir en IVANNA"
            )
        }
        val adaptationSets = root.findAll("AdaptationSet")
        if (adaptationSets.isEmpty()) throw TidalApiException("El manifiesto DASH no trae audio")
        val audioSet = adaptationSets.firstOrNull { set ->
            val ct = set.attrs["contentType"] ?: ""
            val mt = set.attrs["mimeType"] ?: ""
            ct == "audio" || "audio" in mt
        } ?: adaptationSets.first()
        val representation = audioSet.children.firstOrNull { it.name == "Representation" }
            ?: throw TidalApiException("El manifiesto DASH no trae representación de audio")
        val template = representation.children.firstOrNull { it.name == "SegmentTemplate" }
            ?: audioSet.children.firstOrNull { it.name == "SegmentTemplate" }
            ?: throw TidalApiException("El manifiesto DASH no trae SegmentTemplate")
        val initTemplate = template.attrs["initialization"]
            ?: throw TidalApiException("El manifiesto DASH no trae inicialización")
        val mediaTemplate = template.attrs["media"]
            ?: throw TidalApiException("El manifiesto DASH no trae plantilla de segmentos")
        val startNumber = template.attrs["startNumber"]?.toIntOrNull() ?: 0
        val timeline = template.children.firstOrNull { it.name == "SegmentTimeline" }
            ?: throw TidalApiException("El manifiesto DASH no trae SegmentTimeline")
        val segmentCount = timeline.children.filter { it.name == "S" }
            .sumOf { (it.attrs["r"]?.toIntOrNull() ?: 0) + 1 }
        if (segmentCount <= 0 || segmentCount > 10_000) {
            throw TidalApiException("El manifiesto DASH trae un número de segmentos imposible ($segmentCount)")
        }
        val baseUrl = representation.firstChildText("BaseURL")
            ?: audioSet.firstChildText("BaseURL")
            ?: root.firstChildText("BaseURL")
            ?: root.children.firstOrNull { it.name == "Period" }?.firstChildText("BaseURL")

        val segmentUrls = ArrayList<String>(segmentCount + 1)
        segmentUrls.add(resolveUrl(baseUrl, expandTemplate(initTemplate, startNumber, representation)))
        for (i in 0 until segmentCount) {
            val url = expandTemplate(mediaTemplate, startNumber + i, representation)
            segmentUrls.add(resolveUrl(baseUrl, url))
        }

        val outFile = File(cacheDir, "tidal_dash_${info.trackId}.m4s")
        var totalBytes = 0L
        try {
            FileOutputStream(outFile).use { out ->
                for (url in segmentUrls) {
                    val bytes = runCatching { http.requestBytes(url) }
                        .getOrElse { throw TidalApiException("No se pudo bajar el audio de Tidal: ${it.message}") }
                    if (bytes.size > MAX_SEGMENT_BYTES) throw TidalApiException("Segmento de Tidal demasiado grande")
                    totalBytes += bytes.size
                    if (totalBytes > MAX_TOTAL_BYTES) throw TidalApiException("Pista de Tidal demasiado grande")
                    out.write(bytes)
                }
            }
        } catch (e: Exception) {
            runCatching { outFile.delete() }
            throw e
        }
        Log.i(TAG, "DASH descargado: track=${info.trackId} segmentos=${segmentUrls.size} bytes=$totalBytes")
        val codecs = representation.attrs["codecs"] ?: audioSet.attrs["codecs"] ?: ""
        return ResolvedStream("file://${outFile.absolutePath}", qualityLabel(info, codecs), outFile)
    }

    // ── Etiqueta de calidad ENTREGADA (honestidad: lo que dio el servidor) ──

    fun qualityLabel(info: TidalPlaybackInfo, codecs: String): String {
        val isFlac = "flac" in codecs.lowercase() ||
            info.audioQualityDelivered == TidalApiClient.QUALITY_LOSSLESS ||
            info.audioQualityDelivered == TidalApiClient.QUALITY_HI_RES
        if (isFlac) {
            val depth = info.bitDepth
            val rate = info.sampleRate
            return if (depth != null && rate != null) {
                val khz = rate / 1000.0
                "FLAC $depth-bit/${"%.1f".format(khz)} kHz"
            } else "FLAC sin pérdida"
        }
        return when (info.audioQualityDelivered) {
            TidalApiClient.QUALITY_HIGH -> "AAC 320 kbps"
            TidalApiClient.QUALITY_LOW -> "AAC 96 kbps"
            else -> info.audioQualityDelivered
        }
    }

    // ── Utilidades MPD ────────────────────────────────────────────────────

    private fun expandTemplate(template: String, number: Int, representation: XmlNode): String {
        var out = template
        val repId = representation.attrs["id"] ?: ""
        out = out.replace("\$RepresentationID\$", repId)
        // $Number$ y variantes con formato $Number%05d$
        out = out.replace(Regex("\\\$Number(?:%0(\\d+)d)?\\\$")) { match ->
            val width = match.groupValues[1].toIntOrNull()
            if (width != null) "%0${width}d".format(number) else number.toString()
        }
        return out
    }

    private fun resolveUrl(baseUrl: String?, url: String): String {
        if (url.startsWith("http://") || url.startsWith("https://") || url.startsWith("file://")) return url
        val base = baseUrl?.trim().orEmpty()
        if (base.isEmpty()) throw TidalApiException("El manifiesto DASH trae URLs relativas sin BaseURL")
        val dir = if (base.endsWith("/")) base else base.substring(0, base.lastIndexOf('/') + 1)
        return dir + url
    }

    // ── Mini-DOM XML (XmlPullParser; sin dependencias nuevas) ────────────

    private class XmlNode(
        val name: String,
        val attrs: Map<String, String>,
        val children: MutableList<XmlNode> = mutableListOf(),
        var text: String = ""
    ) {
        fun findAll(tag: String): List<XmlNode> {
            val out = ArrayList<XmlNode>()
            if (name == tag) out.add(this)
            for (child in children) out.addAll(child.findAll(tag))
            return out
        }

        fun firstChildText(tag: String): String? {
            val node = children.firstOrNull { it.name == tag } ?: return null
            return node.text.trim().takeIf { it.isNotEmpty() }
        }
    }

    private fun parseXml(xml: String): XmlNode {
        val parser: XmlPullParser = Xml.newPullParser()
        parser.setFeature(XmlPullParser.FEATURE_PROCESS_NAMESPACES, false)
        parser.setInput(xml.reader())
        var root: XmlNode? = null
        val stack = ArrayDeque<XmlNode>()
        var event = parser.eventType
        while (event != XmlPullParser.END_DOCUMENT) {
            when (event) {
                XmlPullParser.START_TAG -> {
                    val attrs = HashMap<String, String>()
                    for (i in 0 until parser.attributeCount) {
                        attrs[parser.getAttributeName(i)] = parser.getAttributeValue(i)
                    }
                    val node = XmlNode(parser.name, attrs)
                    stack.lastOrNull()?.children?.add(node) ?: run { root = node }
                    stack.addLast(node)
                }
                XmlPullParser.TEXT -> {
                    stack.lastOrNull()?.let { it.text += parser.text }
                }
                XmlPullParser.END_TAG -> {
                    if (stack.isNotEmpty()) stack.removeLast()
                }
            }
            event = parser.next()
        }
        return root ?: throw TidalApiException("Manifiesto DASH vacío")
    }
}
