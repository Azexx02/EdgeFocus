package com.edgefocus.app.presentation.shared

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.Base64
import androidx.compose.runtime.Composable
import androidx.compose.runtime.State
import androidx.compose.runtime.produceState
import com.edgefocus.app.data.remote.CameraPreviewImage
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

internal object CameraPreviewDecodePolicy {
    const val MAX_WIDTH = 1_280
    const val MAX_HEIGHT = 720
    const val MAX_PIXELS = 921_600
    const val MAX_DECODED_BYTES = 512 * 1024
    const val MAX_BASE64_CHARS = 700 * 1024

    fun dimensionsAreSafe(width: Int, height: Int): Boolean {
        if (width <= 0 || height <= 0 || width > MAX_WIDTH || height > MAX_HEIGHT) return false
        return width.toLong() * height.toLong() <= MAX_PIXELS
    }

    fun base64IsSafe(value: String): Boolean =
        value.isNotBlank() && value.length <= MAX_BASE64_CHARS
}

@Composable
fun rememberCameraPreviewBitmap(preview: CameraPreviewImage?): State<Bitmap?> = produceState<Bitmap?>(
    initialValue = null,
    key1 = preview?.sequence,
    key2 = preview?.timestampMs,
    key3 = preview?.dataBase64
) {
    value = withContext(Dispatchers.Default) { decodeCameraPreview(preview) }
}

internal fun decodeCameraPreview(preview: CameraPreviewImage?): Bitmap? {
    if (preview?.available != true) return null
    val encoded = preview.dataBase64
        ?.substringAfter("base64,", preview.dataBase64.orEmpty())
        .orEmpty()
    if (!CameraPreviewDecodePolicy.base64IsSafe(encoded)) return null

    return runCatching {
        val bytes = Base64.decode(encoded, Base64.DEFAULT)
        if (bytes.isEmpty() || bytes.size > CameraPreviewDecodePolicy.MAX_DECODED_BYTES) return null

        when (preview.pixelFormat?.trim()?.uppercase()) {
            "GRAY8" -> decodeGray8(preview, bytes)
            else -> decodeEncodedBitmap(bytes)
        }
    }.getOrNull()
}

private fun decodeGray8(preview: CameraPreviewImage, bytes: ByteArray): Bitmap? {
    val width = preview.width
    val height = preview.height
    if (!CameraPreviewDecodePolicy.dimensionsAreSafe(width, height)) return null
    if (bytes.size != width * height) return null

    val pixels = IntArray(bytes.size) { index ->
        val gray = bytes[index].toInt() and 0xFF
        0xFF000000.toInt() or (gray shl 16) or (gray shl 8) or gray
    }
    return Bitmap.createBitmap(pixels, width, height, Bitmap.Config.ARGB_8888)
}

private fun decodeEncodedBitmap(bytes: ByteArray): Bitmap? {
    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds)
    if (!CameraPreviewDecodePolicy.dimensionsAreSafe(bounds.outWidth, bounds.outHeight)) return null

    return BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
}
