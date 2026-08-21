String urlEncode(String str) {

    String encoded = "";

    char c;

    char code0;
    char code1;

    for (int i = 0; i < str.length(); i++) {

        c = str.charAt(i);

        if (isalnum(c)) {

            encoded += c;

        }
        else if (c == ' ') {

            encoded += '+';

        }
        else {

            code1 = (c & 0xf) + '0';

            if ((c & 0xf) > 9)
                code1 = (c & 0xf) - 10 + 'A';


            c = (c >> 4) & 0xf;

            code0 = c + '0';

            if (c > 9)
                code0 = c - 10 + 'A';


            encoded += '%';
            encoded += code0;
            encoded += code1;

        }

    }

    return encoded;
}
void parseLyrics(String syncedLyrics){
    lyricCount = 0;
    int start = 0;

    while (start < syncedLyrics.length() && lyricCount < 200) {
        int end = syncedLyrics.indexOf('\n', start);
        if (end == -1) end = syncedLyrics.length();

        String line = syncedLyrics.substring(start, end);
        if (line.startsWith("[")) {
            const int close = line.indexOf("]");
            if (close > 0) {
                const String time = line.substring(1, close);
                const int colon = time.indexOf(':');
                const int dot = time.indexOf('.');
                if (colon > 0) {
                    const long minutes = time.substring(0, colon).toInt();
                    const long seconds = time.substring(colon + 1, dot >= 0 ? dot : time.length()).toInt();
                    const String fraction = dot >= 0 ? time.substring(dot + 1) : "";
                    unsigned long fractionMs = 0;
                    if (fraction.length() == 1) fractionMs = fraction.toInt() * 100UL;
                    else if (fraction.length() == 2) fractionMs = fraction.toInt() * 10UL;
                    else if (fraction.length() >= 3) fractionMs = fraction.substring(0, 3).toInt();

                    const unsigned long timestamp = minutes * 60000UL + seconds * 1000UL + fractionMs;
                    // A malformed or out-of-order LRC line must not freeze
                    // the whole lyric display after the previous valid line.
                    if (minutes >= 0 && seconds >= 0 && seconds < 60 &&
                        (lyricCount == 0 || timestamp >= lyricTimes[lyricCount - 1])) {
                        lyricTimes[lyricCount] = timestamp;
                        lyricLines[lyricCount] = line.substring(close + 1);
                        lyricLines[lyricCount].trim();
                        lyricCount++;
                    }
                }
            }
        }
        start = end + 1;
    }

    Serial.printf("Synced lyric lines: %d\n", lyricCount);
    if (lyricCount > 0) {
        Serial.printf("Lyric range: %lu to %lu ms\n", lyricTimes[0], lyricTimes[lyricCount - 1]);
    }
}
String cleanPlainLyrics(String plain) {

    String cleaned = "";
    int start = 0;

    while (start < plain.length()) {

        int end = plain.indexOf('\n', start);
        if (end == -1) end = plain.length();

        String line = plain.substring(start, end);
        line.trim();

        if (!line.startsWith("[")) {
            cleaned += line + "\n";
        }

        start = end + 1;
    }

    return cleaned;
}

void getLyrics(String title, String artist, String album, unsigned long trackDurationMs) {

    Serial.println("SEARCHING LYRICS...");

    lyricCount = 0;
    currentLyric = -1;
    {
        HTTPClient http;

        String getUrl = "https://lrclib.net/api/get?track_name="
                       + urlEncode(title)
                       + "&artist_name=" + urlEncode(artist)
                       + "&album_name=" + urlEncode(album)
                       + "&duration=" + String(trackDurationMs / 1000);

        http.begin(getUrl);
        http.addHeader("User-Agent", "ESP32-SpotifyDisplay/1.0 (contacto@ejemplo.com)");

        int code = http.GET();

        Serial.print("LRCLIB GET (exact match) RESPONSE: ");
        Serial.println(code);

        if (code == 200) {

            String payload = http.getString();

            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload);

            if (!err) {

                String synced = doc["syncedLyrics"].isNull() ? "" : doc["syncedLyrics"].as<String>();
                String plain  = doc["plainLyrics"].isNull()  ? "" : doc["plainLyrics"].as<String>();

                if (synced != "") {

                    lyrics = synced;
                    Serial.println("SYNCHRONIZED LYRICS (exact match)");
                    parseLyrics(synced);
                    http.end();
                    return;
                }
                else if (plain != "" &&
                         plain.indexOf("start_ms:") == -1 &&
                         plain.indexOf("end_ms:") == -1) {

                    lyrics = cleanPlainLyrics(plain);
                    Serial.println("PLAIN LYRICS (exact match)");
                    splitLyricIntoPages(lyrics);
                    http.end();
                    return;
                }
            }
            else {
                Serial.print("JSON ERROR (exact get): ");
                Serial.println(err.c_str());
            }
        }

        http.end();
    }

    HTTPClient http;

    String url = "https://lrclib.net/api/search?q="
                 + urlEncode(title + " " + artist);

    http.begin(url);


    http.addHeader("User-Agent", "ESP32-SpotifyDisplay/1.0 (contacto@ejemplo.com)");

    int code = http.GET();

    Serial.print("LRCLIB SEARCH RESPONSE: ");
    Serial.println(code);

    if (code == 200) {

        String payload = http.getString();

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);

        if (err) {
            Serial.print("JSON ERROR: ");
            Serial.println(err.c_str());
            Serial.print("Free heap: ");
            Serial.println(ESP.getFreeHeap());
            Serial.print("Largest contiguous heap: ");
            Serial.println(ESP.getMaxAllocHeap());
            showMessage("No lyrics", "(JSON error)");
            http.end();
            return;
        }

        if (doc.size() > 0) {

            String plain = "";
            String synced = "";
            for (JsonObject candidato : doc.as<JsonArray>()) {

                String candSynced = "";
                String candPlain = "";

                if (!candidato["syncedLyrics"].isNull()) {
                    candSynced = candidato["syncedLyrics"].as<String>();
                }

                if (!candidato["plainLyrics"].isNull()) {
                    candPlain = candidato["plainLyrics"].as<String>();
                }

                if (candSynced != "") {
                    synced = candSynced;
                    break; // best possible option; stop here
                }

                if (candPlain != "" && plain == "" &&
                    candPlain.indexOf("start_ms:") == -1 &&
                    candPlain.indexOf("end_ms:") == -1) {
                    plain = candPlain; // valid candidate; continue
                                       
                }
            }

            if (synced != "") {

                lyrics = synced;
                Serial.println("SYNCHRONIZED LYRICS FOUND:");
                parseLyrics(synced);

            }
            else if (plain != "") {

                lyrics = cleanPlainLyrics(plain);

                Serial.println("PLAIN LYRICS FOUND (not synchronized):");
                Serial.println(lyrics);

               
                splitLyricIntoPages(lyrics);
            }
            else {

                Serial.println("LRCLIB HAS NO LYRICS");
                showMessage("No lyrics", "available");
                http.end();
                return;
            }

        }
        else {

            Serial.println("NO LYRICS (empty response)");
            showMessage("No lyrics", "available");

        }

    }
    else {

        Serial.println("LRCLIB ERROR");
        showMessage("Error", "connecting to LRCLIB");

    }

    http.end();
}

