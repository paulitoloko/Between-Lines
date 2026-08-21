void updateSpotifyDisplay(String title, String artist){

    showSongInfo(title, artist);

}
void showSongInfo(String title, String artist) {

    
  tft.fillRect(0, 128, 160, 32, ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);

    tft.setTextSize(1);

    tft.setCursor(2,130);
    tft.println(title);

    tft.setCursor(2,145);
    tft.println(artist);

}
void getCurrentSongBlocking() {//for if error occurs.

    if (accessToken == "") {
        static unsigned long lastTokenRetry = 0;
        if (millis() - lastTokenRetry >= 15000UL) {
            lastTokenRetry = millis();
            Serial.println("No token; retrying Spotify authentication");
            accessToken = getAccessToken();
        }
        return;
    }


    if (WiFi.status() != WL_CONNECTED) {
        // Do not reset the radio from the 4-second poll. Auto-reconnect stays
        // enabled, while the lyric clock continues from its last valid value.
        Serial.println("WiFi unavailable; keeping local lyric clock");
        return;
    }
    WiFiClientSecure spotifyClient;
    spotifyClient.setInsecure();
    spotifyClient.setTimeout(2000);

    HTTPClient http;
    http.setConnectTimeout(2000);
    http.setTimeout(2000);
    http.setReuse(false);

    if (!http.begin(spotifyClient, "https://api.spotify.com/v1/me/player")) {
        Serial.println("Spotify poll could not start");
        spotifyClient.stop();
        return;
    }

    http.addHeader(
        "Authorization",
        "Bearer " + accessToken
    );


    int code = http.GET();


    if (code == 200) {

        String payload = http.getString();

        JsonDocument doc;

        DeserializationError err = deserializeJson(doc, payload);

        if (err) {
            Serial.print("SPOTIFY JSON ERROR: ");
            Serial.println(err.c_str());
            http.end();
            return;
        }

        progressMs = doc["progress_ms"].as<unsigned long>();

        durationMs = doc["item"]["duration_ms"].as<unsigned long>();

        isPlaying = doc["is_playing"].as<bool>();

        lastLocalTick = millis();

        String title =
            doc["item"]["name"].as<String>();

        String artist =
            doc["item"]["artists"][0]["name"].as<String>();

        // Spotify normally returns images as 640, 300 and 64 px. Use the
        // 300 px version: it is the right source for a 128x128 TFT and does
        // not consume the memory required by the 640 px original.
        String cover =
            doc["item"]["album"]["images"][1]["url"].as<String>();
        if (cover == "") {
            cover = doc["item"]["album"]["images"][2]["url"].as<String>();
        }
        if (cover == "") {
            cover = doc["item"]["album"]["images"][0]["url"].as<String>();
        }

        String album =
            doc["item"]["album"]["name"].as<String>();

        String songID =
            doc["item"]["id"].as<String>();

        if (songID != lastTrackId) {

            Serial.println();
            Serial.println("==============");
            Serial.println("NEW TRACK");
            Serial.println(title);
            Serial.println("==============");

            lastTrackId = songID;

            // Reset lyric state when the track changes
            lyricCount = 0;
            currentLyric = -1;
            totalPages = 0;
            currentPage = 0;

            // Make the new track visible immediately. The cover download can
            // take time, but it no longer delays title/artist feedback.
            updateSpotifyDisplay(title, artist);
            showMessage("Loading cover", "");
            delay(1);

            drawCover(cover);
            showMessage("Loading lyrics", "");
            getLyrics(title, artist, album, durationMs);
        }
        else {

            Serial.println("SAME TRACK");

        }

        // Pause/resume detection, independent of whether
        // the track changed. It is applied as soon as polling data arrives,
        // with no additional software delay.
        if (!isPlaying && wasPlaying) {

            Serial.println("Stopped");
            showMessage("Stopped", artist);

        }
        else if (isPlaying && !wasPlaying) {

            Serial.println("RESUMED");
            // Immediately restore the last displayed lyric/page,
            // without waiting for the normal page-change timing.
            showPage();

        }

        wasPlaying = isPlaying;
        spotifyNetworkFailures = 0;

    }
    else if (code == 204) {

        // There is no active playback (this is not a real error)
        Serial.println("NO ACTIVE PLAYBACK");
        isPlaying = false;

        if (wasPlaying) {
            showMessage("No playback", "");
        }

        wasPlaying = false;
        spotifyNetworkFailures = 0;

    }
    else {
        // Keep rendering and the local lyric clock alive on a transient poll
        // failure. WiFi auto-reconnect handles genuine link loss.
        Serial.printf("Spotify sync error: %d\n", code);
        ++spotifyNetworkFailures;
    }

    http.end();
    spotifyClient.stop();

}

void getCurrentSong() {
    // Apply the player result directly. The previous background hand-off
    // could leave seek and track changes unapplied on this board.
    getCurrentSongBlocking();
}

void spotifyPollTask(void* parameter) {
    for (;;) {
        if (!spotifyPollRequested) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        spotifyPollRequested = false;

        SpotifyUpdate result = {};
        result.status = -1;

        if (accessToken == "") {
            accessToken = getAccessToken();
        }

        if (accessToken == "") {
            result.status = -1;
        } else if (WiFi.status() != WL_CONNECTED) {
            // WiFi auto-reconnect remains enabled. Do not reset the radio from
            // this background poll: a weak transient link must not disturb
            // lyric rendering or start a reconnection loop.
            result.status = -2;
        } else {
            WiFiClientSecure client;
            client.setInsecure();
            client.setTimeout(5000);
            HTTPClient http;
            http.setConnectTimeout(5000);
            http.setTimeout(5000);
            http.setReuse(false);

            if (http.begin(client, "https://api.spotify.com/v1/me/player")) {
                http.addHeader("Authorization", "Bearer " + accessToken);
                result.status = http.GET();

                if (result.status == HTTP_CODE_OK) {
                    String payload = http.getString();
                    JsonDocument doc;
                    const DeserializationError err = deserializeJson(doc, payload);
                    if (!err && !doc["item"].isNull()) {
                        result.hasTrack = true;
                        result.playing = doc["is_playing"].as<bool>();
                        result.progress = doc["progress_ms"].as<unsigned long>();
                        result.duration = doc["item"]["duration_ms"].as<unsigned long>();
                        strlcpy(result.id, doc["item"]["id"] | "", sizeof(result.id));
                        strlcpy(result.title, doc["item"]["name"] | "", sizeof(result.title));
                        strlcpy(result.artist, doc["item"]["artists"][0]["name"] | "", sizeof(result.artist));
                        strlcpy(result.album, doc["item"]["album"]["name"] | "", sizeof(result.album));

                        const char* cover = doc["item"]["album"]["images"][1]["url"] | "";
                        if (cover[0] == '\0') cover = doc["item"]["album"]["images"][2]["url"] | "";
                        if (cover[0] == '\0') cover = doc["item"]["album"]["images"][0]["url"] | "";
                        strlcpy(result.cover, cover, sizeof(result.cover));
                    } else {
                        result.status = -3;
                    }
                }
                http.end();
            }
            client.stop();
        }

        taskENTER_CRITICAL(&spotifyUpdateMux);
        pendingSpotifyUpdate = result;
        pendingSpotifyUpdate.ready = true;
        taskEXIT_CRITICAL(&spotifyUpdateMux);
    }
}

void startSpotifyPollTask() {
    if (spotifyTaskHandle != nullptr) return;
    xTaskCreatePinnedToCore(
        spotifyPollTask,
        "SpotifyPoll",
        8192,
        nullptr,
        1,
        &spotifyTaskHandle,
        0
    );
}

void applyPendingSpotifyUpdate() {
    SpotifyUpdate update = {};
    bool hasUpdate = false;

    taskENTER_CRITICAL(&spotifyUpdateMux);
    if (pendingSpotifyUpdate.ready) {
        update = pendingSpotifyUpdate;
        pendingSpotifyUpdate.ready = false;
        hasUpdate = true;
    }
    taskEXIT_CRITICAL(&spotifyUpdateMux);

    if (!hasUpdate) return;

    if (update.status == HTTP_CODE_NO_CONTENT) {
        isPlaying = false;
        if (wasPlaying) showMessage("No playback", "");
        wasPlaying = false;
        return;
    }

    if (update.status != HTTP_CODE_OK || !update.hasTrack) {
        // Do not flood the serial monitor during a weak WiFi period. The
        // lyric clock keeps using its last valid Spotify progress value.
        static int lastReportedStatus = 0;
        static unsigned long lastReportAt = 0;
        if (update.status != lastReportedStatus || millis() - lastReportAt >= 30000UL) {
            Serial.printf("Spotify background poll error: %d\n", update.status);
            lastReportedStatus = update.status;
            lastReportAt = millis();
        }
        return;
    }

    // Updating these values is fast and immediately corrects seek forward/back.
    progressMs = update.progress;
    durationMs = update.duration > 0 ? update.duration : 1;
    isPlaying = update.playing;
    lastLocalTick = millis();

    const String title(update.title);
    const String artist(update.artist);
    const String album(update.album);

    if (String(update.id) != lastTrackId) {
        Serial.printf("New track: %s\n", update.title);
        lastTrackId = String(update.id);
        lyricCount = 0;
        currentLyric = -1;
        totalPages = 0;
        currentPage = 0;

        updateSpotifyDisplay(title, artist);
        showMessage("Loading cover", "");
        drawCover(String(update.cover));
        showMessage("Loading lyrics", "");
        getLyrics(title, artist, album, durationMs);
    }

    if (!isPlaying && wasPlaying) {
        showMessage("Paused", artist);
    } else if (isPlaying && !wasPlaying && lyricCount > 0) {
        showPage();
    }
    wasPlaying = isPlaying;
}

String getAccessToken() {
    String auth = String(CLIENT_ID) + ":" + String(CLIENT_SECRET);
    unsigned char output[256] = {0};
    size_t olen = 0;
    mbedtls_base64_encode(output, sizeof(output), &olen,
                          (const unsigned char*)auth.c_str(), auth.length());
    const String encoded = String((char*)output);
    const String body = "grant_type=refresh_token&refresh_token=" + String(REFRESH_TOKEN);
    for (int attempt = 1; attempt <= 3; ++attempt) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi unavailable while requesting token");
            recoverWiFiNetwork();
            delay(1500);
            continue;
        }

        Serial.printf("Requesting Spotify token (%d/3)\n", attempt);
        WiFiClientSecure tokenClient;
        tokenClient.setInsecure();
        tokenClient.setTimeout(12000);

        HTTPClient http;
        http.setConnectTimeout(12000);
        http.setTimeout(12000);
        http.setReuse(false);

        if (!http.begin(tokenClient, "https://accounts.spotify.com/api/token")) {
            Serial.println("Token request could not start");
            tokenClient.stop();
            delay(1500);
            continue;
        }
        http.addHeader("Authorization", "Basic " + encoded);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        const int response = http.POST(body);
        Serial.printf("Token HTTP response: %d\n", response);
        if (response == HTTP_CODE_OK) {
            const String payload = http.getString();
            JsonDocument doc;
            const DeserializationError err = deserializeJson(doc, payload);
            const String token = err ? "" : doc["access_token"].as<String>();
            http.end();
            tokenClient.stop();
            if (token != "") {
                Serial.println("Spotify token received");
                return token;
            }
            Serial.println("Token response had no access token");
        } else {
            http.end();
            tokenClient.stop();
        }

        if (attempt < 3) delay(1500);
    }

    Serial.println("Spotify token unavailable after retries");
    return "";
}