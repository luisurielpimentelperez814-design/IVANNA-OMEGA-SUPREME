package com.ivanna.omega.assistant

class IvannaContextMemory {

    private val memory = mutableListOf<String>()


    fun add(value:String){
        memory.add(value)
    }


    fun getAll(): List<String>{
        return memory.toList()
    }


    fun clear(){
        memory.clear()
    }
}
