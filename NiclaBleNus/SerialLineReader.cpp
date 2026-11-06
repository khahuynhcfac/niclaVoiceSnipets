#include "SerialLineReader.h"

bool SerialLineReader::readLineFrom(Stream& stream, String& line) {
    while (stream.available()) {
        const char received = static_cast<char>(stream.read());
        if (received == '\r') continue;

        if (received != '\n') {
            _partialLine += received;
            continue;
        }

        _partialLine.trim();
        if (_partialLine.length() == 0) continue;

        line         = _partialLine;
        _partialLine = "";
        return true;
    }
    return false;
}
