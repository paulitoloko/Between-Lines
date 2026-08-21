#include <TJpg_Decoder.h>
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <LiquidCrystal_I2C.h>
#include "mbedtls/base64.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <WiFiClientSecure.h>
#include "esp_sleep.h"
#include "esp_log.h"
#include "driver/rtc_io.h"
#include "config.h"

#include "DisplayArtwork.h"
#include "WiFiManager.h"
#include "LyricsService.h"
#include "SpotifyService.h"
#include "LcdLyrics.h"
#include "Controls.h"


Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);
LiquidCrystal_I2C lcd(LCD_ADDR, 20, 4);
String accessToken = "";
String coverURL = "";
String lyrics = "";
String lastTrackId = "";

unsigned long lyricTimes[200];
String lyricLines[200];
int lyricCount = 0;
int currentLyric = -1;
String pageLine1[40];
String pageLine2[40];

int totalPages = 0;
int currentPage = 0;

// Long lyric pages are selected from the current LRC time, not from a
// fixed wall-clock delay. This keeps each transition aligned to the song.

unsigned long lastLocalTick = 0;

// Positive values show the lyric slightly earlier; negative values delay it.
// Start with 200 ms to compensate normal HTTPS/player response latency.
const long OFFSET_MS = 200;
// One player-state request every 4 s (15/min) is a conservative balance:
// tighter than the original 5 s while avoiding an aggressive polling rate.
const unsigned long SPOTIFY_POLL_INTERVAL_MS = 4000;
unsigned long lastSpotifyPoll = 0;

unsigned long progressMs = 0;
unsigned long durationMs = 1;
bool isPlaying = false;
bool wasPlaying = false;
uint8_t spotifyNetworkFailures = 0;

// Spotify networking runs on a separate task. The main loop remains the only
// task that writes to the LCD/TFT, so lyrics never pause during an HTTPS poll.
struct SpotifyUpdate {
    bool ready;
    int status;
    bool hasTrack;
    bool playing;
    unsigned long progress;
    unsigned long duration;
    char id[80];
    char title[160];
    char artist[160];
    char album[160];
    char cover[320];
};

SpotifyUpdate pendingSpotifyUpdate = {};
volatile bool spotifyPollRequested = false;
TaskHandle_t spotifyTaskHandle = nullptr;
portMUX_TYPE spotifyUpdateMux = portMUX_INITIALIZER_UNLOCKED;
volatile unsigned long lastWiFiRecoveryAttempt = 0;

const int BTN_PAUSE = 25;
const int BTN_NEXT  = 26;
const int BTN_PREV  = 27;
const int BTN_POWER = 33;

const unsigned long DEBOUNCE_MS = 300; // debounce

unsigned long lastPressPause = 0;
unsigned long lastPressNext  = 0;
unsigned long lastPressPrev  = 0;
unsigned long lastPressPower = 0;
// --------------------------
int lastStatePause = HIGH;
int lastStateNext  = HIGH;
int lastStatePrev  = HIGH;
int lastStatePower = HIGH;
// ---- Function prototypes (forward declarations) ----
void showSongInfo(String title, String artist);
void drawCover(String url);
void updateSpotifyDisplay(String title, String artist);
void getCurrentSong();
void getCurrentSongBlocking();
void startSpotifyPollTask();
void spotifyPollTask(void* parameter);
void applyPendingSpotifyUpdate();
void updateCurrentLyric(unsigned long currentTime);
void splitLyricIntoPages(const String& originalLyric);
void showPage();
void drawProgressBar(unsigned long elapsed, unsigned long total);
void showMessage(String line1, String line2);
String getAccessToken();
void parseLyrics(String syncedLyrics);
void getLyrics(String title, String artist, String album, unsigned long trackDurationMs);
String cleanPlainLyrics(String plain);
String urlEncode(String str);
void connectWiFi();
void recoverWiFiNetwork();
void spotifyPause();
void spotifyPlay();
void spotifyNext();
void spotifyPrevious();
void handleButtons();
void enterStandby();
bool songEndChecked = false;

constexpr int16_t COVER_X = 0;
constexpr int16_t COVER_Y = 0;
constexpr int16_t COVER_SIZE = 128;





void setup() {
    currentLyric = -1;
    currentPage = 0;
    totalPages = 0;
    Serial.begin(115200);
    // The ESP32 TLS stack may log an EOF even after a completed HTTP response.
    // Keep those internal messages out of the monitor; functional errors are
    // still reported by this firmware in a rate-limited way.
    // Suppress framework-only logs; this firmware still reports real request
    // failures itself without flooding the serial monitor.
    esp_log_level_set("*", ESP_LOG_NONE);
    esp_log_level_set("ssl_client", ESP_LOG_NONE);
    esp_log_level_set("WiFiClientSecure", ESP_LOG_NONE);

    delay(2000);

    Serial.println("ESP32 STARTED");
    Serial.println("BUILD: DIRECT-SYNC | WiFi-8s | Cover-128 | Lyrics-pages");


    Wire.begin(LCD_SDA, LCD_SCL);

    lcd.init();
    lcd.backlight();
    lcd.clear();

    pinMode(BTN_PAUSE, INPUT_PULLUP);
    pinMode(BTN_NEXT, INPUT_PULLUP);
    pinMode(BTN_PREV, INPUT_PULLUP);
    pinMode(BTN_POWER, INPUT_PULLUP);


    SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

    tft.initR(INITR_BLACKTAB);
    tft.setRotation(2);


    // The medium Spotify image is 300 px; 1/2 scale gives a 150 px image,
    // then drawCover() centre-crops it to the 128x128 TFT cover area.
    TJpgDec.setJpgScale(2);
    TJpgDec.setCallback(tftOutput);


    tft.fillScreen(ST77XX_BLACK);


    connectWiFi();

    Serial.println("BEFORE TOKEN");

    accessToken = getAccessToken();
    // Direct, bounded polling is used for reliable seek and track detection.

    Serial.println("AFTER TOKEN");

    Serial.println(accessToken != "" ? "Spotify token ready" : "Spotify token unavailable");
    delay(1000);
    lastTrackId = "";
    getCurrentSong();
}

void loop() {
    // Apply network results on the display task before drawing this frame.
    applyPendingSpotifyUpdate();
    handleButtons();

    // Re-sync from Spotify every 4 s instead of every 5 s. This reduces
    // drift while keeping player-state requests conservative.
    if (millis() - lastSpotifyPoll > SPOTIFY_POLL_INTERVAL_MS) {
        getCurrentSong();
        lastSpotifyPoll = millis();
    }

    if (isPlaying) {

        unsigned long elapsedSincePoll =
            millis() - lastLocalTick;

        unsigned long displayedProgress =
            progressMs + elapsedSincePoll;

        if (displayedProgress > durationMs) {
            displayedProgress = durationMs;
        }

        drawProgressBar(
            displayedProgress,
            durationMs
        );

        long adjusted =
            (long)displayedProgress + OFFSET_MS;

        if (adjusted < 0) {
            adjusted = 0;
        }

        updateCurrentLyric(
            (unsigned long)adjusted
        );
    }

    delay(20);
}