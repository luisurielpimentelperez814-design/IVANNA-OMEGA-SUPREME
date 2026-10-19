package com.ivanna.omega.ai

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ModelManagerValidationTest {
    @Test fun acceptsFlatBufferTfliteIdentifier() {
        val model = ByteArray(12)
        "TFL3".toByteArray(Charsets.US_ASCII).copyInto(model, destinationOffset = 4)
        assertTrue(isTfliteModelBytes(model))
    }

    @Test fun rejectsTextPlaceholderAndTruncatedBytes() {
        assertFalse(isTfliteModelBytes("TFL3 - fake model".toByteArray(Charsets.US_ASCII)))
        assertFalse(isTfliteModelBytes(byteArrayOf(0, 1, 2, 3, 4, 5, 6)))
    }
}
