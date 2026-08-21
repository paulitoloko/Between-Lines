void connectWiFi() {
    constexpr unsigned long WIFI_ATTEMPT_TIMEOUT_MS = 8000UL;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connecting WiFi");
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 50);
    tft.print("WiFi...");

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);

    WiFi.disconnect(false, true);
    delay(250);

    unsigned int attempt = 0;
    const unsigned long connectionStartedAt = millis();
    while (WiFi.status() != WL_CONNECTED) {
        ++attempt;
        Serial.printf("WiFi association attempt %u (max 8 s)...\n", attempt);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        const unsigned long attemptStartedAt = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - attemptStartedAt < WIFI_ATTEMPT_TIMEOUT_MS) {
            delay(250);
            Serial.print(".");
        }

        if (WiFi.status() != WL_CONNECTED) {
            Serial.printf("\nWiFi retry after %lu ms, status=%d\n",
                          millis() - attemptStartedAt, WiFi.status());
            WiFi.disconnect(false);
            delay(300);
        }
    }

    Serial.printf("\nWiFi connected in %lu ms. IP: %s RSSI=%d dBm\n",
                  millis() - connectionStartedAt, WiFi.localIP().toString().c_str(), WiFi.RSSI());
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi OK");
    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(20, 50);
    tft.print("WiFi OK");
    delay(500);
}
void recoverWiFiNetwork() {
    Serial.println("Recovering WiFi network...");
    WiFi.disconnect(false);
    delay(250);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}


