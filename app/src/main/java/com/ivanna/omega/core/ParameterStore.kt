package com.ivannafusion.persistence

import android.content.Context
import androidx.datastore.preferences.core.*
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map

val Context.dataStore by preferencesDataStore(name = "ivanna_settings")

class ParameterStore(private val context: Context) {

    suspend fun putInt(key: String, value: Int) {
        context.dataStore.edit { prefs -> prefs[intPreferencesKey(key)] = value }
    }
    suspend fun getInt(key: String, default: Int): Int {
        return context.dataStore.data.map { prefs -> prefs[intPreferencesKey(key)] ?: default }.first()
    }
    suspend fun putFloat(key: String, value: Float) {
        context.dataStore.edit { prefs -> prefs[floatPreferencesKey(key)] = value }
    }
    suspend fun getFloat(key: String, default: Float): Float {
        return context.dataStore.data.map { prefs -> prefs[floatPreferencesKey(key)] ?: default }.first()
    }
    suspend fun putBoolean(key: String, value: Boolean) {
        context.dataStore.edit { prefs -> prefs[booleanPreferencesKey(key)] = value }
    }
    suspend fun getBoolean(key: String, default: Boolean): Boolean {
        return context.dataStore.data.map { prefs -> prefs[booleanPreferencesKey(key)] ?: default }.first()
    }
}
