#ifdef ENABLE_TESTS
#include "server_connection.h"
#include <QCoreApplication>
#include <QDebug>

void testNewlineBugFix()
{
    qDebug() << "=== testNewlineBugFix ===";

    ServerConnection conn;

    // Build: JSON header + binary payload with 0x0A at offset 100, all one chunk
    QByteArray testData;
    testData.append("{\"type\":\"image\",\"modality\":\"test\",\"size\":1000}\n");
    QByteArray payload(1000, 0x55);
    payload[100] = 0x0A;   // false newline inside binary data
    testData.append(payload);

    // Feed it directly into the receive buffer and process
    conn.m_receiveBuffer = testData;
    conn.processReceiveBuffer();

    // After processing, m_imageBuffer should contain exactly the 1000 payload bytes
    // starting with 0x55, not sliced at the false 0x0A
    if (conn.m_imageBuffer.size() == 1000 &&
        (uint8_t)conn.m_imageBuffer[0] == 0x55 &&
        (uint8_t)conn.m_imageBuffer[100] == 0x0A)
    {
        qDebug() << "PASS: binary payload intact, false newline not treated as delimiter";
    }
    else
    {
        qDebug() << "FAIL: m_imageBuffer.size()=" << conn.m_imageBuffer.size()
                 << " expected 1000";
        qDebug() << "FAIL: payload was sliced at the false newline";
    }

    qDebug() << "=== done ===";
}
#endif