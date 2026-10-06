#include "core/ZlibStream.h"
#include "voice/DaveSession.h"
#include "voice/JitterBuffer.h"
#include "voice/OpusCodec.h"
#include "voice/Rtp.h"
#include "voice/TransportCipher.h"

#include <QTest>

#include <zlib.h>

#include <cmath>
#include <numbers>
#include <numeric>

class VoicePipelineTest : public QObject
{
    Q_OBJECT

private slots:
    void transportCipherRoundTrip_data();
    void transportCipherRoundTrip();
    void transportCipherRejectsTampering();
    void rtpParsesExtensionPreamble();
    void rtpIgnoresRtcpAndControlPackets();
    void jitterBufferReordersAndConceals();
    void jitterBufferHandlesSequenceWrap();
    void opusRoundTrip();
    void zlibStreamDecodesSyncFlushedMessages();
    void davePassthroughWhenDisabled();
};

void VoicePipelineTest::transportCipherRoundTrip_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("aes-gcm") << static_cast<int>(TransportCipher::Mode::AesGcm);
    QTest::newRow("xchacha20") << static_cast<int>(TransportCipher::Mode::XChaCha20);
}

void VoicePipelineTest::transportCipherRoundTrip()
{
    QFETCH(int, mode);
    std::array<uint8_t, 32> key;
    std::iota(key.begin(), key.end(), uint8_t(1));
    TransportCipher cipher(static_cast<TransportCipher::Mode>(mode), key);

    uint8_t header[Rtp::FixedHeaderSize];
    Rtp::writeHeader(header, 42, 960, 0x1234);
    const std::vector<uint8_t> payload = {1, 2, 3, 4, 5, 6, 7, 8, 9};

    for (int round = 0; round < 3; ++round) {
        std::vector<uint8_t> packet;
        QVERIFY(cipher.seal(header, sizeof(header), payload.data(), payload.size(), packet));
        QCOMPARE(packet.size(), sizeof(header) + payload.size() + TransportCipher::TagSize + TransportCipher::NonceSuffixSize);
        // The nonce counter is appended in big endian and increments per packet.
        QCOMPARE(Rtp::readU32(packet.data() + packet.size() - 4), uint32_t(round));

        const auto parsed = Rtp::parse(packet.data(), packet.size());
        QVERIFY(parsed);
        QCOMPARE(parsed->sequence, uint16_t(42));
        QCOMPARE(parsed->ssrc, uint32_t(0x1234));

        std::vector<uint8_t> plaintext;
        QVERIFY(cipher.open(packet.data(), packet.size(), parsed->clearSize, plaintext));
        QCOMPARE(plaintext, payload);
    }
}

void VoicePipelineTest::transportCipherRejectsTampering()
{
    std::array<uint8_t, 32> key{};
    TransportCipher cipher(TransportCipher::Mode::AesGcm, key);
    uint8_t header[Rtp::FixedHeaderSize];
    Rtp::writeHeader(header, 1, 0, 7);
    const uint8_t payload[] = {9, 9, 9};
    std::vector<uint8_t> packet;
    QVERIFY(cipher.seal(header, sizeof(header), payload, sizeof(payload), packet));

    // The RTP header is authenticated: changing it must make decryption fail.
    packet[3] ^= 0x01;
    std::vector<uint8_t> plaintext;
    QVERIFY(!cipher.open(packet.data(), packet.size(), Rtp::FixedHeaderSize, plaintext));
}

void VoicePipelineTest::rtpParsesExtensionPreamble()
{
    // Version 2 with the extension bit, one CSRC, payload type 120.
    std::vector<uint8_t> packet = {0x91, 0x78, 0x00, 0x05, 0, 0, 0x03, 0xC0, 0, 0, 0, 9, 0, 0, 0, 1,
                                   0xBE, 0xDE, 0x00, 0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22, 0x33};
    const auto header = Rtp::parse(packet.data(), packet.size());
    QVERIFY(header);
    QCOMPARE(header->payloadType, uint8_t(120));
    QCOMPARE(header->sequence, uint16_t(5));
    QCOMPARE(header->timestamp, uint32_t(960));
    QCOMPARE(header->ssrc, uint32_t(9));
    QCOMPARE(header->clearSize, size_t(12 + 4 + 4)); // fixed header + CSRC + extension preamble
    QCOMPARE(header->extensionBodySize, size_t(8));  // 2 words
}

void VoicePipelineTest::rtpIgnoresRtcpAndControlPackets()
{
    const uint8_t rtcp[] = {0x80, 201, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0};
    QVERIFY(!Rtp::parse(rtcp, sizeof(rtcp)));
    const uint8_t discovery[74] = {0x00, 0x02, 0x00, 70};
    QVERIFY(!Rtp::parse(discovery, sizeof(discovery)));
    const uint8_t ping[] = {0x13, 0x37, 0xF0, 0x0D, 0, 0, 0, 1, 0, 0, 0, 0};
    QVERIFY(!Rtp::parse(ping, sizeof(ping)));
}

void VoicePipelineTest::jitterBufferReordersAndConceals()
{
    JitterBuffer buffer;
    std::vector<uint8_t> packet;
    QCOMPARE(buffer.pop(packet), JitterBuffer::Result::Idle);

    // Arrives out of order, with packet 12 missing.
    buffer.push(11, {11});
    buffer.push(10, {10});
    buffer.push(13, {13});

    QCOMPARE(buffer.pop(packet), JitterBuffer::Result::Packet);
    QCOMPARE(packet, std::vector<uint8_t>{10});
    QCOMPARE(buffer.pop(packet), JitterBuffer::Result::Packet);
    QCOMPARE(packet, std::vector<uint8_t>{11});
    QCOMPARE(buffer.pop(packet), JitterBuffer::Result::Lost);
    QCOMPARE(buffer.pop(packet), JitterBuffer::Result::Packet);
    QCOMPARE(packet, std::vector<uint8_t>{13});

    // Once the speaker stops, playback conceals a few frames and then goes idle.
    JitterBuffer::Result result = JitterBuffer::Result::Lost;
    for (int i = 0; i < 10 && result == JitterBuffer::Result::Lost; ++i)
        result = buffer.pop(packet);
    QCOMPARE(result, JitterBuffer::Result::Idle);
}

void VoicePipelineTest::jitterBufferHandlesSequenceWrap()
{
    JitterBuffer buffer;
    buffer.push(65534, {1});
    buffer.push(65535, {2});
    buffer.push(0, {3});
    buffer.push(1, {4});
    std::vector<uint8_t> packet;
    for (uint8_t expected = 1; expected <= 4; ++expected) {
        QCOMPARE(buffer.pop(packet), JitterBuffer::Result::Packet);
        QCOMPARE(packet, std::vector<uint8_t>{expected});
    }
}

void VoicePipelineTest::opusRoundTrip()
{
    OpusEncoderWrapper encoder;
    OpusDecoderWrapper decoder;
    QVERIFY(encoder.isValid());
    QVERIFY(decoder.isValid());

    // A 440 Hz tone survives encoding with most of its energy.
    std::vector<float> input(OpusFormat::FrameSamples * 2);
    std::vector<float> output(OpusFormat::FrameSamples * 2);
    std::vector<uint8_t> packet(OpusFormat::MaxPacketSize);
    double inputEnergy = 0;
    double outputEnergy = 0;
    for (int frame = 0; frame < 10; ++frame) {
        for (int i = 0; i < OpusFormat::FrameSamples; ++i) {
            const double time = (frame * OpusFormat::FrameSamples + i) / 48000.0;
            const float sample = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 440.0 * time));
            input[2 * i] = input[2 * i + 1] = sample;
        }
        const int size = encoder.encode(input.data(), packet.data(), static_cast<int>(packet.size()));
        QVERIFY(size > 0);
        QCOMPARE(decoder.decode(packet.data(), size, output.data(), OpusFormat::FrameSamples), OpusFormat::FrameSamples);
        if (frame >= 5) { // skip the codec warm-up
            for (size_t i = 0; i < input.size(); ++i) {
                inputEnergy += input[i] * input[i];
                outputEnergy += output[i] * output[i];
            }
        }
    }
    QVERIFY(outputEnergy > inputEnergy * 0.5);
    QCOMPARE(decoder.conceal(output.data(), OpusFormat::FrameSamples), OpusFormat::FrameSamples);
}

void VoicePipelineTest::zlibStreamDecodesSyncFlushedMessages()
{
    // Compress two messages on one deflate stream, flushing after each, like the Gateway does.
    z_stream deflater{};
    QCOMPARE(deflateInit(&deflater, Z_DEFAULT_COMPRESSION), Z_OK);
    auto compress = [&](const QByteArray& message) {
        QByteArray out(1024, Qt::Uninitialized);
        deflater.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(message.constData()));
        deflater.avail_in = static_cast<uInt>(message.size());
        deflater.next_out = reinterpret_cast<Bytef*>(out.data());
        deflater.avail_out = static_cast<uInt>(out.size());
        deflate(&deflater, Z_SYNC_FLUSH);
        out.resize(out.size() - deflater.avail_out);
        return out;
    };
    const QByteArray first = compress(R"({"op":10,"d":{"heartbeat_interval":41250}})");
    const QByteArray second = compress(R"({"op":11})");
    deflateEnd(&deflater);

    ZlibStream stream;
    // Messages can arrive split across WebSocket frames.
    QVERIFY(!stream.feed(first.left(5)));
    const auto decodedFirst = stream.feed(first.mid(5));
    QVERIFY(decodedFirst);
    QCOMPARE(*decodedFirst, QByteArray(R"({"op":10,"d":{"heartbeat_interval":41250}})"));
    const auto decodedSecond = stream.feed(second);
    QVERIFY(decodedSecond);
    QCOMPARE(*decodedSecond, QByteArray(R"({"op":11})"));
}

void VoicePipelineTest::davePassthroughWhenDisabled()
{
    // With DAVE protocol version 0 (a call without end-to-end encryption), frames pass through unchanged.
    QList<int> sentJson;
    DaveSession session(QStringLiteral("100"), QStringLiteral("200"), [&](int op, const QJsonObject&) { sentJson.append(op); },
                        [](int, const QByteArray&) {});
    session.addUser(QStringLiteral("101"));
    session.start(0, 1234);

    const std::vector<uint8_t> frame = {0xFC, 0x01, 0x02, 0x03};
    std::vector<uint8_t> encrypted;
    QVERIFY(session.encrypt(1234, frame.data(), frame.size(), encrypted));
    QCOMPARE(encrypted, frame);

    std::vector<uint8_t> decrypted;
    QVERIFY(session.decrypt(QStringLiteral("101"), frame.data(), frame.size(), decrypted));
    QCOMPARE(decrypted, frame);
    QVERIFY(DaveSession::maxSupportedProtocolVersion() >= 1);
}

QTEST_GUILESS_MAIN(VoicePipelineTest)
#include "VoicePipelineTest.moc"
