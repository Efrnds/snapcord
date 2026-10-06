#pragma once

#include <QByteArray>

#include <memory>
#include <optional>

// Decompressor for the Gateway "zlib-stream" transport compression: one zlib context shared by the whole
// connection, where each complete message ends with the Z_SYNC_FLUSH suffix 00 00 FF FF.
class ZlibStream
{
public:
    ZlibStream();
    ~ZlibStream();

    ZlibStream(const ZlibStream&) = delete;
    ZlibStream& operator=(const ZlibStream&) = delete;

    void reset();

    // Returns the decompressed message once a full one has arrived, std::nullopt while waiting for more data.
    // Returns an empty array on a decompression error.
    std::optional<QByteArray> feed(const QByteArray& chunk);

private:
    struct State;
    std::unique_ptr<State> m_state;
    QByteArray m_pending;
};
