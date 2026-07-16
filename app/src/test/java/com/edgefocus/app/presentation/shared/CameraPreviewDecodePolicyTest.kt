package com.edgefocus.app.presentation.shared

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class CameraPreviewDecodePolicyTest {
    @Test
    fun dimensionsRejectInvalidAndOversizedFrames() {
        assertFalse(CameraPreviewDecodePolicy.dimensionsAreSafe(0, 30))
        assertFalse(CameraPreviewDecodePolicy.dimensionsAreSafe(1_281, 30))
        assertFalse(CameraPreviewDecodePolicy.dimensionsAreSafe(1_280, 721))
    }

    @Test
    fun dimensionsAcceptCurrentHostPreview() {
        assertTrue(CameraPreviewDecodePolicy.dimensionsAreSafe(40, 30))
    }

    @Test
    fun base64LengthIsBoundedBeforeDecode() {
        assertTrue(CameraPreviewDecodePolicy.base64IsSafe("AQID"))
        assertFalse(CameraPreviewDecodePolicy.base64IsSafe("A".repeat(CameraPreviewDecodePolicy.MAX_BASE64_CHARS + 1)))
    }
}
