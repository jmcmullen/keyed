package services.session.keyed.engine

/**
 * Result from processing one audio frame
 */
data class FrameResult(
    val beatActivation: Float,
    val downbeatActivation: Float
)
