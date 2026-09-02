package com.ivanna.omega.assistant.core

object ConversationMemory {
    private val shortTerm = mutableListOf<String>()
    
    fun addInteraction(user: String, ivanna: String) {
        shortTerm.add("User: $user\nIvanna: $ivanna")
        if (shortTerm.size > 5) shortTerm.removeAt(0)
    }
    
    fun getContext(): String = shortTerm.joinToString("\n---\n")
    fun clear() = shortTerm.clear()
}
