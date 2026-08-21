bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    // TJpg_Decoder sends blocks that can extend beyond the crop area. Clip
    // both the destination and source so no pixel is drawn outside 128x128.
    const int16_t left = x < COVER_X ? COVER_X : x;
    const int16_t top = y < COVER_Y ? COVER_Y : y;
    const int16_t right = (x + static_cast<int16_t>(w)) > (COVER_X + COVER_SIZE)
                        ? (COVER_X + COVER_SIZE) : (x + static_cast<int16_t>(w));
    const int16_t bottom = (y + static_cast<int16_t>(h)) > (COVER_Y + COVER_SIZE)
                         ? (COVER_Y + COVER_SIZE) : (y + static_cast<int16_t>(h));

    if (right <= left || bottom <= top) return true;

    const int16_t sourceX = left - x;
    const int16_t sourceY = top - y;
    const int16_t drawWidth = right - left;
    const int16_t drawHeight = bottom - top;
    tft.drawRGBBitmap(left, top, bitmap + sourceY * w + sourceX, drawWidth, drawHeight);
    return true;
}
// Configuration

class CoverBufferStream : public Stream {
  public:
    CoverBufferStream(uint8_t* data, size_t capacity) : data_(data), capacity_(capacity), used_(0), overflow_(false) {}
    using Print::write;
    size_t write(uint8_t value) override { return write(&value, 1); }
    size_t write(const uint8_t* source, size_t length) override {
        const size_t room = capacity_ - used_;
        const size_t count = length < room ? length : room;
        if (count > 0) {
            memcpy(data_ + used_, source, count);
            used_ += count;
        }
        if (count != length) overflow_ = true;
        return count;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    size_t size() const { return used_; }
    bool overflowed() const { return overflow_; }
  private:
    uint8_t* data_;
    size_t capacity_;
    size_t used_;
    bool overflow_;
};

void drawCover(String url) {
    if (url == "") {
        Serial.println("Cover URL is empty");
        return;
    }

    // Spotify's image CDN occasionally closes the first TLS handshake on this
    // board. Retry the cover alone; do not reset WiFi or affect lyric loading.
    for (int attempt = 1; attempt <= 3; ++attempt) {
        Serial.printf("Downloading cover (attempt %d/3)...\n", attempt);
        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(12000);

        HTTPClient http;
        http.setConnectTimeout(12000);
        http.setTimeout(12000);
        http.setReuse(false);

        bool complete = false;
        if (!http.begin(client, url)) {
            Serial.println("Cover request could not start");
        } else {
            const int code = http.GET();
            Serial.printf("Cover HTTP response: %d\n", code);

            if (code == HTTP_CODE_OK) {
                const int len = http.getSize();
                Serial.printf("Cover size: %d bytes\n", len);
                if (len > 0 && len <= 100000) {
                    uint8_t* jpgData = static_cast<uint8_t*>(malloc(len));
                    if (jpgData != nullptr) {
                        CoverBufferStream coverBuffer(jpgData, static_cast<size_t>(len));
                        const int transferred = http.writeToStream(&coverBuffer);
                        if (transferred == len && coverBuffer.size() == static_cast<size_t>(len) && !coverBuffer.overflowed()) {
                            tft.fillRect(0, 0, 128, 128, ST77XX_BLACK);
                            const JRESULT result = TJpgDec.drawJpg(-11, -11, jpgData, len);
                            complete = (result == JDR_OK);
                            if (!complete) Serial.printf("JPEG decode error: %d\n", result);
                        } else {
                            Serial.printf("Cover transfer failed: %d bytes, buffer=%u/%d\n",
                                          transferred, (unsigned)coverBuffer.size(), len);
                        }
                        free(jpgData);
                    } else {
                        Serial.println("Not enough memory for cover");
                    }
                } else {
                    Serial.println("Invalid cover size");
                }
            } else {
                Serial.println("Cover download error");
            }
        }

        http.end();
        client.stop();
        if (complete) {
            Serial.println("Cover received complete");
            return;
        }
        if (attempt < 3) delay(800);
    }

    Serial.println("Cover unavailable after 3 attempts");
}