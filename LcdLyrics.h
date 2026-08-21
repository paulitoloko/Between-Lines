constexpr uint8_t LCD_LETTER_ROW_1 = 0;
constexpr uint8_t LCD_LETTER_ROW_2 = 1;
constexpr uint8_t LCD_UNUSED_ROW = 2;
constexpr uint8_t LCD_PROGRESS_ROW = 3;
constexpr size_t LCD_COLUMNS = 20;

// Prints exactly one bounded LCD row. No character can flow into another row.
void writeLcdLine(uint8_t row, const String& text) {
    lcd.setCursor(0, row);
    const size_t longitud = text.length() < LCD_COLUMNS ? text.length() : LCD_COLUMNS;

    for (size_t columna = 0; columna < LCD_COLUMNS; ++columna) {
        lcd.print(columna < longitud ? text.charAt(columna) : ' ');
    }
}

void showMessage(String line1, String line2) {
    writeLcdLine(LCD_LETTER_ROW_1, line1);
    writeLcdLine(LCD_LETTER_ROW_2, line2);
    writeLcdLine(LCD_UNUSED_ROW, "");

    totalPages = 0;
    currentPage = 0;
}

void showPage() {
    if (totalPages == 0 || currentPage < 0 || currentPage >= totalPages) return;

    // Lyrics are limited to rows 0 and 1. Clear row 2 on each update so
    // no previous text can remain there; row 3 belongs only to progress.
    writeLcdLine(LCD_LETTER_ROW_1, pageLine1[currentPage]);
    writeLcdLine(LCD_LETTER_ROW_2, pageLine2[currentPage]);
    writeLcdLine(LCD_UNUSED_ROW, "");
}
void updateSynchronizedPage(int lyricIndex, unsigned long currentTime) {
    if (totalPages <= 1 || lyricIndex < 0 || lyricIndex >= lyricCount) return;

    const unsigned long lineStart = lyricTimes[lyricIndex];
    // The next LRC timestamp defines the real reading window for this line.
    // A short fallback is used only for the final lyric, where no next mark exists.
    const unsigned long lineEnd = (lyricIndex + 1 < lyricCount)
                                  ? lyricTimes[lyricIndex + 1]
                                  : lineStart + 6000UL;
    if (lineEnd <= lineStart) return;

    const unsigned long lineDuration = lineEnd - lineStart;
    const unsigned long elapsed = currentTime > lineStart
                                     ? currentTime - lineStart : 0UL;

    int targetPage = totalPages - 1;
    if (elapsed < lineDuration) {
        unsigned long totalWeight = 0;
        for (int i = 0; i < totalPages; ++i) {
            // Longer pages receive a proportionally longer reading time.
            totalWeight += pageLine1[i].length() + pageLine2[i].length();
        }

        if (totalWeight > 0) {
            const unsigned long weightedProgress =
                static_cast<unsigned long>((static_cast<uint64_t>(elapsed) * totalWeight) / lineDuration);
            unsigned long limit = 0;
            for (int i = 0; i < totalPages; ++i) {
                limit += pageLine1[i].length() + pageLine2[i].length();
                if (weightedProgress < limit) {
                    targetPage = i;
                    break;
                }
            }
        }
    }

    if (targetPage != currentPage) {
        currentPage = targetPage;
        showPage();
    }
}

void updateCurrentLyric(unsigned long currentTime) {
    int line = -1;

    for (int i = 0; i < lyricCount; i++) {
        if (currentTime >= lyricTimes[i]) {
            line = i;
        } else {
            break;
        }
    }

    if (line < 0) return;

    if (line != currentLyric) {
        currentLyric = line;
        Serial.print("CURRENT LYRIC: ");
        Serial.println(lyricLines[line]);
        splitLyricIntoPages(lyricLines[line]);
    }

    // Re-evaluate on every loop. Seeking forward/backward immediately selects
    // the page that belongs to the actual player position.
    updateSynchronizedPage(line, currentTime);
}
// Adds one finished 20x2 page without moving text into the next page.
bool saveLyricPage(const char* line1, const char* line2) {
    if (totalPages >= 40) return false;

    pageLine1[totalPages] = line1;
    pageLine2[totalPages] = line2;
    ++totalPages;
    return true;
}

bool isLyricSeparator(char character) {
    return character == ' ' || character == '\t';
}

void splitLyricIntoPages(const String& originalLyric) {
    constexpr size_t LCD_COLUMNS = 20;

    totalPages = 0;
    currentPage = 0;

    // Read the lyric in place. Unlike the earlier version, this never removes
    // text from a String while it is being tokenized.
    const char* text = originalLyric.c_str();
    size_t start = 0;
    size_t end = originalLyric.length();

    while (start < end && isLyricSeparator(text[start])) ++start;
    while (end > start && isLyricSeparator(text[end - 1])) --end;

    char line1[LCD_COLUMNS + 1] = {0};
    char line2[LCD_COLUMNS + 1] = {0};
    uint8_t currentLine = 1;
    size_t position = start;

    while (position < end) {
        while (position < end && isLyricSeparator(text[position])) ++position;
        if (position >= end) break;

        const size_t wordStart = position;
        while (position < end && !isLyricSeparator(text[position])) ++position;
        const size_t wordEnd = position;
        size_t wordPosition = wordStart;

        // Each character is consumed only from left to right. A word changes
        // line or page only when it does not fit in the current destination.
        while (wordPosition < wordEnd) {
            char* destination = (currentLine == 1) ? line1 : line2;
            const size_t destinationLength = strlen(destination);
            const bool continuingWord = wordPosition > wordStart;
            const size_t separator = (!continuingWord && destinationLength > 0) ? 1 : 0;
            const size_t pendingCharacters = wordEnd - wordPosition;

            // A full line cannot accept a separator. Calculate the available
            // space only after this check, preventing unsigned underflow and
            // any write beyond the 20-character LCD buffer.
            if (destinationLength >= LCD_COLUMNS) {
                if (currentLine == 1) {
                    currentLine = 2;
                } else {
                    if (!saveLyricPage(line1, line2)) {
                        position = end;
                        break;
                    }
                    line1[0] = '\0';
                    line2[0] = '\0';
                    currentLine = 1;
                }
                continue;
            }

            const size_t freeSpace = LCD_COLUMNS - destinationLength - separator;

            if (pendingCharacters <= freeSpace) {
                size_t writePosition = destinationLength;
                if (separator == 1) destination[writePosition++] = ' ';
                memcpy(destination + writePosition, text + wordPosition, pendingCharacters);
                destination[writePosition + pendingCharacters] = '\0';
                wordPosition = wordEnd;
                continue;
            }

            if (destinationLength == 0) {
                // A single word longer than the LCD is split only by necessity,
                // retaining its original character order across the next line.
                const size_t fragment = pendingCharacters < LCD_COLUMNS ? pendingCharacters : LCD_COLUMNS;
                memcpy(destination, text + wordPosition, fragment);
                destination[fragment] = '\0';
                wordPosition += fragment;
                continue;
            }

            // Do not rotate or copy a previous line. Advance to the next line,
            // or save the complete page and begin a new empty one.
            if (currentLine == 1) {
                currentLine = 2;
            } else {
                if (!saveLyricPage(line1, line2)) {
                    position = end;
                    break;
                }
                line1[0] = '\0';
                line2[0] = '\0';
                currentLine = 1;
            }
        }
    }

    if ((line1[0] != '\0' || line2[0] != '\0') && totalPages < 40) {
        saveLyricPage(line1, line2);
    }

    if (totalPages == 0) {
        pageLine1[0] = "";
        pageLine2[0] = "";
        totalPages = 1;
    }

    showPage();
}

void drawProgressBar(unsigned long elapsed, unsigned long total) {

    if (total == 0) {
        return;
    }

    int percentage = (elapsed * 100) / total;

    int filled = map(percentage, 0, 100, 0, 20);

    // The fourth physical row is reserved exclusively for progress blocks,
    // never for lyric characters.
    lcd.setCursor(0, LCD_PROGRESS_ROW);

    for (int i = 0; i < 20; i++) {

        if (i < filled) {
            lcd.write(255);
        }
        else {
            lcd.print(" ");
        }
    }
}
// ---- Physical button control ----


