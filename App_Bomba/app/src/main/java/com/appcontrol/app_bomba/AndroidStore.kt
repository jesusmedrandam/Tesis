package com.appcontrol.app_bomba

import android.content.Context
import android.webkit.JavascriptInterface

class AndroidStore(private val context: Context) {

    private val prefs = context.getSharedPreferences("app_bomba_prefs", Context.MODE_PRIVATE)

    @JavascriptInterface
    fun saveValue(key: String, value: String) {
        prefs.edit().putString(key, value).apply()
    }

    @JavascriptInterface
    fun getValue(key: String): String? {
        return prefs.getString(key, null)
    }
}
