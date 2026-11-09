package com.ivanna.omega.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Star
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.music.RockConcert
import com.ivanna.omega.music.RockConcertsCollection
import com.ivanna.omega.ui.theme.AmberSignal
import com.ivanna.omega.ui.theme.AuroraCyan
import com.ivanna.omega.ui.theme.AuroraCyanDim
import com.ivanna.omega.ui.theme.GlassOutline
import com.ivanna.omega.ui.theme.ObsidianRaised
import com.ivanna.omega.ui.theme.ObsidianVoid
import com.ivanna.omega.ui.theme.TextMuted
import com.ivanna.omega.ui.theme.TextPrimary
import com.ivanna.omega.ui.theme.TextSecondary

/**
 * RockConcertsScreen — la colección curada de los 30 mejores conciertos de
 * rock y metal de la historia (v2.5.372).
 *
 * - Tarjeta del destacado del día arriba ([RockConcertsCollection.featuredOfTheDay]).
 * - Chips de filtro por género ([RockConcertsCollection.genresPresent]).
 * - Lista completa: artista, título, recinto + ciudad + año, género, por qué
 *   es legendario y los servicios donde escucharlo.
 * - Al tocar un servicio se abre su buscador con el [RockConcert.searchTitle]
 *   exacto (Intent ACTION_VIEW, patrón ya usado en AuditoryExperienceScreen);
 *   si no hay navegador/app que lo abra, el título se copia al portapapeles.
 *
 * Ruta: [IvannaRoute.ROCK_CONCERTS] ("rock_concerts"), con acceso desde el
 * hub SISTEMA (tarjeta "CONCIERTOS LEGENDARIOS", junto a MUSIC INTELLIGENCE).
 */
@Composable
fun RockConcertsScreen(onBack: () -> Unit) {
    val context = LocalContext.current
    var selectedGenre by remember { mutableStateOf<String?>(null) }
    var statusMsg by remember { mutableStateOf<String?>(null) }
    val featured = remember { RockConcertsCollection.featuredOfTheDay() }
    val genres = remember { RockConcertsCollection.genresPresent() }
    val shown = remember(selectedGenre) {
        if (selectedGenre == null) RockConcertsCollection.all
        else RockConcertsCollection.byGenre(selectedGenre!!)
    }

    Column(
        modifier = Modifier.fillMaxSize().background(ObsidianVoid)
            .windowInsetsPadding(WindowInsets.systemBars)
    ) {
        // Cabecera fija: atrás + título.
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            IconButton(onClick = onBack) {
                Icon(Icons.Filled.ArrowBack, contentDescription = "Atrás", tint = AuroraCyan)
            }
            Column(Modifier.weight(1f)) {
                Text(
                    "CONCIERTOS LEGENDARIOS",
                    color = TextPrimary, fontSize = 18.sp,
                    fontWeight = FontWeight.ExtraBold, letterSpacing = 1.5.sp
                )
                Text(
                    "Los 30 mejores conciertos de rock · escúchalos con IVANNA",
                    color = TextSecondary, fontSize = 11.sp
                )
            }
        }
        statusMsg?.let {
            Text(
                it, color = AuroraCyan, fontSize = 11.sp, textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp)
            )
        }

        LazyColumn(
            modifier = Modifier.weight(1f),
            contentPadding = PaddingValues(horizontal = 16.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // Destacado del día.
            item {
                ConcertCard(
                    concert = featured,
                    featured = true,
                    onServiceClick = { service, title ->
                        statusMsg = openServiceSearch(context, service, title)
                    }
                )
            }
            // Filtro por género.
            item {
                Row(
                    modifier = Modifier.fillMaxWidth()
                        .horizontalScroll(rememberScrollState()),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    GenreFilterChip(
                        label = "Todos",
                        selected = selectedGenre == null,
                        onClick = { selectedGenre = null }
                    )
                    genres.forEach { genre ->
                        GenreFilterChip(
                            label = genre,
                            selected = selectedGenre == genre,
                            onClick = { selectedGenre = genre }
                        )
                    }
                }
            }
            // Lista completa (filtrada).
            items(shown, key = { it.id }) { concert ->
                ConcertCard(
                    concert = concert,
                    featured = false,
                    onServiceClick = { service, title ->
                        statusMsg = openServiceSearch(context, service, title)
                    }
                )
            }
            // Pie con el conteo.
            item {
                Text(
                    "${shown.size} de ${RockConcertsCollection.all.size} conciertos · " +
                        "${genres.size} géneros · " +
                        "${RockConcertsCollection.all.minOf { it.year }}–" +
                        RockConcertsCollection.all.maxOf { it.year },
                    color = TextMuted, fontSize = 11.sp, textAlign = TextAlign.Center,
                    modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp)
                )
            }
        }
    }
}

@Composable
private fun ConcertCard(
    concert: RockConcert,
    featured: Boolean,
    onServiceClick: (service: String, searchTitle: String) -> Unit
) {
    val accent = if (featured) AmberSignal else AuroraCyan
    Surface(
        modifier = Modifier.fillMaxWidth(),
        color = ObsidianRaised,
        shape = RoundedCornerShape(16.dp),
        border = BorderStroke(
            1.dp,
            if (featured) accent.copy(alpha = 0.7f) else GlassOutline.copy(alpha = 0.6f)
        )
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            if (featured) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Icon(
                        Icons.Filled.Star, contentDescription = null,
                        tint = AmberSignal, modifier = Modifier.size(14.dp)
                    )
                    Text(
                        "DESTACADO DEL DÍA", color = AmberSignal, fontSize = 11.sp,
                        fontWeight = FontWeight.Bold, letterSpacing = 1.5.sp
                    )
                }
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    concert.artist, color = TextPrimary, fontSize = 17.sp,
                    fontWeight = FontWeight.ExtraBold, modifier = Modifier.weight(1f)
                )
                Surface(
                    color = AuroraCyanDim.copy(alpha = 0.16f),
                    shape = RoundedCornerShape(10.dp),
                    border = BorderStroke(1.dp, AuroraCyanDim.copy(alpha = 0.5f))
                ) {
                    Text(
                        concert.genre, color = AuroraCyan, fontSize = 10.sp,
                        fontWeight = FontWeight.Bold,
                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 3.dp)
                    )
                }
            }
            Text(
                concert.title, color = TextSecondary, fontSize = 14.sp,
                fontWeight = FontWeight.SemiBold
            )
            Text(
                "${concert.venue} · ${concert.city} · ${concert.year}",
                color = TextMuted, fontSize = 12.sp
            )
            Text(
                concert.whyLegendary, color = TextSecondary, fontSize = 12.sp,
                lineHeight = 18.sp
            )
            Text(
                "ESCUCHAR EN", color = TextMuted, fontSize = 10.sp,
                fontWeight = FontWeight.Bold, letterSpacing = 1.5.sp
            )
            Row(
                modifier = Modifier.fillMaxWidth()
                    .horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                concert.listenOn.forEach { service ->
                    Surface(
                        onClick = { onServiceClick(service, concert.searchTitle) },
                        color = ObsidianVoid,
                        shape = RoundedCornerShape(20.dp),
                        border = BorderStroke(1.dp, AuroraCyan.copy(alpha = 0.45f))
                    ) {
                        Row(
                            modifier = Modifier.padding(horizontal = 12.dp, vertical = 7.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(5.dp)
                        ) {
                            Icon(
                                Icons.Filled.Search, contentDescription = null,
                                tint = AuroraCyan, modifier = Modifier.size(13.dp)
                            )
                            Text(
                                service, color = TextPrimary, fontSize = 12.sp,
                                fontWeight = FontWeight.SemiBold
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun GenreFilterChip(label: String, selected: Boolean, onClick: () -> Unit) {
    Surface(
        onClick = onClick,
        color = if (selected) AuroraCyan.copy(alpha = 0.22f) else ObsidianRaised,
        shape = RoundedCornerShape(20.dp),
        border = BorderStroke(
            1.dp,
            if (selected) AuroraCyan else GlassOutline.copy(alpha = 0.6f)
        )
    ) {
        Text(
            label,
            color = if (selected) AuroraCyan else TextSecondary,
            fontSize = 12.sp,
            fontWeight = if (selected) FontWeight.Bold else FontWeight.Normal,
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 8.dp)
        )
    }
}

/**
 * Abre el buscador del servicio con el título exacto del concierto.
 * Devuelve el mensaje de estado para mostrar en la UI. Si no hay app o
 * navegador que abra la búsqueda, copia el título al portapapeles.
 * Solo se construyen URLs de búsqueda de servicios reales (sin deep links
 * a contenido: los enlaces cambian y se rompen; el título no).
 */
private fun openServiceSearch(context: Context, service: String, searchTitle: String): String {
    val query = Uri.encode(searchTitle)
    val url = when (service) {
        "YouTube" -> "https://www.youtube.com/results?search_query=$query"
        "Spotify" -> "https://open.spotify.com/search/$query"
        "TIDAL" -> "https://listen.tidal.com/search?q=$query"
        "Amazon Music" -> "https://music.amazon.com/search?searchPhrase=$query"
        else -> null
    }
    if (url != null) {
        val opened = runCatching {
            context.startActivity(
                Intent(Intent.ACTION_VIEW, Uri.parse(url))
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            )
            true
        }.getOrDefault(false)
        if (opened) return "Abriendo $service: $searchTitle"
    }
    val copied = runCatching {
        val clipboard =
            context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        clipboard.setPrimaryClip(
            ClipData.newPlainText("IVANNA · concierto", searchTitle)
        )
        true
    }.getOrDefault(false)
    return if (copied) "Título copiado: $searchTitle"
    else "No se pudo abrir $service"
}
