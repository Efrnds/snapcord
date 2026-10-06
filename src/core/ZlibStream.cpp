#include "core/ZlibStream.h"

#include <zlib.h>

#include <array>

struct ZlibStream::State
{
    z_stream stream{};
    bool initialized = false;
};

ZlibStream::ZlibStream()
    : m_state(std::make_unique<State>())
{
    reset();
}

ZlibStream::~ZlibStream()
{
    if (m_state->initialized)
        inflateEnd(&m_state->stream);
}

void ZlibStream::reset()
{
    if (m_state->initialized)
        inflateEnd(&m_state->stream);
    m_state->stream = {};
    m_state->initialized = inflateInit(&m_state->stream) == Z_OK;
    m_pending.clear();
}

std::optional<QByteArray> ZlibStream::feed(const QByteArray& chunk)
{
    static const QByteArray suffix = QByteArray::fromHex("0000ffff");

    m_pending.append(chunk);
    if (!m_pending.endsWith(suffix))
        return std::nullopt;

    if (!m_state->initialized)
        return QByteArray();

    QByteArray output;
    std::array<char, 64 * 1024> buffer;
    z_stream& stream = m_state->stream;
    stream.next_in = reinterpret_cast<Bytef*>(m_pending.data());
    stream.avail_in = static_cast<uInt>(m_pending.size());

    do {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        const int result = inflate(&stream, Z_SYNC_FLUSH);
        if (result != Z_OK && result != Z_BUF_ERROR) {
            m_pending.clear();
            return QByteArray();
        }
        output.append(buffer.data(), static_cast<qsizetype>(buffer.size() - stream.avail_out));
    } while (stream.avail_out == 0);

    m_pending.clear();
    return output;
}
