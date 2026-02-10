package com.appcontrol.app_bomba

import android.annotation.SuppressLint
import android.graphics.Color
import android.os.Bundle
import android.view.View
import android.webkit.WebChromeClient
import android.webkit.WebResourceRequest
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView
import com.appcontrol.app_bomba.ui.theme.App_BombaTheme

class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        enableEdgeToEdge()

        // colores barras
        val colorHex = "#b3d9ff"
        window.statusBarColor = Color.parseColor(colorHex)
        window.navigationBarColor = Color.parseColor(colorHex)
        window.decorView.systemUiVisibility =
            View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR or View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR

        // -----------------------------
        // 🔵 LEER DATOS GUARDADOS
        // -----------------------------
        val prefs = getSharedPreferences("APP_STORE", MODE_PRIVATE)
        val ip = prefs.getString("local_ip", null)
        val token = prefs.getString("local_token", null)

        // -----------------------------
        // 🔵 DECIDIR PANTALLA INICIAL
        // -----------------------------
        val startPage =
            if (ip != null && token != null)
                "file:///android_asset/panel_local.html"
            else
                "file:///android_asset/home.html"

        setContent {
            App_BombaTheme {
                WebViewScreen(startPage)
            }
        }
    }
}

@SuppressLint("SetJavaScriptEnabled")
@Composable
fun WebViewScreen(startPage: String) {

    var lastUrl by remember { mutableStateOf(startPage) }

    AndroidView(
        modifier = Modifier.fillMaxSize(),
        factory = { context ->

            WebView(context).apply {

                settings.javaScriptEnabled = true
                settings.domStorageEnabled = true
                settings.allowFileAccess = true
                settings.allowContentAccess = true
                settings.mixedContentMode = WebSettings.MIXED_CONTENT_ALWAYS_ALLOW

                // permitir AndroidStore
                addJavascriptInterface(AndroidStore(context), "AndroidStore")

                // ⭐ Necesario para alert(), console.log
                webChromeClient = WebChromeClient()

                webViewClient = object : WebViewClient() {
                    override fun shouldOverrideUrlLoading(
                        view: WebView?,
                        request: WebResourceRequest?
                    ): Boolean {

                        val url = request?.url.toString()

                        if (url.startsWith("http://") || url.startsWith("https://")) {
                            return false
                        }

                        if (url.startsWith("file:///android_asset/")) {
                            lastUrl = url
                            view?.loadUrl(url)
                            return true
                        }

                        return false
                    }
                }

                loadUrl(lastUrl)
            }
        },
        update = { it.loadUrl(lastUrl) }
    )
}


