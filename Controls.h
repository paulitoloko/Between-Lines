void spotifyPause() { //middle button response
    if (accessToken == "") return;
    HTTPClient http;
    http.begin("https://api.spotify.com/v1/me/player/pause");
    http.addHeader("Authorization", "Bearer " + accessToken);
    http.addHeader("Content-Type", "application/json");
    int code = http.PUT("{}");
    Serial.print("PAUSE RESPONSE: ");
    Serial.println(code);
    http.end();
    
    isPlaying = false;
    wasPlaying = false;
    showMessage("Stopped", "");
}

void spotifyPlay() {//middle button response, if already paused, resume playback

    if (accessToken == "") return;

    HTTPClient http;
    http.begin("https://api.spotify.com/v1/me/player/play");
    http.addHeader("Authorization", "Bearer " + accessToken);
    http.addHeader("Content-Type", "application/json");

    int code = http.PUT("{}");

    Serial.print("PLAY RESPONSE: ");
    Serial.println(code);

    http.end();

    // Immediate feedback: restore the current lyric instantly
    isPlaying = true;
    wasPlaying = true;
    lastLocalTick = millis(); // prevents an unusual jump in the progress bar
    showPage();
}

void spotifyNext() {//right button response

    if (accessToken == "") return;

    HTTPClient http;
    http.begin("https://api.spotify.com/v1/me/player/next");
    http.addHeader("Authorization", "Bearer " + accessToken);
    http.addHeader("Content-Type", "application/json");

    int code = http.POST("{}");

    Serial.print("NEXT RESPONSE: ");
    Serial.println(code);

    http.end();

    showMessage("Next", "song...");

    // Small delay so Spotify can register the track change
    // before refreshing, instead of waiting for the normal poll.
    delay(100);
    getCurrentSong();
    lastSpotifyPoll = millis(); // restart the polling cycle from here
}

void spotifyPrevious() {//left button response

    if (accessToken == "") return;

    HTTPClient http;
    http.begin("https://api.spotify.com/v1/me/player/previous");
    http.addHeader("Authorization", "Bearer " + accessToken);
    http.addHeader("Content-Type", "application/json");

    int code = http.POST("{}");

    Serial.print("PREVIOUS RESPONSE: ");
    Serial.println(code);

    http.end();

    showMessage("Previous", "song...");

    delay(400);
    lastTrackId = ""; // force recognition as a "new" track even if
                       // Spotify sometimes reuses the same session ID
    getCurrentSong();
    lastSpotifyPoll = millis();
}

// Turn off displays, disconnect Wi-Fi, and enter deep sleep. The power button
// (BTN_POWER, pin 20) is configured as a wake source: when pressed again,
// the ESP32 fully restarts and executes setup() again
// (this chip has no true low-consumption standby that retains RAM,
// so deep sleep plus reboot is used).
void enterStandby() {//left side button response

    Serial.println("ENTERING STANDBY...");

    // Wait for the button to be released
    while (digitalRead(BTN_POWER) == LOW) {
        delay(10);
    }

    delay(200);

    // Turn off the displays
    tft.fillScreen(ST77XX_BLACK);
    lcd.clear();
    lcd.noBacklight();

    // Disconnect from the network
    WiFi.disconnect(true);
    delay(100);

    // Enable RTC pull resistors
    rtc_gpio_pullup_en((gpio_num_t)BTN_POWER);
    rtc_gpio_pulldown_dis((gpio_num_t)BTN_POWER);

    // The ESP32 wakes when the pin transitions to LOW
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_POWER, 0);

    delay(100);

    esp_deep_sleep_start();
}

void handleButtons() {

    unsigned long now = millis();

    int statePause = digitalRead(BTN_PAUSE);
    int stateNext = digitalRead(BTN_NEXT);
    int statePrev = digitalRead(BTN_PREV);
    int statePower = digitalRead(BTN_POWER);

    if (statePause == LOW &&
        lastStatePause == HIGH &&
        now - lastPressPause > DEBOUNCE_MS) {

        lastPressPause = now;

        if (isPlaying) {
            spotifyPause();
        } else {
            spotifyPlay();
        }
    }

    if (stateNext == LOW &&
        lastStateNext == HIGH &&
        now - lastPressNext > DEBOUNCE_MS) {

        lastPressNext = now;
        spotifyNext();
    }

    if (statePrev == LOW &&
        lastStatePrev == HIGH &&
        now - lastPressPrev > DEBOUNCE_MS) {

        lastPressPrev = now;
        spotifyPrevious();
    }

    if (statePower == LOW &&
        lastStatePower == HIGH &&
        now - lastPressPower > DEBOUNCE_MS) {

        lastPressPower = now;
        enterStandby();
    }
    lastStatePause = statePause;
    lastStateNext = stateNext;
    lastStatePrev = statePrev;
    lastStatePower = statePower;
}
