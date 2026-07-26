package com.ivanna.omega.audio.effects

import kotlin.math.*

interface AudioEffect {
    fun process(input: FloatArray): FloatArray
    fun reset()
}

data class Complex(var real: Float, var imag: Float)

fun fft(x: Array<Complex>, inverse: Boolean = false) {
    val n = x.size
    if (n <= 1) return
    var j = 0
    for (i in 1 until n) {
        var bit = n shr 1
        while (j and bit != 0) { j = j xor bit; bit = bit shr 1 }
        j = j xor bit
        if (i < j) { val t = x[i]; x[i] = x[j]; x[j] = t }
    }
    var len = 2
    while (len <= n) {
        val ang = 2.0 * PI / len * if (inverse) 1 else -1
        val wRe = cos(ang).toFloat(); val wIm = sin(ang).toFloat()
        var i = 0
        while (i < n) {
            var cRe = 1f; var cIm = 0f
            val half = len / 2
            for (k in 0 until half) {
                val uRe = x[i+k].real; val uIm = x[i+k].imag
                val vRe = x[i+k+half].real*cRe - x[i+k+half].imag*cIm
                val vIm = x[i+k+half].real*cIm + x[i+k+half].imag*cRe
                x[i+k].real = uRe+vRe; x[i+k].imag = uIm+vIm
                x[i+k+half].real = uRe-vRe; x[i+k+half].imag = uIm-vIm
                val nr = cRe*wRe - cIm*wIm; cIm = cRe*wIm + cIm*wRe; cRe = nr
            }
            i += len
        }
        len = len shl 1
    }
    if (inverse) { val fn = n.toFloat(); for (c in x) { c.real /= fn; c.imag /= fn } }
}

fun hannWindow(size: Int): FloatArray =
    FloatArray(size) { i -> (0.5*(1.0 - cos(2.0*PI*i/size))).toFloat() }

// ─── CinematicReverb ─────────────────────────────────────────────────────────
class CinematicReverb(
    private var roomSize: Float = 0.8f,
    private var damping:  Float = 0.5f,
    private var wetLevel: Float = 0.3f,
    private var dryLevel: Float = 0.7f,
    private var width:    Float = 0.8f
) : AudioEffect {
    private var curRoom = roomSize; private var curDamp = damping
    private val combL = Array(8) { CombFilter(COMB[it],     roomSize*0.9f+0.1f, damping) }
    private val combR = Array(8) { CombFilter(COMB[it]+23,  roomSize*0.9f+0.1f, damping) }
    private val apL   = Array(4) { AllpassFilter(AP[it]) }
    private val apR   = Array(4) { AllpassFilter(AP[it]-13) }
    companion object { val COMB = intArrayOf(1116,1188,1277,1356,1422,1491,1557,1617)
                       val AP   = intArrayOf(556,441,341,225) }
    override fun process(input: FloatArray): FloatArray {
        val out = FloatArray(input.size)
        for (i in input.indices) {
            curDamp += (damping  - curDamp)*0.01f
            curRoom += (roomSize - curRoom)*0.01f
            val fb = curRoom*0.9f+0.1f
            var wL = 0f; var wR = 0f
            for (j in 0 until 8) { combL[j].updateFeedback(fb,curDamp); combR[j].updateFeedback(fb,curDamp)
                wL += combL[j].process(input[i]); wR += combR[j].process(input[i]) }
            wL /= 8f; wR /= 8f
            for (j in 0 until 4) { wL = apL[j].process(wL); wR = apR[j].process(wR) }
            val mid = (wL+wR)*0.5f; val side = (wL-wR)*0.5f*width
            out[i] = input[i]*dryLevel + (mid+side)*wetLevel
        }
        return out
    }
    override fun reset() { combL.forEach{it.reset()}; combR.forEach{it.reset()}
                           apL.forEach{it.reset()};   apR.forEach{it.reset()} }
    fun updateParameters(r: Float, d: Float) { roomSize=r; damping=d }
}
private class CombFilter(sz: Int, private var fb: Float, private var dmp: Float) {
    private val buf = FloatArray(sz); private var idx=0; private var fs=0f
    fun process(x: Float): Float { val o=buf[idx]; fs=o*(1f-dmp)+fs*dmp
        buf[idx]=x+fs*fb; idx=(idx+1)%buf.size; return o }
    fun reset() { buf.fill(0f); fs=0f; idx=0 }
    fun updateFeedback(f: Float, d: Float) { fb=f; dmp=d }
}
private class AllpassFilter(sz: Int, private val fb: Float = 0.5f) {
    private val buf = FloatArray(sz.coerceAtLeast(1)); private var idx=0
    fun process(x: Float): Float { val b=buf[idx]; val o=-x+b
        buf[idx]=x+b*fb; idx=(idx+1)%buf.size; return o }
    fun reset() { buf.fill(0f); idx=0 }
}

// ─── ModulatingDelay ─────────────────────────────────────────────────────────
class ModulatingDelay(
    private var baseTimeMs: Float = 100f,
    private var depth:      Float = 0.5f,
    private var rateHz:     Float = 0.5f,
    private var feedback:   Float = 0.4f,
    private var wetLevel:   Float = 0.4f,
    private var dryLevel:   Float = 0.6f,
    private var tone:       Float = 0.5f
) : AudioEffect {
    private val SR = 44100f
    private val maxD = 44100
    private val buf = FloatArray(maxD); private var wIdx=0
    private var phase=0.0; private var lpState=0f
    override fun process(input: FloatArray): FloatArray {
        val out = FloatArray(input.size)
        for (i in input.indices) {
            val mod = sin(phase)*depth
            val dSamples = ((baseTimeMs*(1f+mod*0.5f))*SR/1000f).toInt().coerceIn(1,maxD-3)
            val rIdx = (wIdx-dSamples+maxD)%maxD
            val y0=buf[(rIdx-1+maxD)%maxD]; val y1=buf[rIdx]
            val y2=buf[(rIdx+1)%maxD];       val y3=buf[(rIdx+2)%maxD]
            val fr = (dSamples - floor(dSamples.toDouble())).toFloat()
            val a0=y3-y2-y0+y1; val a1=y0-y1-a0; val a2=y2-y0; val a3=y1
            val delayed = a0*fr*fr*fr + a1*fr*fr + a2*fr + a3
            val fbSig = delayed*feedback; lpState += tone*(fbSig-lpState)
            buf[wIdx]=input[i]+lpState; wIdx=(wIdx+1)%maxD
            out[i]=input[i]*dryLevel+delayed*wetLevel
            phase += 2.0*PI*rateHz/SR; if (phase>2.0*PI) phase-=2.0*PI
        }
        return out
    }
    override fun reset() { buf.fill(0f); wIdx=0; phase=0.0; lpState=0f }
}

// ─── FormantShifter ──────────────────────────────────────────────────────────
class FormantShifter(
    private var pitchRatio:   Float = 0.7f,
    private var formantShift: Float = 0.8f
) : AudioEffect {
    private val fftSize = 2048; private val hopSize = 512; private val SR = 44100f
    private val window      = hannWindow(fftSize)
    private val inBuf       = FloatArray(fftSize*2); private var inIdx=0
    private val outBuf      = FloatArray(fftSize*2); private var outIdx=0
    private val fftBuf      = Array(fftSize) { Complex(0f,0f) }
    private val lastPhase   = FloatArray(fftSize/2+1)
    private val lastOutPhase= FloatArray(fftSize/2+1)
    private var frames = 0
    override fun process(input: FloatArray): FloatArray {
        val out = FloatArray(input.size)
        for (i in input.indices) {
            inBuf[inIdx]=input[i]; inIdx=(inIdx+1)%inBuf.size
            if (inIdx % hopSize == 0) processFrame()
            out[i]=outBuf[outIdx]; outBuf[outIdx]=0f; outIdx=(outIdx+1)%outBuf.size
        }
        return out
    }
    private fun processFrame() {
        val frame = FloatArray(fftSize)
        val rs = (inIdx-fftSize+inBuf.size)%inBuf.size
        for (i in 0 until fftSize) frame[i]=inBuf[(rs+i)%inBuf.size]*window[i]
        for (i in 0 until fftSize) { fftBuf[i].real=frame[i]; fftBuf[i].imag=0f }
        fft(fftBuf, false)
        val mag = FloatArray(fftSize/2+1); val ph = FloatArray(fftSize/2+1)
        for (k in 0..fftSize/2) {
            mag[k] = sqrt(fftBuf[k].real*fftBuf[k].real + fftBuf[k].imag*fftBuf[k].imag)
            ph[k]  = atan2(fftBuf[k].imag, fftBuf[k].real)
        }
        val synMag = FloatArray(fftSize/2+1); val synPh = FloatArray(fftSize/2+1)
        for (k in 0..fftSize/2) {
            var dp = (ph[k]-lastPhase[k]).toDouble(); lastPhase[k]=ph[k]
            val expected = 2.0*PI*k*hopSize/fftSize
            dp -= expected; dp -= 2.0*PI*floor(dp/(2.0*PI)+0.5)
            val trueFreq = expected+dp
            val sb = (k*formantShift).coerceIn(0f,(fftSize/2).toFloat())
            val ib = sb.toInt().coerceIn(0,fftSize/2-1); val fr=sb-ib
            synMag[k] = mag[ib]*(1f-fr)+mag[(ib+1).coerceAtMost(fftSize/2)]*fr
            synPh[k]  = (lastOutPhase[k]+trueFreq/pitchRatio).toFloat()
            lastOutPhase[k]=synPh[k]
        }
        for (k in 0..fftSize/2) {
            val r=(synMag[k]*cos(synPh[k].toDouble())).toFloat()
            val im=(synMag[k]*sin(synPh[k].toDouble())).toFloat()
            fftBuf[k].real=r; fftBuf[k].imag=im
            if (k>0 && k<fftSize/2) { fftBuf[fftSize-k].real=r; fftBuf[fftSize-k].imag=-im }
        }
        fft(fftBuf, true)
        for (i in 0 until fftSize) outBuf[(outIdx+i)%outBuf.size] += fftBuf[i].real*window[i]
        outIdx = (outIdx+(hopSize*pitchRatio).toInt().coerceAtLeast(1))%outBuf.size
        frames++
    }
    override fun reset() { inBuf.fill(0f); outBuf.fill(0f); inIdx=0; outIdx=0
        lastPhase.fill(0f); lastOutPhase.fill(0f); frames=0 }
}

// ─── SubHarmonicGenerator ────────────────────────────────────────────────────
class SubHarmonicGenerator(
    private var octaveDown: Int   = -1,
    private var mix:        Float = 0.5f,
    private var drive:      Float = 0.3f
) : AudioEffect {
    private val SR = 44100f
    private val tracker = AutoCorrelationPitchTracker(SR)
    private var subPhase = 0.0; private var env = 0f
    private val attCoeff = exp(-1f/(0.01f*SR)); private val relCoeff = exp(-1f/(0.1f*SR))
    override fun process(input: FloatArray): FloatArray {
        val out = FloatArray(input.size)
        val freqMult = 2.0.pow(octaveDown).toFloat()
        for (i in input.indices) {
            val s = input[i]; val amp = abs(s)
            env = if (amp>env) amp+attCoeff*(env-amp) else amp+relCoeff*(env-amp)
            val baseFreq = tracker.process(s)
            val subFreq = baseFreq*freqMult
            val sub = if (baseFreq>20f) sin(subPhase).toFloat()*env else 0f
            subPhase += 2.0*PI*subFreq/SR; if (subPhase>2.0*PI) subPhase-=2.0*PI
            out[i] = s + tanh((sub*drive*2.5f).toDouble()).toFloat()*mix
        }
        return out
    }
    override fun reset() { tracker.reset(); subPhase=0.0; env=0f }
}
private class AutoCorrelationPitchTracker(private val SR: Float) {
    private val N = 2048; private val buf = FloatArray(N); private var idx=0; private var lastF=110f
    fun process(x: Float): Float {
        buf[idx]=x; idx=(idx+1)%N
        if (idx%128!=0) return lastF
        var bestC=-1f; var bestLag=0
        for (lag in 20 until N/2) {
            var s=0f; var s1=0f; var s2=0f
            for (i in 0 until N-lag) {
                val a=buf[(idx-N+i+N)%N]; val b=buf[(idx-N+i+lag+N)%N]
                s+=a*b; s1+=a*a; s2+=b*b }
            val c=if(s1>0f&&s2>0f) s/sqrt(s1*s2) else 0f
            if(c>bestC){bestC=c;bestLag=lag}
        }
        if(bestC>0.3f&&bestLag>0) lastF=SR/bestLag
        return lastF
    }
    fun reset() { buf.fill(0f); idx=0; lastF=110f }
}