#ifndef SERIAL_LINE_READER_H
#define SERIAL_LINE_READER_H

#include <Arduino.h>

// Assembles newline-terminated lines from a byte stream, holding the partial
// line across loop() calls so nothing blocks waiting for the terminator.
class SerialLineReader {
public:
    // Consumes the bytes waiting on the stream and returns true as soon as one
    // complete, non-empty line is ready in `line`. Call it again to drain any
    // further lines. Carriage returns are discarded and lines are trimmed.
    bool readLineFrom(Stream& stream, String& line);

private:
    String _partialLine;
};

#endif  // SERIAL_LINE_READER_H
