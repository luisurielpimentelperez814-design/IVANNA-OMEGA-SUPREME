package com.ivanna.omega.sensing

import org.junit.Assert.assertEquals
import org.junit.Test

class RootWifiCmdParseTest {
    private val sample = """
        Number of scan results: 3
            BSSID              Frequency      RSSI           Age(sec)     SSID                                 Flags
        aa:bb:cc:dd:ee:01      2437          -48           1.250        Casa Net                         [WPA2-PSK-CCMP][ESS]
        aa:bb:cc:dd:ee:02      5180          -61           2.000        Vecino                           [WPA2-PSK-CCMP][ESS]
        aa:bb:cc:dd:ee:03      2412          -80           400.000      Viejo                            [ESS]
        aa:bb:cc:dd:ee:04      2462          -70           3.000                                         [ESS]
    """.trimIndent()

    @Test fun parsesRowsAndDropsStaleOnes() {
        val obs = RootWifiSurveyBackend.parseCmdWifi(sample, 1_000L)
        assertEquals(listOf("Casa Net", "Vecino", "<red oculta>"), obs.map { it.ssid })
        assertEquals(-48, obs[0].rssiDbm)
        assertEquals(5180, obs[1].frequencyMhz)
    }

    @Test fun garbageYieldsNothing() {
        assertEquals(0, RootWifiSurveyBackend.parseCmdWifi("cmd: not found", 0L).size)
    }
}
