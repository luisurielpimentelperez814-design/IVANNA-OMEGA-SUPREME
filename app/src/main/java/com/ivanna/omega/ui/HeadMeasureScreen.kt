package com.ivanna.omega.ui

import android.Manifest
import android.content.pm.PackageManager
import android.graphics.Bitmap
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.core.ParameterStore
import com.ivanna.omega.ui.theme.MagistralGoldHi
import com.ivanna.omega.ui.theme.MagistralMuted
import com.ivanna.omega.ui.theme.MagistralSerif
import com.ivanna.omega.ui.theme.MagistralText
import com.ivanna.omega.ui.theme.MagistralVoid
import kotlin.math.roundToInt

/**
 * Ficha magistral — Pantalla VI · Spatial, módulo L (prototipo aprobado,
 * texto exacto). Esta pantalla ES la rama "Medida" de la HRTF: la foto con
 * tarjeta de referencia mide tu cabeza para el perfil propio. Sin
 * equivalente exacto en el código: no existe aquí ningún selector
 * Genérica / Medida / Firmas élite; el botón va junto al título.
 */
private val hrtfSpec = SpecInfo(
    title = "HRTF · Función de tu cabeza",
    what = "La HRTF describe cómo tu anatomía colorea el sonido según de dónde viene. Sin ella, el audio espacial suena a altavoz pegado a la oreja; con ella, las fuentes salen de tu cabeza y se colocan en el espacio. Genérica: promedio poblacional. Medida: tu perfil. Firmas élite: curvas de referencia medidas de productos insignia y la curva Harman.",
    purpose = "Es la diferencia entre «ancho» y localizable: con la HRTF correcta distingues si una fuente está a 30 cm o a 2 m, delante o detrás, sin pensarlo.",
    whenUse = "Con audífonos, siempre. Medida si tienes tu perfil; Firmas élite para comparar tu cadena contra las referencias de la industria; Genérica como punto de partida digno.",
    whenAvoid = "Con bocinas la HRTF no aplica igual: ahí gobiernan sala y campo. Una HRTF ajena muy distinta a tu anatomía produce el efecto contrario: todo suena «dentro del cráneo» y con el timbre torcido."
)

/**
 * HeadMeasureScreen — "Medir mi cabeza" (v2.5.389).
 *
 * La foto que alimenta la Ec. 2 del campo Ψ y el render ORBE: el radio de
 * cabeza personalizado. Sin medición, IVANNA usa KEMAR (8.75 cm) y el sonido
 * no cambia; con medición, el ILD/ITD se calculan con TU cabeza.
 *
 * Cómo tomar la foto (esto es lo que ve el usuario):
 *  1. De frente, con buena luz, la cabeza centrada y llenando la foto.
 *  2. Una tarjeta de crédito/débito pegada VERTICAL en la frente o la sien,
 *     a la misma distancia que la cabeza (es la regla: mide 8.56 cm).
 *  3. Que se vean las dos orejas y la tarjeta completa.
 * Después, en la foto, arrastra las líneas verdes a los bordes de la cabeza
 * (de oreja a oreja) y las líneas ámbar a los bordes de la tarjeta. La app
 * calcula el ancho real y el radio. También se puede escribir el ancho a mano
 * (medido con cinta, de oreja a oreja).
 *
 * La foto NO se sube a ningún lado: vive solo en la memoria de esta pantalla
 * y se descarta al salir. No se guarda, no se comparte.
 */
@Composable
fun HeadMeasureScreen(onBack: () -> Unit = {}) {
    val context = LocalContext.current
    val paramStore = remember { ParameterStore(context) }
    var bitmap by remember { mutableStateOf<Bitmap?>(null) }
    var hasCameraPermission by remember {
        mutableStateOf(
            ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) ==
                PackageManager.PERMISSION_GRANTED
        )
    }
    var status by remember { mutableStateOf("") }
    var appliedRadiusM by remember { mutableStateOf(paramStore.getOrbeHeadRadiusM()) }

    val pictureLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.TakePicturePreview()
    ) { bmp ->
        if (bmp != null) {
            bitmap = bmp
            status = "Foto lista. Arrastra las líneas a los bordes."
        } else {
            status = "No se tomó la foto. Inténtalo de nuevo."
        }
    }
    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        hasCameraPermission = granted
        if (granted) pictureLauncher.launch(null)
        else status = "Sin permiso de cámara no se puede tomar la foto. Puedes escribir el ancho a mano."
    }

    // Marcadores (fracción 0..1 del ancho de la foto).
    var headL by remember { mutableFloatStateOf(0.28f) }
    var headR by remember { mutableFloatStateOf(0.72f) }
    var cardL by remember { mutableFloatStateOf(0.44f) }
    var cardR by remember { mutableFloatStateOf(0.56f) }
    var boxWidthPx by remember { mutableFloatStateOf(1f) }
    var manualWidthText by remember { mutableStateOf("") }

    val bmp = bitmap
    val measuredRadiusM: Float? = if (bmp != null) {
        HeadMeasureMath.headRadiusMFromSpans(
            (headR - headL) * boxWidthPx,
            (cardR - cardL) * boxWidthPx
        )
    } else null
    val measuredWidthCm: Float? = if (bmp != null) {
        HeadMeasureMath.headWidthCmFromSpans(
            (headR - headL) * boxWidthPx,
            (cardR - cardL) * boxWidthPx
        )
    } else null

    fun applyRadius(radiusM: Float) {
        val r = radiusM.coerceIn(0.05f, 0.15f)
        paramStore.setOrbeHeadRadiusM(r)
        runCatching { IvannaNativeLib.nativeSetOrbeHeadRadiusM(r) }
        appliedRadiusM = r
        status = "Aplicado: radio %.1f cm (ancho de cabeza %.1f cm). Ya suena con tu cabeza.".format(
            r * 100f, r * 200f
        )
    }

    Column(
        modifier = Modifier.fillMaxSize().background(MagistralVoid)
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text("MEDIR MI CABEZA", fontSize = 20.sp, fontWeight = FontWeight.Bold, color = MagistralGoldHi, fontFamily = MagistralSerif, modifier = Modifier.weight(1f))
            SpecInfoButton(hrtfSpec)
        }
        Text(
            "IVANNA calcula el sonido 3D con el tamaño de tu cabeza. Sin medirla usa el estándar (8.75 cm de radio) y suena igual que siempre. Con tu medida, la posición de los sonidos se ajusta a ti.",
            fontSize = 14.sp, color = MagistralMuted
        )
        Text(
            "Radio actual: %.1f cm %s".format(
                appliedRadiusM * 100f,
                if (appliedRadiusM == 0.0875f) "(estándar)" else "(tu medida)"
            ),
            fontSize = 14.sp, color = MagistralText
        )

        Text("CÓMO TOMAR LA FOTO", fontSize = 14.sp, fontWeight = FontWeight.Bold, color = MagistralGoldHi, fontFamily = MagistralSerif)
        Text(
            "1. De frente, con buena luz, tu cabeza centrada y llenando la foto.\n" +
                "2. Una tarjeta de crédito o débito pegada VERTICAL en tu frente o sien, a la misma distancia que tu cabeza. Es la regla: mide 8.56 cm.\n" +
                "3. Que se vean tus dos orejas y la tarjeta completa.\n" +
                "La foto no se guarda ni se sube a ningún lado: solo vive en esta pantalla.",
            fontSize = 14.sp, color = MagistralMuted
        )

        Button(onClick = {
            if (hasCameraPermission) pictureLauncher.launch(null)
            else permissionLauncher.launch(Manifest.permission.CAMERA)
        }) { Text(if (bmp == null) "TOMAR FOTO" else "TOMAR OTRA FOTO") }

        if (bmp != null) {
            Box(
                modifier = Modifier.fillMaxWidth().height(320.dp)
                    .onSizeChanged { boxWidthPx = it.width.toFloat() }
            ) {
                Image(
                    bitmap = bmp.asImageBitmap(),
                    contentDescription = "Foto frontal para medir la cabeza",
                    contentScale = ContentScale.FillBounds,
                    modifier = Modifier.fillMaxSize()
                )
                // Líneas de la cabeza (verdes) y de la tarjeta (ámbar), arrastrables.
                MarkerLine(fraction = headL, widthPx = boxWidthPx, color = Color(0xFF4CAF50)) { headL = it }
                MarkerLine(fraction = headR, widthPx = boxWidthPx, color = Color(0xFF4CAF50)) { headR = it }
                MarkerLine(fraction = cardL, widthPx = boxWidthPx, color = Color(0xFFFFC107)) { cardL = it }
                MarkerLine(fraction = cardR, widthPx = boxWidthPx, color = Color(0xFFFFC107)) { cardR = it }
            }
            Text(
                "Verde = bordes de tu cabeza (de oreja a oreja). Ámbar = bordes de la tarjeta. Arrastra cada línea hasta su borde.",
                fontSize = 13.sp, color = MagistralMuted
            )
            if (measuredWidthCm != null && measuredRadiusM != null) {
                Text(
                    "Ancho medido: %.1f cm  →  radio: %.1f cm".format(measuredWidthCm, measuredRadiusM * 100f),
                    fontSize = 15.sp, fontWeight = FontWeight.Bold, color = MagistralText
                )
                Button(onClick = { applyRadius(measuredRadiusM) }) { Text("APLICAR ESTA MEDIDA") }
            } else {
                Text(
                    "No se puede medir así: revisa que las líneas ámbar cubran justo la tarjeta y las verdes justo tu cabeza.",
                    fontSize = 14.sp, color = Color(0xFFFF8A80)
                )
            }
        }

        Text("O ESCRIBE EL ANCHO A MANO", fontSize = 14.sp, fontWeight = FontWeight.Bold, color = MagistralGoldHi, fontFamily = MagistralSerif)
        Text(
            "Mídelo con una cinta, de oreja a oreja, en centímetros. Lo normal es entre 14 y 19 cm.",
            fontSize = 14.sp, color = MagistralMuted
        )
        Row(verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                value = manualWidthText,
                onValueChange = { manualWidthText = it.filter { c -> c.isDigit() || c == '.' || c == ',' } },
                label = { Text("Ancho (cm)") },
                modifier = Modifier.width(140.dp)
            )
            Spacer(modifier = Modifier.width(8.dp))
            Button(onClick = {
                val w = manualWidthText.replace(',', '.').toFloatOrNull()
                if (w == null || w !in 10f..25f) {
                    status = "Ese ancho no es válido (tiene que estar entre 10 y 25 cm)."
                } else {
                    applyRadius(HeadMeasureMath.headRadiusMFromWidthCm(w))
                }
            }) { Text("APLICAR") }
        }

        OutlinedButton(onClick = { applyRadius(0.0875f) }) { Text("VOLVER AL ESTÁNDAR (8.75 cm)") }
        if (status.isNotEmpty()) Text(status, fontSize = 14.sp, color = MagistralText)
        OutlinedButton(onClick = onBack) { Text("VOLVER") }
    }
}

@Composable
private fun MarkerLine(fraction: Float, widthPx: Float, color: Color, onMove: (Float) -> Unit) {
    // El área de arrastre es de 28 dp centrada en la línea; el desplazamiento
    // en píxeles se convierte a fracción con el ancho real de la foto.
    // rememberUpdatedState: el gesto no se reinicia a mitad del arrastre
    // cuando la fracción cambia (con pointerInput(fraction) se cortaba).
    val currentFraction by rememberUpdatedState(fraction)
    val currentWidthPx by rememberUpdatedState(widthPx)
    val currentOnMove by rememberUpdatedState(onMove)
    Box(
        modifier = Modifier
            .offset { IntOffset((fraction * widthPx).roundToInt() - 14.dp.roundToPx(), 0) }
            .size(width = 28.dp, height = 320.dp)
            .pointerInput(Unit) {
                detectHorizontalDragGestures { change, dragAmount ->
                    change.consume()
                    val w = currentWidthPx
                    if (w > 1f) {
                        currentOnMove((currentFraction + dragAmount / w).coerceIn(0f, 1f))
                    }
                }
            },
        contentAlignment = Alignment.Center
    ) {
        Box(
            modifier = Modifier
                .size(width = 3.dp, height = 320.dp)
                .background(color)
        )
        Box(
            modifier = Modifier
                .size(20.dp)
                .background(color, shape = CircleShape)
        )
    }
}
