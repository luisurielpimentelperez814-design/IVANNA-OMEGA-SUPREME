package com.ivanna.omega.ai

object SAFCore {

    private var memory = 0.0

    fun update(
        current: DoubleArray,
        target: DoubleArray,
        metric: DoubleArray
    ): DoubleArray {

        val delta =
            DoubleArray(current.size) {
                target[it]-current[it]
            }

        var energy = 0.0

        for(i in delta.indices){
            energy +=
                delta[i] *
                metric[i] *
                delta[i]
        }

        memory =
            0.9 * memory +
            0.1 * kotlin.math.sqrt(energy)

        val gain =
            energy /
            (
              energy +
              energy +
              0.05*memory +
              0.00001
            )


        return DoubleArray(current.size){

            current[it] +
            gain *
            delta[it] /
            metric[it]

        }
    }
}
