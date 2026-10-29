package com.ivanna.omega.spatial

/** Trial assignment and evidence are transient; old/failed trials cannot become valid results. */
class AbxTrialSession(private val drawDynamic: () -> Boolean = { kotlin.random.Random.nextBoolean() }) {
    var scenarioX: Int = 1
        private set
    var active: Boolean = false
        private set
    private val heard = mutableSetOf<String>()

    fun begin() {
        check(!active) { "Trial already active" }
        scenarioX = if (drawDynamic()) 2 else 1
        heard.clear()
        active = true
    }

    fun scenario(sample: String): Int {
        require(sample in setOf("A", "B", "X"))
        check(active)
        return when (sample) { "A" -> 1; "B" -> 2; else -> scenarioX }
    }

    fun confirm(sample: String, accepted: Boolean) {
        scenario(sample)
        if (!accepted) cancel() else heard += sample
    }

    fun canSubmit(selection: String?): Boolean =
        active && heard.containsAll(listOf("A", "B", "X")) && selection in listOf("A", "B")

    fun finish(selection: String): Boolean {
        check(canSubmit(selection))
        val correct = (selection == "B") == (scenarioX == 2)
        cancel()
        return correct
    }

    fun cancel() { active = false; heard.clear() }
}
